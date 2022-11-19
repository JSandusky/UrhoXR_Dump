#include "VR.h"

#include "../Graphics/Camera.h"
#include "../IO/FileSystem.h"
#include "../Graphics/Graphics.h"
#include "../Graphics/Geometry.h"
#include "../IO/Log.h"
#include "../Graphics/Material.h"
#include "../Graphics/Model.h"
#include "../Scene/Node.h"
#include "../Graphics/RenderPath.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/Scene.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/Texture.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/View.h"

#include "../Graphics/VertexBuffer.h"
#include "../Graphics/IndexBuffer.h"

#include "../Engine/Engine.h"
#include "../Core/CoreEvents.h"
#include "../Graphics/GraphicsEvents.h"
#include "../Scene/SceneEvents.h"

#include "../Graphics/Shader.h"
#include "../Resource/JSONFile.h"
#include "../Resource/JSONValue.h"
#include "../Resource/XMLFile.h"

#include "VREvents.h"

#include <thread>
#include <chrono>

#pragma optimize("", off)

namespace Urho3D
{

String GetTrackedDevicePropString(vr::IVRSystem* system, vr::TrackedDeviceIndex_t index, vr::TrackedDeviceProperty prop)
{
    char name[512] = { };
    vr::ETrackedPropertyError err;
    auto len = system->GetStringTrackedDeviceProperty(index, vr::Prop_RenderModelName_String, name, 511, &err);
    if (err == vr::TrackedProp_Success)
        return String(name, len);

    return String();
}

String VR_CompositorError(vr::EVRCompositorError err)
{
    switch (err)
    {
    case vr::VRCompositorError_None: return "None";
    case vr::VRCompositorError_RequestFailed: return "Request Failed";
    case vr::VRCompositorError_IncompatibleVersion: return "Incompatible Version";
    case vr::VRCompositorError_DoNotHaveFocus: return "Do Not Have Focus";
    case vr::VRCompositorError_InvalidTexture: return "Invalid Texture";
    case vr::VRCompositorError_IsNotSceneApplication: return "Is Not Scene Application";
    case vr::VRCompositorError_TextureIsOnWrongDevice: return "Texture is on wrong device";
    case vr::VRCompositorError_TextureUsesUnsupportedFormat: return "Uses unsupported format";
    case vr::VRCompositorError_SharedTexturesNotSupported: return "Shared textures not supported";
    case vr::VRCompositorError_IndexOutOfRange: return "Index out of range";
    case vr::VRCompositorError_AlreadySubmitted: return "Already submitted";
    case vr::VRCompositorError_InvalidBounds: return "Invalid Bounds";
    case vr::VRCompositorError_AlreadySet: return "Already Set";
    }
    return "None";
}

String VR_InputError(vr::EVRInputError err)
{
    switch (err)
    {
    case vr::VRInputError_NameNotFound: return "Name not found";
    case vr::VRInputError_WrongType: return "Wrong type";
    case vr::VRInputError_InvalidHandle: return "Invalid handle";
    case vr::VRInputError_InvalidParam: return "Invalid param";
    case vr::VRInputError_NoSteam: return "No Steam";
    case vr::VRInputError_MaxCapacityReached: return "Max capacity reached";
    case vr::VRInputError_IPCError: return "IPC Error";
    case vr::VRInputError_NoActiveActionSet: return "No active action set";
    case vr::VRInputError_InvalidDevice: return "Invalid device";
    case vr::VRInputError_InvalidSkeleton: return "Invalid skeleton";
    case vr::VRInputError_InvalidBoneCount: return "Invalid bone count";
    case vr::VRInputError_InvalidCompressedData: return "Invalid compressed data";
    case vr::VRInputError_NoData: return "No data";
    case vr::VRInputError_BufferTooSmall: return "Buffer too small";
    case vr::VRInputError_MismatchedActionManifest: return "Mismatched action manfiest";
    case vr::VRInputError_MissingSkeletonData: return "Missing skeleton data";
    case vr::VRInputError_InvalidBoneIndex: return "Invalid bone index";
    case vr::VRInputError_InvalidPriority: return "Invalid priority";
    case vr::VRInputError_PermissionDenied: return "Permission denied";
    case vr::VRInputError_InvalidRenderModel: return "Invalid render model";
    }
    return "None";
}

Vector3 SteamVR::ToUrho(const vr::HmdVector3_t& v) const
{
    return Vector3(v.v[0], v.v[1], -v.v[2]);
}

Matrix4 SteamVR::ToUrho(const vr::HmdMatrix34_t &matPose) const
{
    //return Matrix3x4(
    //    matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
    //    matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], matPose.m[2][3],
    //    matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3]) 
    //    *
    //    Matrix3x4(1.0f, 0.0f, 0.0f, 0.0f,
    //        0.0f, 1.0f, 0.0f, 0.0f,
    //        0.0f, 0.0f, -1.0f, 0.0f);
    //return Matrix3x4(
    //        matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
    //        matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], -matPose.m[2][3],
    //        matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3]
    //        );

    const auto s = scaleCorrection_;
    return Matrix4(
        s, 0, 0, 0,
        0, s, 0, 0,
        0, 0, s, 0,
        0, 0, 0, 1
    )
        *
    Matrix4(
        matPose.m[0][0], matPose.m[0][1], -matPose.m[0][2], matPose.m[0][3],
        matPose.m[1][0], matPose.m[1][1], -matPose.m[1][2], matPose.m[1][3] + heightCorrection_,
        -matPose.m[2][0], -matPose.m[2][1], matPose.m[2][2], -matPose.m[2][3],
        ////1, 0, 0, 1
        matPose.m[3][0], matPose.m[3][1], matPose.m[3][2], matPose.m[3][3]
    );
}

Matrix4 SteamVR::ToUrho(const vr::HmdMatrix44_t &matPose) const
{
    //return Matrix4(
    //    mat.m[0][0], mat.m[1][0], -mat.m[2][0], mat.m[3][0],
    //    mat.m[0][1], mat.m[1][1], -mat.m[2][1], mat.m[3][1],
    //    -mat.m[0][2], -mat.m[1][2], mat.m[2][2], -mat.m[3][2],
    //    mat.m[0][3], mat.m[1][3], mat.m[2][3], mat.m[3][3]
    //);
    return Matrix4(
        matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
        matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3],
        matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], -matPose.m[2][3],
        //0, 0, 0, 0
        matPose.m[3][0], matPose.m[3][1], matPose.m[3][2], matPose.m[3][3]
    );
}

