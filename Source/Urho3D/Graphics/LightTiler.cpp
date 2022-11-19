#include "LightTiler.h"
#include "../Scene/Node.h"
#include "../Core/Context.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Light.h"
#include <MathGeoLib.h>

namespace Urho3D
{

//****************************************************************************
//
//  Function:   LightTiler::LightTiler
//
//  Purpose:    Constructs buffers for Tiled/Clustered lighting methods.
//              Actual scheme is arbitrary, and could be:
//              
//              XY: Forward-tiled
//              XZ: Just Cause 2
//              XYZ: clustered
//
//              Because the light recording is done on the CPU here the tests
//              are crude AABB tests that will cause a lot of false positives.
//
//****************************************************************************
struct ClusterInfo {
    Vector3 minVec;
    float pad0;
    Vector3 maxVec;
    float pad1;
    IntVector3 tiles;
    int pad2;
};

struct uint4 {
    unsigned x_;
    unsigned y_;
    unsigned z_;
    unsigned w_;
};

struct LightData
{
    Vector3 position_;
    float radius_;
    Vector4 color_;
};

LightTiler::LightTiler(Context* context, IntVector3 cells, uint32_t lightsPerCell) :
    Object(context),
    lightsPerCell_(lightsPerCell),
    tileDim_(cells)
{
    maxLights_ = 300;

    auto cd = context->GetSubsystem<ComputeDevice>();

    cellsUBO_ = new ComputeBuffer(context);
    cellsUBO_->SetSize(sizeof(uint4) * CellCount(), sizeof(uint4));

    lightsUBO_ = new ComputeBuffer(context);
    lightsUBO_->SetSize(sizeof(LightData) * maxLights_, sizeof(LightData));

    lightIndexesUBO_ = new ComputeBuffer(context);
    lightIndexesUBO_->SetSize(sizeof(unsigned) * lightsPerCell_ * CellCount(), sizeof(unsigned));

    clusterInfo_ = new ComputeBuffer(context);
    clusterInfo_->SetSize(sizeof(ClusterInfo), sizeof(ClusterInfo));
}

LightTiler::~LightTiler()
{
    // dtor take care of it
}

Vector3 Vector3_ToSphericalCoordinates(Vector3 self)
{
    // R_y * R_x * (0,0,length) = (cosx*siny, -sinx, cosx*cosy).
    Vector3 v = self;
    float len = v.Length();
    v.Normalize();
    if (len <= 1e-5f)
        return Vector3();
    float azimuth = atan2(v.x_, v.z_);
    float inclination = asin(-v.y_);
    return Vector3(azimuth, inclination, len);
}

Vector3 AABB_CornerPoint(const BoundingBox& bnds, int index)
{
    switch (index)
    {
    default: // For release builds where assume() is disabled, return always the first option if out-of-bounds.
    case 0: return bnds.min_;
    case 1: return Vector3(bnds.min_.x_, bnds.min_.y_, bnds.max_.z_);
    case 2: return Vector3(bnds.min_.x_, bnds.max_.y_, bnds.min_.z_);
    case 3: return Vector3(bnds.min_.x_, bnds.max_.y_, bnds.max_.z_);
    case 4: return Vector3(bnds.max_.x_, bnds.min_.y_, bnds.min_.z_);
    case 5: return Vector3(bnds.max_.x_, bnds.min_.y_, bnds.max_.z_);
    case 6: return Vector3(bnds.max_.x_, bnds.max_.y_, bnds.min_.z_);
    case 7: return bnds.max_;
    }
}

//****************************************************************************
//
//  Function:   LightTiler::BuildLightTables_Radial
//
//  Purpose:    For VR we can use spherical-coordinates in order to do both eyes at once.
//              Assumes the light-list provided contains.
//
//  WARNING:    Camera[0] needs to be a "control" "head" camera for frustum info
//              and the head position. Only actually supports up to 3 cameras,
//              doesn't currently work in a CAVE setting.
//
//****************************************************************************
#pragma optimize("", off)
uint32_t LightTiler::BuildLightTables_Radial(Camera** cameras, int camCt, const Vector< SharedPtr<Light> >& lights)
{
    const float zStep = 1.0f / tileDim_.z_;
    const float yStep = 1.0f / tileDim_.y_;
    const float xStep = 1.0f / tileDim_.x_;

    Vector<uint4> lightCounts(CellCount(), { 0, 0, 0, 0 });
    Vector<uint32_t> lightIndices(CellCount() * lightsPerCell_);
    Vector<LightData> lightRawData;

    uint32_t hitCt = 0;

    for (auto& l : lights)
    {
        LightData d;
        //d.lightMat = l->GetShadowMatrix(0);
        //d.lightPos = float4(l->GetPosition(), l->GetKind());
        //d.lightDir = float4(l->GetDirection(), l->GetRadius());
        //d.color = l->GetColor();
        //d.shadowMapCoords[0] = l->GetShadowDomain(0);
        //d.shadowMapCoords[1] = l->GetShadowDomain(1);
        //d.fov = l->GetFOV();
        //d.castShadows = l->IsShadowCasting() ? 1.0f : 0.0f;
        //d.goboIndex = 0.0f;
        //d.rampIndex = 0.0f;
        lightRawData.Push(d);
    }

    Camera* headCam = cameras[0], *leftEye = nullptr, *rightEye = nullptr;
    if (camCt > 1)
    {
        leftEye = cameras[1];
        rightEye = cameras[2];
    }

    leftEye = leftEye == nullptr ? headCam : leftEye;
    rightEye = rightEye == nullptr ? headCam : rightEye;

    const auto frus = headCam->GetFrustum();
    const auto nearDist = leftEye->GetNearClip();
    const auto farDist = leftEye->GetFarClip();

    // bottom left
    const Vector3 minVec = (leftEye->GetFrustum().vertices_[6] - leftEye->GetFrustum().vertices_[2]).Normalized();
    // top right
    const Vector3 maxVec = (rightEye->GetFrustum().vertices_[4] - rightEye->GetFrustum().vertices_[0]).Normalized();

    ClusterInfo info;
    info.minVec = minVec;
    info.maxVec = maxVec;
    info.tiles = tileDim_;
    clusterInfo_->SetData(&info, sizeof(info), sizeof(ClusterInfo));

    Matrix3 sphereSpaceTransform = Quaternion(Vector3(0.0f, 0.0f, 1.0f), minVec).RotationMatrix();

    const auto headCamPos = headCam->GetNode()->GetWorldPosition();
    const auto totalAngle = Vector3_ToSphericalCoordinates(maxVec) - Vector3_ToSphericalCoordinates(minVec);
    for (unsigned lightIndex = 0; lightIndex < lights.Size() && lightIndex < maxLights_; ++lightIndex)
    {
        auto& l = lights[lightIndex];
        auto aabb = l->GetWorldBoundingBox();

        Vector3 pts[8];
        for (int i = 0; i < 8; ++i)
            pts[i] = Vector3_ToSphericalCoordinates(AABB_CornerPoint(aabb, i) - headCamPos);

        auto minPt = pts[0];
        auto maxPt = pts[0];
        for (int i = 1; i < 8; ++i)
        {
            minPt = VectorMin(minPt, pts[i]);
            maxPt = VectorMax(maxPt, pts[i]);
        }

        int zStartIndex = toSliceZ(minPt.z_, nearDist, farDist);
        int zEndIndex = toSliceZ(maxPt.z_, nearDist, farDist);

        int yStartIndex = (int)Floor(math::InvLerp(minVec.y_, maxVec.y_, minPt.x_) * tileDim_.x_);
        int yEndIndex = (int)Ceil(math::InvLerp(minVec.y_, maxVec.y_, maxPt.x_) * tileDim_.x_);

        int xStartIndex = (int)Floor(math::InvLerp(minVec.x_, maxVec.x_, minPt.x_) * tileDim_.x_);
        int xEndIndex = (int)Ceil(math::InvLerp(minVec.x_, maxVec.x_, maxPt.x_) * tileDim_.x_);

        // Now cull the light, casts aren't optional
        if ((zStartIndex < 0 && zEndIndex < 0) || (zStartIndex >= (int)tileDim_.z_ && zEndIndex >= (int)tileDim_.z_))
            continue;

        if ((yStartIndex < 0 && yEndIndex < 0) || (yStartIndex >= (int)tileDim_.y_ && yEndIndex >= (int)tileDim_.y_))
            continue;

        if ((xStartIndex < 0 && xEndIndex < 0) || (xStartIndex >= (int)tileDim_.x_ && xEndIndex >= (int)tileDim_.x_))
            continue;

        zStartIndex = Clamp<int>(zStartIndex, 0, tileDim_.z_ - 1);
        zEndIndex = Clamp<int>(zEndIndex, 0, tileDim_.z_ - 1);

        yStartIndex = Clamp<int>(yStartIndex, 0, tileDim_.y_ - 1);
        yEndIndex = Clamp<int>(yEndIndex, 0, tileDim_.y_ - 1);

        xStartIndex = Clamp<int>(xStartIndex, 0, tileDim_.x_ - 1);
        xEndIndex = Clamp<int>(xEndIndex, 0, tileDim_.x_ - 1);

        for (auto z = zStartIndex; z <= zEndIndex; ++z)
        {
            for (auto y = yStartIndex; y <= yEndIndex; ++y)
            {
                for (auto x = xStartIndex; x <= xEndIndex; ++x)
                {
                    auto clusterID = x + y * tileDim_.x_ + z * tileDim_.x_ * tileDim_.y_;
                    lightIndices[clusterID * lightsPerCell_ + lightCounts[clusterID].x_ % lightsPerCell_] = lightIndex;
                    lightCounts[clusterID].x_ += 1;
                    ++hitCt;
                }
            }
        }
    }

    cellsUBO_->SetData(lightCounts.Buffer(), sizeof(uint4)* lightCounts.Size(), sizeof(uint4));
    lightsUBO_->SetData(lightRawData.Buffer(), sizeof(LightData)* lightRawData.Size(), sizeof(LightData));
    lightIndexesUBO_->SetData(lightIndices.Buffer(), sizeof(uint32_t)* lightIndices.Size(), sizeof(uint32_t));
    transform_ = sphereSpaceTransform;

    return hitCt;
}
#pragma optimize("", on)

}
