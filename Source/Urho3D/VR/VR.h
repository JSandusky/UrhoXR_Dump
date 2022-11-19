#pragma once

#include "../Core/Object.h"
#include "../Graphics/Drawable.h"
#include "../Scene/LogicComponent.h"
#include "../Math/Ray.h"
#include "../VR/VRInterface.h"

#include <openvr.h>

namespace Urho3D
{

class Camera;
class Geometry;
class Model;
class RenderPath;
class Texture2D;
class Shader;
class StaticModel;
class View;

/**

Register as a subsystem, Initialize sometime after GFX has been initialized but before Audio is initialized ...
    otherwise it won't get the right audio target.

Expectations for the VR-Rig:

    Scene
        - "VRRig" NETWORKED
            - "Head" LOCAL
                - "Left_Eye" LOCAL
                    - Camera
                - "Right_Eye" LOCAL
                    - Camera
            - "Left_Hand" NETWORKED, will have enabled status set based on controller availability
                - StaticModel[0] = controller model
            - "Right_Hand" NETWORKED, will have enabled status set based on controller availability
                - StaticModel[0] = controller model

        Instead of networking those components network whatever state is relevant like (ie. IK targets),
        tag hands against the client-id so server-side logic can deal with them.

        Ideally, don't touch multiplayer in VR.

Expectations for Single-pass VR:
    Single-pass doesn't not work without modifications to Batch drawing to pass an isVR parameter and additional cameras for View.
    It is not possible to compute a single frustum to enclose the view for PIMAX (>180 combined FOV).

    Single-pass can save as much as 20%, save ~15%-20% fill-rate by also calling DrawEyeMasks() to force depths to 0 for hidden areas.
        15% happens to be pretty close to the extra pixel cost of PBR over legacy.
        Using depth-0 since Urho3D uses stencil for other purposes
        VR Frame-times are hard. Better GPUs, 90 fps cap - sometimes you won't even go over 40% GPU utilization despite riding right at 10ms
            Not sending enough work to get the GPU to throttle up in relation to vblanks it sees

    Use a RenderPath with:
        vsdefines="VR"
        psdefines="VR"

    Vertex Shader
        #ifdef VR
            uint iInstanceID : SV_InstanceID,
            out uint oInstanceID : TEXCOORD8,
            out float oClip : SV_ClipDistance0,
            out float oCull : SV_CullDistance0,
        #endif

        #ifdef VR
            oPos = GetClipPos(worldPos, iInstanceID);
            float eyeOffsetScale[2] = {-0.5, 0.5};
            float4 eyeClipEdge[2] = { {-1,0,0,1}, {1,0,0,1} };

            uint eyeIndex = iInstanceID & 1;
            oCull = oClip = dot(oPos, eyeClipEdge[eyeIndex]);

            oPos.x *= 0.5;
            oPos.x += eyeOffsetScale[eyeIndex] * oPos.w;
        #else
            oPos = GetClipPos(worldPos);
        #endif
    
        Perform clipping based on [SV_InstanceID & 1]

    Uniform buffers (use SV_InstanceID & 1 to index):

        cbuffer CameraVS : register(b1)
        {
            #ifdef VR
                float3 cCameraPos[2];
            #else
                float3 cCameraPos;
            #endif
    
            float cNearClip;
            float cFarClip;
            float4 cDepthMode;
            float3 cFrustumSize;
            float4 cGBufferOffsets;
    
            #ifdef VR
                float4x3 cView[2]; // 0 is left, 1 is right
                float4x3 cViewInv[2];
                float4x4 cViewProj[2];
            #else
                float4x3 cView;
                float4x3 cViewInv;
                float4x4 cViewProj;
            #endif
    
            float4 cClipPlane;
        }

VR Specialized Rendering (single texture target):
    Any specialized rendering (like DebugRenderer) that View calls needs to check for eyes
    width is always target-width / 2. Just set it up to draw twice, settings the viewports and
    camera parameters appropriately.

Optimization:
    - Use one of the VR render-targets for the main view with a simple fullscreen blit render-path.
    - Disable reuse shadowmaps if not using single-pass rendering, or using an additional scene render for PC desktop display

To-Do:
    - Render-Scale is only expected to go down from 1.0 and not up ... there's no checks for failure to construct (ie. the target is too large)
        - That's why it's capped at 2x max, most VR capable GPUs can make a render-target 4x as wide as their max (single-texture is already 2x wide)
    - Vive Trackers
        - Specifically they probably mess with height-correction
        - Typically mean limited things:
            - foot trackers
            - waist tracker
            - tool tracker (seen with PVC guns, etc)
        - Mesh loading needs some added changes to support it, always iterate in case of new meshes
    - Utility components
        - controller "fade in", use an inflated bounding box and fade in Controller models as they approach each other
            - because mashing controllers into each other in half-life Alyx sucks
        - Controller space management, bounds awareness and shifting based on collision risks
            - ie. shift relative weapon pose up/down in hand space to avoid ring collisions.
        - Grabber/tosser
    - Support multiple Action-Sets
    - Do suit/vest haptics have any standard?
    - Extract eye-masked images, for easy blit
*/
class URHO3D_API SteamVR : public VRInterface
{
    URHO3D_OBJECT(SteamVR, VRInterface);
public:
    SteamVR(Context*);
    virtual ~SteamVR();