auto vrHandToIndex(VRHand hand)
{
    return hand == VR_HAND_LEFT ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
}

auto vrEyeToIndex(VREye eye)
{
    return eye == VR_EYE_LEFT ? vr::Eye_Left : vr::Eye_Right;
}

SteamVR::SteamVRBinding::SteamVRBinding(Context* ctx) :
    XRBinding(ctx)
{

}

SteamVR::SteamVRBinding::~SteamVRBinding()
{

}

SteamVR::SteamVRActionSet::SteamVRActionSet(Context* ctx) :
    XRActionGroup(ctx)
{

}

SteamVR::SteamVRActionSet::~SteamVRActionSet() 
{ 
}

SteamVR::SteamVR(Context* ctx) : VRInterface(ctx)
{
    SubscribeToEvent(E_BEGINFRAME, URHO3D_HANDLER(SteamVR, HandlePreUpdate));
    SubscribeToEvent(E_POSTPRESENT, URHO3D_HANDLER(SteamVR, HandlePostRender));

    for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i)
        poses_[i].bDeviceIsConnected = false;
}

SteamVR::~SteamVR()
{
    Shutdown();
}

bool SteamVR::Initialize(const String& manifestPath)
{
    auto engine = GetSubsystem<Engine>();
    engine->SetMaxFps(90);

    vr::EVRInitError error = vr::VRInitError_None;

    auto hmd = vr::VR_Init(&error, vr::VRApplication_Scene);
    if (error != vr::VRInitError_None)
    {
        URHO3D_LOGERROR(vr::VR_GetVRInitErrorAsEnglishDescription(error));
        return false;
    }

    if (!vr::VRCompositor())
    {
        URHO3D_LOGERROR("VR compositor initialization failed");
        return false;
    }

    if (hmd == nullptr)
    {
        URHO3D_LOGERROR("VR system interface initialization failed");
        return false;
    }

    vrSystem_ = hmd;
    vrSystem_->GetRecommendedRenderTargetSize(&eyeWidth_, &eyeHeight_);
    trueEyeWidth_ = eyeWidth_;
    trueEyeHeight_ = eyeHeight_;

    eyeWidth_ *= renderScale_;
    eyeHeight_ *= renderScale_;

    CreateEyeTextures();

    auto fs = GetSubsystem<FileSystem>();
    auto progDir = AddTrailingSlash(fs->GetProgramDir());
    auto manifestFile = progDir + manifestPath;

    vr::VRInput()->GetInputSourceHandle("/user/head", &headInputHandle_);
    vr::VRInput()->GetInputSourceHandle("/user/hand/left", &handInputHandles_[0]);
    vr::VRInput()->GetInputSourceHandle("/user/hand/right", &handInputHandles_[1]);

    if (fs->FileExists(manifestFile))
    {
        auto error = vr::VRInput()->SetActionManifestPath(manifestFile.CString());
        if (error != vr::EVRInputError::VRInputError_None)
            URHO3D_LOGERRORF("VR manifest error: %s", VR_InputError(error).CString());

        ParseManifestFile(manifestFile);
    }
    else
        URHO3D_LOGWARNING("No haptics found");

    LoadHiddenAreaMesh();

    vr::ETrackedPropertyError propErr = vr::TrackedProp_Success;
    float fps = vrSystem_->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float, &propErr);
    if (propErr == vr::TrackedProp_Success)
        engine->SetMaxFps(fps); // 90hz? if we for somereason don't get it
    else
        engine->SetMaxFps(90);

    SetCurrentActionSet("actions/Default");

    return true;
}

void SteamVR::CreateEyeTextures()
{
    sharedTexture_.Reset();
    leftTexture_.Reset();
    rightTexture_.Reset();

    sharedDS_.Reset();
    leftDS_.Reset();
    rightDS_.Reset();

    if (useSingleTexture_)
    {
        sharedTexture_ = new Texture2D(GetContext());
        sharedTexture_->SetNumLevels(1);
        sharedTexture_->SetSize(eyeWidth_ * 2, eyeHeight_, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET, msaaLevel_);
        sharedTexture_->SetFilterMode(FILTER_BILINEAR);

        sharedDS_ = new Texture2D(GetContext());
        sharedDS_->SetNumLevels(1);
        sharedDS_->SetSize(eyeWidth_ * 2, eyeHeight_, Graphics::GetDepthStencilFormat(), TEXTURE_DEPTHSTENCIL, msaaLevel_);
        sharedTexture_->GetRenderSurface()->SetLinkedDepthStencil(sharedDS_->GetRenderSurface());
    }
    else
    {
        leftTexture_ = new Texture2D(GetContext());
        leftTexture_->SetNumLevels(1);
        leftTexture_->SetSize(eyeWidth_, eyeHeight_, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET, msaaLevel_);
        leftTexture_->SetFilterMode(FILTER_BILINEAR);

        rightTexture_ = new Texture2D(GetContext());
        rightTexture_->SetNumLevels(1);
        rightTexture_->SetSize(eyeWidth_, eyeHeight_, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET, msaaLevel_);
        rightTexture_->SetFilterMode(FILTER_BILINEAR);

        leftDS_ = new Texture2D(GetContext());
        leftDS_->SetNumLevels(1);
        leftDS_->SetSize(eyeWidth_, eyeHeight_, Graphics::GetDepthStencilFormat(), TEXTURE_DEPTHSTENCIL, msaaLevel_);

        rightDS_ = new Texture2D(GetContext());
        rightDS_->SetNumLevels(1);
        rightDS_->SetSize(eyeWidth_, eyeHeight_, Graphics::GetDepthStencilFormat(), TEXTURE_DEPTHSTENCIL, msaaLevel_);

        leftTexture_->GetRenderSurface()->SetLinkedDepthStencil(leftDS_->GetRenderSurface());
        rightTexture_->GetRenderSurface()->SetLinkedDepthStencil(rightDS_->GetRenderSurface());
    }
}

