#pragma once

#include <ThirdParty/OpenXRSDK/include/openxr/openxr.h>

#include <Urho3D/VR/VRInterface.h>
#include "../Graphics/Model.h"

namespace Urho3D
{
    class AnimatedModel;
    class XMLFile;

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

        Ideally, just avoid multiplayer altogether or do fake multiplayer like a mobile game.

To-Do:
    - Trackers
    - Multiple Action-Sets
    - Hand Skeleton

*/
    class URHO3D_API OpenXR : public VRInterface
    {
        URHO3D_OBJECT(OpenXR, VRInterface);
    public:
        OpenXR(Context*);
        virtual ~OpenXR();

        virtual VRRuntime GetRuntime() const override { return VR_OPENXR; }
        virtual const char* GetRuntimeName() const override { return "OPEN_XR"; }

        virtual void QueryExtensions();

        virtual bool Initialize(const String& manifestPath) override;
        virtual void Shutdown() override;

        // XR is currently single-texture only.
        virtual void SetSingleTexture(bool state) override { }

        virtual bool IsLive() const { return sessionLive_; }
        virtual bool IsRunning() const override { return sessionLive_; }

        virtual void TriggerHaptic(VRHand hand, float durationSeconds, float cyclesPerSec, float amplitude) override;

        virtual Matrix3x4 GetHandTransform(VRHand) const override;
        virtual Matrix3x4 GetHandAimTransform(VRHand hand) const override;
        virtual Ray GetHandAimRay(VRHand) const override;
        virtual void GetHandVelocity(VRHand hand, Vector3* linear, Vector3* angular) const override;
        virtual Matrix3x4 GetEyeLocalTransform(VREye eye) const override;
        virtual Matrix4 GetProjection(VREye eye, float nearDist, float farDist) const override;
        virtual Matrix3x4 GetHeadTransform() const override;

        virtual void UpdateHands(Scene* scene, Node* rigRoot, Node* leftHand, Node* rightHand) override;

        virtual void CreateEyeTextures() override;

        void HandlePreUpdate(StringHash, VariantMap&);
        void HandlePreRender(StringHash, VariantMap&);
        void HandlePostRender(StringHash, VariantMap&);

        virtual void BindActions(SharedPtr<XMLFile>);
        /// Sets the current action set.
        virtual void SetCurrentActionSet(SharedPtr<XRActionGroup>) override;

        SharedPtr<Node> GetControllerModel(VRHand hand) { return wandModels_[hand].model_ ? wandModels_[hand].model_ : nullptr; }
        void UpdateControllerModel(VRHand hand, SharedPtr<Node>);

        const StringVector GetExtensions() const { return extensions_; }
        void SetExtraExtensions(const StringVector& ext) { extraExtensions_ = ext; }

    protected:
        bool OpenSession();
        bool CreateSwapchain();
        void CloseSession();
        void DestroySwapchain();
        void UpdateBindings(float time);
        void UpdateBindingBound();
        void GetHiddenAreaMask();
        void LoadControllerModels();

        SharedPtr<XMLFile> manifest_ = { };
        XrInstance instance_ = { };
        XrSystemId system_ = { };
        XrSession session_ = { };
        XrSwapchain swapChain_ = { };
        XrView views_[2] = { { XR_TYPE_VIEW }, {XR_TYPE_VIEW} };

        // OXR headers are a complete mess, getting platform specific object defs creates a mess of platform includes, including "WIN32_EXTRA_FAT"
        struct Opaque;
        UniquePtr<Opaque> opaque_;

        SharedPtr<Texture2D> eyeColorTextures_[4] = { {}, {} };
        unsigned imgCount_ = { };

        // Pointless head-space.
        XrSpace headSpace_ = { };   
        XrSpace viewSpace_ = { };
        XrSpaceLocation headLoc_ = { XR_TYPE_SPACE_LOCATION };
        XrSpaceVelocity headVel_ = { XR_TYPE_SPACE_VELOCITY };

        XrEnvironmentBlendMode blendMode_ = { };
        ///
        XrTime predictedTime_ = { };
        /// Whether the session is currently active or not.
        bool sessionLive_ = false;
        /// Indicates whether visibility mask is supported.
        bool supportsMask_ = false;
        /// Indicates that controller model is supported.
        bool supportsControllerModel_ = false;

        struct ControllerModel {
            XrControllerModelKeyMSFT modelKey_ = 0;
            SharedPtr<Node> model_;
            XrControllerModelNodePropertiesMSFT properties_[256];
            unsigned numProperties_ = 0;
        };

        ControllerModel wandModels_[2];

        class XRActionBinding : public XRBinding
        {
        public:
            XRActionBinding(Context* ctx, OpenXR* xr) : XRBinding(ctx), xr_(xr) { }
            virtual ~XRActionBinding();

            /// If haptic this will trigger a vibration.
            virtual void Vibrate(float duration, float freq, float amplitude) override;

            /// Reference to owning OpenXR instance.
            OpenXR* xr_;
            /// Action itself, possibly shared in the case of sub-path handed actions.
            XrAction action_ = { };
            /// Owning actionset that contains this action.
            XrActionSet set_ = { };
            /// Indicates handed-ness for the OXR query.
            XrPath subPath_ = XR_NULL_PATH;
            /// If we're a space action we'll have an action space.
            XrSpace actionSpace_ = { };

            /// Position and orientation from space location.
            XrSpaceLocation location_ = { XR_TYPE_SPACE_LOCATION };
            /// Linear and Angular velocity from space location.
            XrSpaceVelocity velocity_ = { XR_TYPE_SPACE_VELOCITY };
            /// only 1 of the subpath handlers will do deletion, this indicates who will do it.
            bool responsibleForDelete_ = true;
        };

        class XRActionSet : public XRActionGroup
        {
            URHO3D_OBJECT(XRActionSet, XRActionGroup)
        public:
            XRActionSet(Context* ctx) : XRActionGroup(ctx) { }
            virtual ~XRActionSet();

            XrActionSet actionSet_ = { };
        };

        /// Cached grip pose bindings to avoid constant queries.
        SharedPtr<XRActionBinding> handGrips_[2];
        /// Cached aim pose bindings to avoid constant queries.
        SharedPtr<XRActionBinding> handAims_[2];
        /// Cached haptic outputs to avoid constant queries.
        SharedPtr<XRActionBinding> handHaptics_[2];

        /// List of extensions reported.
        StringVector extensions_;
        /// List of additional extensions you want.
        StringVector extraExtensions_;
    };

}