    virtual VRRuntime GetRuntime() const override { return VR_OPENVR; }
    virtual const char* GetRuntimeName() const override { return "OPEN_VR"; }
    
    virtual bool Initialize(const String& manifestPath = "Data/vr_actions.json") override;
    virtual void Shutdown() override;

    virtual bool IsLive() const override { return sessionLive_; }
    virtual bool IsRunning() const override { return vrSystem_ != nullptr; }
    
    void HandlePreUpdate(StringHash, VariantMap&);
    void HandlePostRender(StringHash, VariantMap&);

    virtual Matrix3x4 GetHeadTransform() const override;
    virtual Matrix3x4 GetHandTransform(VRHand hand) const override;
    virtual void GetHandVelocity(VRHand hand, Vector3* linear, Vector3* angular) const override;
    virtual Matrix3x4 GetEyeLocalTransform(VREye eye) const override;
    /// Gets the pose/tip aiming.
    virtual Matrix3x4 GetHandAimTransform(VRHand hand) const override;
    /// For the pose/tip aiming (like a gun).
    virtual Ray GetHandAimRay(VRHand hand) const;

    virtual void TriggerHaptic(VRHand hand, float durationSeconds, float frequency, float strength) override;

    void SetupModel(StaticModel* target, bool isRight);

    /// Sets up the Head, Left_Eye, Right_Eye nodes.
    void PrepareRig(Node* vrRig);
    void UpdateRig(Node* vrRig, float nearDist, float farDist, bool forSinglePass);
    void UpdateRig(Scene* scene, Node* head, Node* leftEye, Node* rightEye, float nearDist, float farDist, bool forSinglePass);
    void UpdateHands(Scene* scene, Node* rigRoot, Node* leftHand, Node* rightHand);

    SharedPtr<Texture2D> GetLeftEyeTexture() const { return useSingleTexture_ ? sharedTexture_ : leftTexture_; }
    SharedPtr<Texture2D> GetRightEyeTexture() const { return useSingleTexture_ ? sharedTexture_ : rightTexture_; }

    /// Renders the eye-masks to depth 0 (-1 in GL) so depth-test discards pixels. Also clears the render-targets in question.
    virtual void DrawEyeMask() override;

    /// Called internally during initialization. Call again whenever changing render-scale, single-texture, or MSAA.
    virtual void CreateEyeTextures() override;

    /// Sets the current action set.
    virtual void SetCurrentActionSet(SharedPtr<XRActionGroup>) override;
    /// Sets the current action set by name.
    virtual void SetCurrentActionSet(const String& setName) override;

private:
    Matrix4 ToUrho(const vr::HmdMatrix44_t&) const;
    Matrix4 ToUrho(const vr::HmdMatrix34_t&) const;
    Vector3 ToUrho(const vr::HmdVector3_t& v) const;
    virtual Matrix4 GetProjection(VREye eye, float near, float far) const override;

    void LoadHiddenAreaMesh();
    void LoadRenderModels();
    void ParseManifestFile(const String& manifestFile);
    void UpdateBindingValues();
    void CheckBindingState();

    vr::IVRSystem* vrSystem_ = nullptr;
    bool sessionLive_ = false;

    float renderScale_ = 1.0f;
    float ipdCorrection_ = 0.0f;
    float heightCorrection_ = 0.0f;
    float scaleCorrection_ = 1.0f;
    uint32_t trueEyeWidth_ = 0, trueEyeHeight_ = 0;
    uint32_t eyeWidth_, eyeHeight_;

    SharedPtr<Texture2D> leftTexture_, rightTexture_, sharedTexture_, leftDS_, rightDS_, sharedDS_;
    SharedPtr<Geometry> hiddenAreaMesh_[2];

    vr::TrackedDevicePose_t poses_[vr::k_unMaxTrackedDeviceCount];
    // The only reliable haptics we'll have.
    vr::VRActionHandle_t hapticHandles_[2] = { vr::k_ulInvalidActionHandle, vr::k_ulInvalidActionHandle };
    
    vr::VRInputValueHandle_t headInputHandle_ = 0;
    vr::VRInputValueHandle_t handInputHandles_[2] = { 0, 0 };

    struct ControlMesh {
        String name_;
        vr::RenderModel_t* model_ = nullptr;
        vr::RenderModel_TextureMap_t* colorTex_ = nullptr;
        
        SharedPtr<Geometry> geometry_;
        SharedPtr<Texture2D> texture_;
        BoundingBox bounds_;

        void Free();
    } wandMeshes_[2];

    class SteamVRBinding : public XRBinding
    {
        URHO3D_OBJECT(SteamVRBinding, XRBinding);
    public:
        SteamVRBinding(Context*);
        virtual ~SteamVRBinding();

        vr::VRInputValueHandle_t handHandle_ = 0;
        vr::VRActionHandle_t handle_ = vr::k_ulInvalidActionHandle;
    };

    class SteamVRActionSet : public XRActionGroup
    {
        URHO3D_OBJECT(SteamVRActionSet, XRActionGroup);
    public:
        SteamVRActionSet(Context*);
        virtual ~SteamVRActionSet();

        vr::VRActionSetHandle_t actionSet_;
    };
};
    
}