void SteamVR::LoadHiddenAreaMesh()
{
    // Grab the hidden area meshes
    for (int i = 0; i < 2; ++i)
    {
        auto leftMesh = vrSystem_->GetHiddenAreaMesh(i == 0 ? vr::Eye_Left : vr::Eye_Right);
        hiddenAreaMesh_[i] = new Geometry(GetContext());

        VertexBuffer* vbo = new VertexBuffer(GetContext());
        vbo->SetSize(leftMesh.unTriangleCount * 3, { VertexElement(TYPE_VECTOR3, SEM_POSITION, 0, 0) });

        PODVector<Vector3> verts;
        for (unsigned i = 0; i < leftMesh.unTriangleCount; ++i)
        {
            verts.Push({ leftMesh.pVertexData[i * 3 + 0].v[0], leftMesh.pVertexData[i * 3 + 0].v[1], 0.0f });
            verts.Push({ leftMesh.pVertexData[i * 3 + 1].v[0], leftMesh.pVertexData[i * 3 + 1].v[1], 0.0f });
            verts.Push({ leftMesh.pVertexData[i * 3 + 2].v[0], leftMesh.pVertexData[i * 3 + 2].v[1], 0.0f });
        }
        vbo->SetData(verts.Buffer());
        hiddenAreaMesh_[i]->SetVertexBuffer(0, vbo);
        hiddenAreaMesh_[i]->SetDrawRange(TRIANGLE_LIST, 0, 0, 0, verts.Size(), true);
    }
}

void SteamVR::LoadRenderModels()
{
    // Load model controllers if possible
    for (int i = 0; i < 2; ++i)
    {
        uint32_t handIndex = (uint32_t)(vrSystem_->GetTrackedDeviceIndexForControllerRole(i == 1 ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand));

        // if we have a texture then we're already long done
        if (wandMeshes_[i].texture_)
            continue;

        String controllerName = GetTrackedDevicePropString(vrSystem_, handIndex, vr::Prop_RenderModelName_String);
        if (controllerName.Length())
        {
            wandMeshes_[i].name_ = controllerName;

            // if we have a geometry then we don't need to query again
            if (wandMeshes_[i].geometry_.Null())
            {
                auto r = vr::VRRenderModels()->LoadRenderModel_Async(controllerName.CString(), &wandMeshes_[i].model_);

                if (r == vr::VRRenderModelError_None)
                {
                    if (wandMeshes_[i].geometry_.Null())
                    {
                        URHO3D_LOGWARNING("Loaded wand model");
                        auto& model = wandMeshes_[i].model_;
                        unsigned indexCount = model->unTriangleCount * 3;

                        VertexBuffer* vbo = new VertexBuffer(GetContext());
                        IndexBuffer* ibo = new IndexBuffer(GetContext());

                        BoundingBox bnds;

                        PODVector<vr::RenderModel_Vertex_t> vertices;
                        for (unsigned v = 0; v < model->unVertexCount; ++v)
                        {
                            vertices.Push(model->rVertexData[v]);
                            vertices[v].vPosition.v[2] *= -1; // our Z goes the other way
                            if (v == 0)
                                bnds.Define(Vector3(vertices[v].vPosition.v[0], vertices[v].vPosition.v[1], vertices[v].vPosition.v[2]));
                            else
                                bnds.Merge(Vector3(vertices[v].vPosition.v[0], vertices[v].vPosition.v[1], vertices[v].vPosition.v[2]));
                        }

                        vbo->SetSize(model->unVertexCount, {
                            VertexElement(TYPE_VECTOR3, SEM_POSITION, 0),
                            VertexElement(TYPE_VECTOR3, SEM_NORMAL, 0),
                            VertexElement(TYPE_VECTOR2, SEM_TEXCOORD, 0) });
                        vbo->SetData(vertices.Buffer());

                        ibo->SetSize(indexCount, false);
                        ibo->SetData(model->rIndexData);

                        wandMeshes_[i].bounds_ = bnds;
                        wandMeshes_[i].geometry_ = new Geometry(GetContext());
                        wandMeshes_[i].geometry_->SetVertexBuffer(0, vbo);
                        wandMeshes_[i].geometry_->SetIndexBuffer(ibo);
                        wandMeshes_[i].geometry_->SetDrawRange(TRIANGLE_LIST, 0, indexCount, 0, model->unVertexCount);
                    }
                }
            }

            // only try for texture if we have geometry but no texture and a valid texture ID
            if (wandMeshes_[i].geometry_.NotNull() && wandMeshes_[i].model_->diffuseTextureId >= 0 && wandMeshes_[i].texture_.Null())
            {
                auto r = vr::VRRenderModels()->LoadTexture_Async(wandMeshes_[i].model_->diffuseTextureId, &wandMeshes_[i].colorTex_);

                if (r == vr::VRRenderModelError_None)
                {
                    URHO3D_LOGWARNING("Loaded wand texture");
                    if (wandMeshes_[i].texture_.Null())
                    {
                        auto& tex = wandMeshes_[i].colorTex_;
                        wandMeshes_[i].texture_ = new Texture2D(GetContext());
                        wandMeshes_[i].texture_->SetSize(tex->unWidth, tex->unHeight, Graphics::GetRGBAFormat());
                        wandMeshes_[i].texture_->SetData(0, 0, 0, wandMeshes_[i].texture_->GetWidth(), wandMeshes_[i].texture_->GetHeight(), tex->rubTextureMapData);
                    }
                }
            }
        }
    }
}

