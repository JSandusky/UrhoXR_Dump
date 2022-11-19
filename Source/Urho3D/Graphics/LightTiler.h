#pragma once

#include "../Math/BoundingBox.h"

#include "../Graphics/ComputeBuffer.h"
#include "../Graphics/ComputeDevice.h"

namespace Urho3D
{

class Camera;
class Light;
class Texture;
class Context;

/// Coordinates the management of lighting.
class LightTiler : public Object
{
    URHO3D_OBJECT(LightTiler, Object);
public:
    LightTiler(Context* device, IntVector3 cells, uint32_t lightsPerCell);
    virtual ~LightTiler();

    /// In spherical coordinates instead of vanilla froxels. Based on Min-To-Max range walk.
    uint32_t BuildLightTables_Radial(Camera** cameras, int cameraCt, const Vector< SharedPtr<Light> >& lights);

    /// Stores the counts
    SharedPtr<ComputeBuffer> cellsUBO_;

    /// Stores the LightData structs.
    SharedPtr<ComputeBuffer> lightsUBO_;
    /// Stores the indexes for each cell that map a light to a LightData struct.
    SharedPtr<ComputeBuffer> lightIndexesUBO_;
    SharedPtr<ComputeBuffer> clusterInfo_;

    SharedPtr<ComputeBuffer> iblCubesUBO_;
    SharedPtr<ComputeBuffer> iblCubeIndexesUBO_;

    SharedPtr<ComputeBuffer> decalsUBO_;
    SharedPtr<ComputeBuffer> decalIndexesUBO_;

    SharedPtr<Texture> lightsTex_;

    Matrix4 transform_; // left-multiply
    IntVector3 tileDim_; // uze Z > 1 for clustered.
    uint32_t lightsPerCell_;
    uint32_t maxLights_;

    inline uint32_t toIndex(int x, int y, int z) const { return x + (y * tileDim_.x_) + (z * tileDim_.x_ * tileDim_.y_); }
    inline uint32_t CellCount() const { return tileDim_.x_ * tileDim_.y_ * tileDim_.z_; }
    inline int toSliceZ(float z, float nearDist, float farDist) const {
        float logFrac = log(farDist / nearDist);
        return Floor((log(z) * (tileDim_.z_ / logFrac)) - ((tileDim_.z_ * log(nearDist)) / logFrac));
    }
};

}