void SteamVR::Shutdown()
{
    if (vrSystem_)
    {
        vrSystem_ = nullptr;
        for (int i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i)
            poses_[i].bDeviceIsConnected = false;

        sharedTexture_.Reset();
        leftTexture_.Reset();
        rightTexture_.Reset();

        wandMeshes_[0].Free();
        wandMeshes_[1].Free();

        vr::VR_Shutdown();
    }
}

void SteamVR::HandlePreUpdate(StringHash, VariantMap&)
{
    if (vrSystem_ != nullptr)
    {
        vr::VREvent_t event;
        while (vrSystem_->PollNextEvent(&event, sizeof(event)))
        {
            switch (event.eventType)
            {
            case vr::VREvent_EnterStandbyMode: {
                auto& data = GetEventDataMap();
                data[VRPause::P_STATE] = true;
                SendEvent(E_VRPAUSE, data);
                sessionLive_ = false;
            } break;
            case vr::VREvent_DashboardActivated: {
                auto& data = GetEventDataMap();
                data[VRPause::P_STATE] = true;
                SendEvent(E_VRPAUSE, data);
                sessionLive_ = false;
            } break;
            case vr::VREvent_DashboardDeactivated: {
                auto& data = GetEventDataMap();
                data[VRPause::P_STATE] = false;
                SendEvent(E_VRPAUSE, data);
                sessionLive_ = true;
            } break;
            case vr::VREvent_LeaveStandbyMode: {
                auto& data = GetEventDataMap();
                data[VRPause::P_STATE] = false;
                SendEvent(E_VRPAUSE, data);
                sessionLive_ = true;
            } break;
            case vr::VREvent_Input_BindingsUpdated: {
                //?? does this invalidate our action handles?
                SendEvent(E_VRINTERACTIONPROFILECHANGED);
            } break;
            case vr::VREvent_Quit:
            //case vr::VREvent_ProcessQuit:
            case vr::VREvent_DriverRequestedQuit:
                SendEvent(E_VREXIT);
                Shutdown();
                return;
            }
        }

        unsigned poseID;
        vr::VRCompositor()->GetLastPosePredictionIDs(nullptr, &poseID);
        vr::VRCompositor()->GetPosesForFrame(poseID, poses_, vr::k_unMaxTrackedDeviceCount);

        // Update bindings if we have a valid action set
        if (activeActionSet_)
        {
            auto set = activeActionSet_->Cast<SteamVRActionSet>();
            vr::VRActiveActionSet_t activeInputSets;
            activeInputSets.ulActionSet = set->actionSet_;
            activeInputSets.nPriority = 100; //??
            activeInputSets.ulSecondaryActionSet = vr::k_ulInvalidActionSetHandle;
            activeInputSets.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
            
            vr::VRInput()->UpdateActionState(&activeInputSets, sizeof(vr::VRActiveActionSet_t), 1);
            UpdateBindingValues();
        }

        LoadRenderModels();

        if (autoClearMasks_)
            DrawEyeMask();

        //vr::VRCompositor()->WaitGetPoses(poses_, vr::k_unMaxTrackedDeviceCount, NULL, 0);
    }
}

Matrix3x4 SteamVR::GetHeadTransform() const
{
    if (poses_[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid)
        return Matrix3x4(ToUrho(poses_[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking));
    return Matrix3x4();
}

Matrix3x4 SteamVR::GetHandTransform(VRHand hand) const
{
    if (vrSystem_ == nullptr)
        return Matrix3x4();

    uint32_t handIndex = (uint32_t)(vrSystem_->GetTrackedDeviceIndexForControllerRole(vrHandToIndex(hand)));
    if (handIndex < vr::k_unMaxTrackedDeviceCount && poses_[handIndex].bPoseIsValid)
        return Matrix3x4(ToUrho(poses_[handIndex].mDeviceToAbsoluteTracking));

    return Matrix3x4();
}

void SteamVR::GetHandVelocity(VRHand hand, Vector3* linear, Vector3* ang) const
{
    if (vrSystem_ == nullptr)
        return;

    uint32_t handIndex = (uint32_t)(vrSystem_->GetTrackedDeviceIndexForControllerRole(vrHandToIndex(hand)));
    if (handIndex < vr::k_unMaxTrackedDeviceCount && poses_[handIndex].bPoseIsValid)
    {
        if (linear)
            *linear = ToUrho(poses_[handIndex].vVelocity);
        if (ang)
            *ang = ToUrho(poses_[handIndex].vAngularVelocity);
    }
}

Matrix3x4 SteamVR::GetEyeLocalTransform(VREye eye) const
{
    if (vrSystem_)
        return Matrix3x4(ToUrho(vrSystem_->GetEyeToHeadTransform(vrEyeToIndex(eye))));
    return Matrix3x4();
}

Matrix3x4 SteamVR::GetHandAimTransform(VRHand hand) const
{
    if (vrSystem_)
    {
        vr::RenderModel_ControllerMode_State_t cState = { 0 };
        vr::RenderModel_ComponentState_t state;
        memset(&state, 0, sizeof(state));
        if (vr::VRRenderModels()->GetComponentStateForDevicePath(wandMeshes_[hand].name_.CString(), vr::k_pch_Controller_Component_Tip, handInputHandles_[hand], &cState, &state))
            return GetHandTransform(hand) * Matrix3x4(ToUrho(state.mTrackingToComponentLocal));
    }
    return Matrix3x4();
}

Ray SteamVR::GetHandAimRay(VRHand isRight) const
{
    if (vrSystem_)
    {
        auto aimTrans = GetHandAimTransform(isRight);
        if (aimTrans == Matrix3x4::IDENTITY)
            return Ray();

        return Ray(aimTrans.Translation(), (aimTrans * Vector3(0, 0, 1)).Normalized());
    }
    return Ray();
}

Matrix4 SteamVR::GetProjection(VREye eye, float near, float far) const
{
    if (vrSystem_)
    {
        auto m = ToUrho(vrSystem_->GetProjectionMatrix(vrEyeToIndex(eye), near, far)) * -1.0f;
        return m;
    }
    return Matrix4();
}

void SteamVR::TriggerHaptic(VRHand hand, float duration, float frequency, float amp)
{
    if (hapticHandles_[hand] != 0)
        vr::VRInput()->TriggerHapticVibrationAction(hapticHandles_[hand], 0.0f, duration, frequency, amp, handInputHandles_[hand]);
}

void SteamVR::PrepareRig(Node* headRoot)
{
    headRoot->SetWorldPosition(Vector3(0, 0, 0));
    headRoot->SetWorldRotation(Quaternion::IDENTITY);
    auto head = headRoot->CreateChild("Head", LOCAL);

    auto leftEye = head->CreateChild("Left_Eye", LOCAL);
    auto rightEye = head->CreateChild("Right_Eye", LOCAL);

    auto leftCam = leftEye->GetOrCreateComponent<Camera>();
    auto rightCam = rightEye->GetOrCreateComponent<Camera>();

    auto leftHand = headRoot->CreateChild("Left_Hand", LOCAL);
    auto rightHand = headRoot->CreateChild("Right_Hand", LOCAL);
}

void SteamVR::UpdateRig(Node* vrRig, float nearDist, float farDist, bool forSinglePass)
{
    auto head = vrRig->GetChild("Head");
    auto leftEye = head->GetChild("Left_Eye");
    auto rightEye = head->GetChild("Right_Eye");

    UpdateRig(head->GetScene(), head, leftEye, rightEye, nearDist, farDist, forSinglePass);
}

void SteamVR::UpdateRig(Scene* scene, Node* head, Node* leftEye, Node* rightEye, float nearDist, float farDist, bool forSinglePass)
{
    if (vrSystem_ == nullptr)
        return;

    if (head == nullptr)
    {
        auto headRoot = scene->CreateChild("VRRig", LOCAL);
        head = headRoot->CreateChild("Head", LOCAL);
    }

    // no textures? create them now?
    if (sharedTexture_.Null() && leftTexture_.Null() && rightTexture_.Null())
        CreateEyeTextures();

    head->SetTransform(GetHeadTransform());

    if (leftEye == nullptr)
        leftEye = head->CreateChild("Left_Eye", LOCAL);
    if (rightEye == nullptr)
        rightEye = head->CreateChild("Right_Eye", LOCAL);

    auto leftCam = leftEye->GetOrCreateComponent<Camera>();
    auto rightCam = rightEye->GetOrCreateComponent<Camera>();

    leftCam->SetFov(110.0f);  // junk mostly, the eye matrices will be overriden
    leftCam->SetNearClip(nearDist);
    leftCam->SetFarClip(farDist);

    rightCam->SetFov(110.0f); // junk mostly, the eye matrices will be overriden
    rightCam->SetNearClip(nearDist);
    rightCam->SetFarClip(farDist);
    
    leftCam->SetProjection(GetProjection(VR_EYE_LEFT, nearDist, farDist));
    rightCam->SetProjection(GetProjection(VR_EYE_RIGHT, nearDist, farDist));

    leftEye->SetTransform(GetEyeLocalTransform(VR_EYE_LEFT));
    rightEye->SetTransform(GetEyeLocalTransform(VR_EYE_RIGHT));

    leftEye->Rotate(Quaternion(0, 0, 180), TS_LOCAL);
    rightEye->Rotate(Quaternion(0, 0, 180), TS_LOCAL);

    float ipdAdjust = ipdCorrection_ * 0.5f;
    leftEye->Translate({ ipdAdjust, 0, 0 }, TS_LOCAL);
    rightEye->Translate({ -ipdAdjust, 0, 0 }, TS_LOCAL);

    if (sharedTexture_ && forSinglePass)
    {
        auto surface = sharedTexture_->GetRenderSurface();

        if (surface->GetViewport(0) == nullptr)
        {
            XMLFile* rp = GetSubsystem<ResourceCache>()->GetResource<XMLFile>("RenderPaths/Forward_VR.xml");
            SharedPtr<Viewport> view(new Viewport(GetContext(), scene, leftCam, nullptr));
            view->SetLeftEye(leftCam);
            view->SetRightEye(rightCam);
            view->SetCullCamera(leftCam);
            view->SetRect({ 0, 0, sharedTexture_->GetWidth(), sharedTexture_->GetHeight() });
            view->SetRenderPath(rp);
            surface->SetViewport(0, view);
        }
        else
        {
            auto view = surface->GetViewport(0);
            view->SetScene(scene);
            view->SetCullCamera(leftCam);
            view->SetLeftEye(leftCam);
            view->SetRightEye(rightCam);
        }

        surface->QueueUpdate();
    }
    else
    {
        auto leftSurface = useSingleTexture_ ? sharedTexture_->GetRenderSurface() : leftTexture_->GetRenderSurface();
        auto rightSurface = useSingleTexture_ ? sharedTexture_->GetRenderSurface() : rightTexture_->GetRenderSurface();

        if (leftSurface->GetViewport(0) == nullptr)
        {
            SharedPtr<Viewport> leftView(new Viewport(GetContext(), scene, leftCam));
            SharedPtr<Viewport> rightView(new Viewport(GetContext(), scene, rightCam));

            leftView->SetRect(GetLeftEyeRect());
            rightView->SetRect(GetRightEyeRect());

            leftSurface->SetViewport(0, leftView);
            rightSurface->SetViewport(1, rightView);
        }
        else
        {
            auto leftView = leftSurface->GetViewport(0);
            leftView->SetScene(scene);
            leftView->SetCamera(leftCam);

            auto rightView = rightSurface->GetViewport(1);
            rightView->SetScene(scene);
            rightView->SetCamera(rightCam);
        }

        leftSurface->SetUpdateMode(SURFACE_UPDATEALWAYS);
        rightSurface->SetUpdateMode(SURFACE_UPDATEALWAYS);
    }
}

void SteamVR::UpdateHands(Scene* scene, Node* rigRoot, Node* leftHand, Node* rightHand)
{
    if (vrSystem_ == nullptr)
        return;

    if (leftHand == nullptr)
        leftHand = rigRoot->CreateChild("Left_Hand");
    if (rightHand == nullptr)
        rightHand = rigRoot->CreateChild("Right_Hand");

    auto leftM = leftHand->GetOrCreateComponent<StaticModel>();
    auto rightM = rightHand->GetOrCreateComponent<StaticModel>();

    SetupModel(leftM, false);
    SetupModel(rightM, true);

    leftHand->SetTransform(GetHandTransform(VR_HAND_LEFT));
    rightHand->SetTransform(GetHandTransform(VR_HAND_RIGHT));

    uint32_t leftHandIndex = (uint32_t)vrSystem_->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
    uint32_t rightHandIndex = (uint32_t)vrSystem_->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);

    if (leftHandIndex == vr::k_unTrackedDeviceIndexInvalid)
        leftHand->SetEnabled(false);
    else
        leftHand->SetEnabled(poses_[leftHandIndex].bPoseIsValid && poses_[leftHandIndex].bDeviceIsConnected);

    if (rightHandIndex == vr::k_unTrackedDeviceIndexInvalid)
        rightHand->SetEnabled(false);
    else
        rightHand->SetEnabled(poses_[rightHandIndex].bPoseIsValid && poses_[rightHandIndex].bDeviceIsConnected);
}

void SteamVR::HandlePostRender(StringHash, VariantMap&)
{
    if (vrSystem_ == nullptr)
        return;

    vr::VRCompositor()->WaitGetPoses(poses_, vr::k_unMaxTrackedDeviceCount, NULL, 0);
    vr::EVRCompositorError err = vr::VRCompositorError_None;

    if (useSingleTexture_)
    {
        vr::Texture_t sharedTexture = { sharedTexture_->GetGPUObject(), vr::TextureType_DirectX, vr::ColorSpace_Gamma };
        vr::VRTextureBounds_t leftBounds;
        leftBounds.uMin = 0.0f; leftBounds.uMax = 0.5f;
        leftBounds.vMin = 0.0f; leftBounds.vMax = 1.0f;
        vr::VRTextureBounds_t rightBounds;
        rightBounds.uMin = 0.5f; rightBounds.uMax = 1.0f;
        rightBounds.vMin = 0.0f; rightBounds.vMax = 1.0f;

        // if MSAA then we need to send the resolve texture
        if (sharedTexture_->GetMultiSample() > 1)
        {
            GetSubsystem<Graphics>()->ResolveToTexture(sharedTexture_);
            sharedTexture.handle = sharedTexture_->GetResolveTexture();
        }
        
        err = vr::VRCompositor()->Submit(vr::Eye_Left, &sharedTexture, &leftBounds);
        if (err != vr::VRCompositorError_None)
        {
            URHO3D_LOGERROR("LeftEyeError: " + VR_CompositorError(err));
        }
        err = vr::VRCompositor()->Submit(vr::Eye_Right, &sharedTexture, &rightBounds);
        if (err != vr::VRCompositorError_None)
        {
            URHO3D_LOGERROR("RightEyeError: " + VR_CompositorError(err));
        }
    }
    else
    {
        vr::Texture_t leftEyeTexture = { leftTexture_->GetGPUObject(), vr::TextureType_DirectX, vr::ColorSpace_Gamma };
        vr::Texture_t rightEyeTexture = { rightTexture_->GetGPUObject(), vr::TextureType_DirectX, vr::ColorSpace_Gamma };

        // if MSAA then we need to send the resolve texture
        if (leftTexture_->GetMultiSample() > 1)
        {
            GetSubsystem<Graphics>()->ResolveToTexture(leftTexture_);
            leftEyeTexture.handle = leftTexture_->GetResolveTexture();
        }

        err = vr::VRCompositor()->Submit(vr::Eye_Left, &leftEyeTexture);
        if (err != vr::VRCompositorError_None)
        {
            URHO3D_LOGERROR("LeftEyeError: " + VR_CompositorError(err));
        }

        // if MSAA then we need to send the resolve texture (done after submit, so that if there's work (deformation) then it can be done while we resolve if we need to.
        if (rightTexture_->GetMultiSample() > 1)
        {
            GetSubsystem<Graphics>()->ResolveToTexture(rightTexture_);
            leftEyeTexture.handle = rightTexture_->GetResolveTexture();
        }

        err = vr::VRCompositor()->Submit(vr::Eye_Right, &rightEyeTexture);
        if (err != vr::VRCompositorError_None)
        {
            URHO3D_LOGERROR("RightEyeError: " + VR_CompositorError(err));
        }
    }
}

void SteamVR::SetupModel(StaticModel* target, bool isRight)
{
    if (wandMeshes_[isRight].geometry_ && target->GetModel() == nullptr)
    {
        auto mdl = new Model(GetContext());
        mdl->SetNumGeometries(1);
        mdl->SetGeometry(0, 0, wandMeshes_[isRight].geometry_);
        mdl->SetBoundingBox(wandMeshes_[isRight].bounds_);
        target->SetModel(mdl);
    }

    if (target->GetMaterial() == nullptr)
        target->SetMaterial(GetSubsystem<ResourceCache>()->GetResource<Material>("Materials/DefaultGrey.xml")->Clone());

    if (wandMeshes_[isRight].texture_)
    {
        auto mat = target->GetMaterial();
        if (mat && mat->GetTexture(TU_DIFFUSE) == nullptr)
            mat->SetTexture(TU_DIFFUSE, wandMeshes_[isRight].texture_);
    }
}

void SteamVR::ControlMesh::Free()
{
    //if (model_)
    //    vr::VRRenderModels()->FreeRenderModel(model_);
    //model_ = nullptr;
    //if (colorTex_)
    //    vr::VRRenderModels()->FreeTexture(colorTex_);
    //colorTex_ = nullptr;

    texture_.Reset();
    geometry_.Reset();
}

void SteamVR::DrawEyeMask()
{
    auto gfx = GetSubsystem<Graphics>();

    IntRect vpts[] = {
        GetLeftEyeRect(),
        GetRightEyeRect()
    };

    RenderSurface* surfaces[] = {
        GetLeftEyeTexture()->GetRenderSurface(),
        GetRightEyeTexture()->GetRenderSurface()
    };

    Texture2D* ds[] = {
        useSingleTexture_ ? sharedDS_.Get() : leftDS_.Get(),
        useSingleTexture_ ? sharedDS_.Get() : rightDS_.Get(),
    };

    ShaderVariation* vertexShader = gfx->GetShader(VS, "VR_EyeMask", nullptr);
    ShaderVariation* pixelShader = gfx->GetShader(PS, "VR_EyeMask", nullptr);

    gfx->ResetRenderTargets();
    for (int i = 0; i < 2; ++i)
    {        
        gfx->SetRenderTarget(0, surfaces[i]);
        gfx->SetDepthStencil(ds[i]);
        gfx->SetViewport(vpts[i]);
        gfx->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_STENCIL);
        gfx->SetVertexBuffer(hiddenAreaMesh_[i]->GetVertexBuffer(0));
        gfx->SetShaders(vertexShader, pixelShader, nullptr, nullptr, nullptr);
        gfx->SetDepthWrite(true);
        gfx->SetDepthTest(CMP_ALWAYS);
        gfx->SetScissorTest(false);
        gfx->SetStencilTest(false);
        gfx->SetCullMode(CULL_NONE);
        gfx->SetBlendMode(BLEND_REPLACE);
        gfx->SetColorWrite(true);
        gfx->Draw(TRIANGLE_LIST, 0, hiddenAreaMesh_[i]->GetVertexCount());
    }    

    gfx->ResetRenderTargets();
}

void SteamVR::ParseManifestFile(const String& manifestFile)
{
    JSONFile file(GetContext());
    
    if (!file.LoadFile(manifestFile))
        return;

    auto actions = file.GetRoot().Get("actions");
    if (actions.NotNull())
    {
        auto actionArray = actions.GetArray();
        for (unsigned i = 0; i < actionArray.Size(); ++i)
        {
            auto action = actionArray[i];
            String name = action.Get("name").GetString();
            String type = action.Get("type").GetString();
            bool handed = action.Get("handed").GetBool();

            unsigned thirdSlash = name.Find('/');
            thirdSlash = name.Find('/', thirdSlash + 1);
            thirdSlash = name.Find('/', thirdSlash + 1);

            String setName = name.Substring(0, thirdSlash);
            auto foundSet = actionSets_.Find(setName);
            SharedPtr<XRActionGroup> set;
            if (foundSet == actionSets_.End())
            {
                SharedPtr<SteamVRActionSet> s(new SteamVRActionSet(GetContext()));
                vr::VRInput()->GetActionSetHandle(setName.CString(), &s->actionSet_);
                s->name_ = setName;

                actionSets_.Insert({ setName, s });
                set = s;
            }
            else
                set = foundSet->second_;

            SharedPtr<SteamVRBinding> binding(new SteamVRBinding(GetContext()));
            //binding->path_ = name;
            if (type == "boolean")
                binding->dataType_ = VAR_BOOL;
            else if (type == "vector1" || type == "single")
                binding->dataType_ = VAR_FLOAT;
            else if (type == "vector2")
                binding->dataType_ = VAR_VECTOR2;
            else if (type == "vector3")
                binding->dataType_ = VAR_VECTOR3;
            else if (type == "pose")
                binding->dataType_ = VAR_MATRIX3X4;
            else
                binding->dataType_ = VAR_NONE;

            auto err = vr::VRInput()->GetActionHandle(name.CString(), &binding->handle_);
            if (err == vr::VRInputError_None)
            {
                if (handed)
                {
                    binding->handHandle_ = handInputHandles_[0];
                    binding->hand_ = VR_HAND_LEFT;
                    set->bindings_.Push(binding);
                    binding->handHandle_ = handInputHandles_[1];
                    binding->hand_ = VR_HAND_RIGHT;
                    set->bindings_.Push(binding);
                }
                else
                    set->bindings_.Push(binding);
            }
            else
                URHO3D_LOGERRORF("Failed to find VR input binding for %s, code %s", name.CString(), VR_InputError(err).CString());
        }
    }
    else
    {
        URHO3D_LOGERROR("No actions found for VR action manifest");
    }

    auto localization = file.GetRoot().Get("localization");
    if (localization.NotNull())
    {
        auto localizationArray = localization.GetArray();
        for (int i = 0; i < localizationArray.Size(); ++i)
        {
            auto lang = localizationArray[0].GetObject();
            for (auto field : lang)
            {
                if (field.first_.Compare("language_tag", false) == 0)
                    continue;

                for (auto& s : actionSets_)
                {
                    for (auto& b : s.second_->bindings_)
                    {
                        if (b->path_.Compare(field.first_, false) == 0)
                        {
                            b->localizedName_ = field.second_.GetString();
                            break;
                        }
                    }
                }
            }
        }
    }
}

void SteamVR::UpdateBindingValues()
{
    if (activeActionSet_)
    {
        for (auto& b : activeActionSet_->bindings_)
        {
            auto binding = b->Cast<SteamVRBinding>();
            if (binding->handle_ == 0)
                continue;

            switch (binding->dataType_)
            {
            case VAR_BOOL: {
                vr::InputDigitalActionData_t data;
                auto err = vr::VRInput()->GetDigitalActionData(binding->handle_, &data, sizeof(data), binding->hand_);
                if (err != vr::VRInputError_None)
                {
                    binding->storedData_ = false;
                    binding->delta_ = false;
                    continue;
                }

                if (data.bActive)
                {
                    binding->storedData_ = data.bState;
                    binding->changed_ = data.bChanged;
                }
                else
                {
                    binding->storedData_ = false;
                    binding->changed_ = false;
                }
            } break;
            case VAR_FLOAT: {
                vr::InputAnalogActionData_t data;
                auto err = vr::VRInput()->GetAnalogActionData(binding->handle_, &data, sizeof(data), binding->hand_);
                if (err != vr::VRInputError_None)
                    continue;

                binding->active_ = data.bActive;
                if (data.bActive)
                {
                    binding->storedData_ = data.x;
                    binding->delta_ = data.deltaX;
                    binding->changed_ = fabsf(data.deltaX) > FLT_EPSILON;
                }
                else
                    binding->changed_ = false;
            } break;
            case VAR_VECTOR2: {
                vr::InputAnalogActionData_t data;
                auto err = vr::VRInput()->GetAnalogActionData(binding->handle_, &data, sizeof(data), binding->hand_);
                if (err != vr::VRInputError_None)
                    continue;

                binding->active_ = data.bActive;
                if (data.bActive)
                {
                    binding->storedData_ = Vector2(data.x, data.y);
                    auto v = Vector2(data.deltaX, data.deltaY);
                    binding->delta_ = v;
                    binding->changed_ = v.Length() > FLT_EPSILON;
                }
                else
                    binding->changed_ = false;
                break;
            } break;
            case VAR_VECTOR3: {
                vr::InputAnalogActionData_t data;
                auto err = vr::VRInput()->GetAnalogActionData(binding->handle_, &data, sizeof(data), binding->hand_);
                if (err != vr::VRInputError_None)
                    continue;

                binding->active_ = data.bActive;
                if (data.bActive)
                {
                    binding->storedData_ = Vector3(data.x, data.y, -data.z);
                    auto v = Vector3(data.deltaX, data.deltaY, -data.deltaZ);
                    binding->delta_ = v;
                    binding->changed_ = v.Length() > FLT_EPSILON;
                }
                else
                    binding->changed_ = false;
                break;
            } break;
            case VAR_MATRIX3X4: { // pose
                vr::InputPoseActionData_t data;
                auto err = vr::VRInput()->GetPoseActionDataForNextFrame(binding->handle_, vr::ETrackingUniverseOrigin::TrackingUniverseStanding, &data, sizeof(data), binding->hand_);
                if (err != vr::VRInputError_None)
                {
                    URHO3D_LOGERRORF("VR input binding update error: %s", VR_InputError(err).CString());
                    continue;
                }

                binding->active_ = data.bActive;
                if (data.bActive)
                {
                    if (data.pose.eTrackingResult >= 200)
                    {
                        binding->storedData_ = ToUrho(data.pose.mDeviceToAbsoluteTracking);
                        binding->extraData_[0] = ToUrho(data.pose.vVelocity);
                        binding->extraData_[1] = ToUrho(data.pose.vAngularVelocity);
                    }
                    else
                        binding->active_ = false;
                }
            } break;
            }
        }
    }
}

void SteamVR::CheckBindingState()
{
    if (!IsRunning())
        return;

    if (activeActionSet_)
    {
        auto set = activeActionSet_->Cast<SteamVRActionSet>();
        for (auto b : activeActionSet_->bindings_)
        {
            auto binding = b->Cast<SteamVRBinding>();
            vr::InputBindingInfo_t bindingInfo;
            vr::VRInputValueHandle_t origins[2];
            auto err = vr::VRInput()->GetActionOrigins(set->actionSet_, binding->handle_, origins, 2);
            if (err == vr::VRInputError_None)
            {

                //if (binding->IsHanded()) //?? presumably so? querying action info is totally undocumented
                //    binding->isBound_ = origins[binding->Hand()] != 0;
                //else
                // because undocumented and can't be assured they're stable, assume we've got sane persons binding things sensibly
                binding->isBound_ = origins[0] != 0;
            }
        }
    }
}

void SteamVR::SetCurrentActionSet(SharedPtr<XRActionGroup> set)
{
    activeActionSet_ = set;
}

}
