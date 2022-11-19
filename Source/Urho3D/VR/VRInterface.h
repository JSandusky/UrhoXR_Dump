#pragma once

#include <Urho3D/Math/BoundingBox.h>
#include <Urho3D/Graphics/Geometry.h>
#include <Urho3D/Core/Object.h>
#include <Urho3D/Math/Ray.h>
#include <Urho3D/Graphics/Texture2D.h>

namespace Urho3D
{

    class Scene;
    class Node;

    enum VRRuntime
    {
        VR_OPENVR,
        VR_OPENXR,
        VR_OCULUS,       // reserved, not implemented
        VR_OCULUS_MOBILE // reserved, not implemented
    };

    enum VRHand
    {
        VR_HAND_NONE = -1,
        VR_HAND_LEFT = 0,
        VR_HAND_RIGHT = 1
    };

    enum VREye
    {
        VR_EYE_NONE = -1,
        VR_EYE_LEFT = 0,
        VR_EYE_RIGHT = 1
    };

    // for the future
    enum VRRenderMode
    {
        VR_SINGLE_TEXTURE,  // 1 double size texture containing both eyes
        VR_LAYERED          // render-target array
    };

    struct URHO3D_API VRWindowedValue
    {
        Variant data_;
        float time_;
        float spanTime_;
    };

    /// Wraps an input binding. Subclassed as required by interface implementations.
    class URHO3D_API XRBinding : public Object
    {
        friend class SteamVR;
        friend class OpenXR;
        friend class VRInterface;

        URHO3D_OBJECT(XRBinding, Object);
    public:
        XRBinding(Context*);
        virtual ~XRBinding();

        String GetLocalizedName() const { return localizedName_; }
        void SetLocalizedName(const String& name) { localizedName_ = name; }

        bool IsChanged() const { return changed_; }
        bool IsActive() const { return active_; }
        bool IsHanded() const { return hand_ != VR_HAND_NONE; }
        VRHand Hand() const { return hand_; }

        bool GetBool(float pressThreshold) const { return storedData_.GetFloatSafe() > pressThreshold; }
        bool GetBool() const { return storedData_.GetBool(); }
        float GetFloat() const { return storedData_.GetFloatSafe(); }
        Vector2 GetVec2() const { return storedData_.GetVector2(); }
        Vector3 GetVec3() const { return storedData_.GetVector3(); }
        Vector3 GetPos() const { return storedData_.GetMatrix3x4().Translation(); }
        Quaternion GetRot() const { return storedData_.GetMatrix3x4().Rotation(); }
        Matrix3x4 GetTransform() const { return storedData_.GetMatrix3x4(); }

        Variant GetData() const { return storedData_; }
        Variant GetDelta() const { return delta_; }
        Variant GetExtraDelta() const { return extraDelta_; }

        bool IsBound() const { return isBound_; }

        /// Returns the windowed data, last item will always be current regardless of thresholds.
        Vector<VRWindowedValue>& GetWindowedData() { return windowedData_; }
        /// Returns the windowed data, last item will always be current, regardless of thresholds.
        const Vector<VRWindowedValue>& GetWindowedData() const { return windowedData_; }

        /// Action results can be windowed so that time-series of events can be scanned.
        unsigned GetWindowSize() const { return windowSize_; }
        /// Configures how large the window will be, erasing front elements if becoming smaller.
        void SetWindowSize(unsigned sz);
        /// Returns the length threshold for window updates.
        float GetWindowDeltaThreshold() const { return windowDeltaThreshold_; }
        /// Sets the length threshold for window updates.
        void SetWindowDeltaThreshold(float v) { windowDeltaThreshold_ = v; }
        /// Push time-stamped data into the window.
        void PushWindow(Variant v, float time);

        /// Returns the time gap threshold for window updates.
        float GetWindowTiming() const { return windowTiming_; }
        /// Sets the time gap threshold for window updates, this allows limiting the density.
        void SetWindowTiming(float t) { windowTiming_ = t; }

        virtual void Vibrate(float duration, float frequency, float amplitude) { }

        bool IsInput() const { return haptic_ == false; }
        bool IsHaptic() const { return haptic_; }

    protected:
        String localizedName_;
        String path_;
        VRHand hand_ = VR_HAND_NONE;
        VariantType dataType_;
        bool changed_ = false;
        bool active_ = false;
        bool haptic_ = false;
        bool isBound_ = false;
        bool isPose_ = false;
        bool isAimPose_ = false;
        Variant storedData_;
        Variant extraData_[2];
        Variant delta_;
        Variant extraDelta_[2];

        Vector<VRWindowedValue> windowedData_;
        unsigned windowSize_= 0;
        float windowDeltaThreshold_ = 0.0f;
        float windowTiming_ = 0.0f;
    };

    /// Represents a logical action set in the underlying APIs.
    class URHO3D_API XRActionGroup : public Object
    {
        friend class SteamVR;
        friend class OpenXR;
        friend class VRInterface;

        URHO3D_OBJECT(XRActionGroup, Object);
    public:
        XRActionGroup(Context* ctx) : Object(ctx) { }
        virtual ~XRActionGroup() { }

        /// Return action name.
        const String& GetName() const { return name_; }
        /// Return localized action name.
        const String& GetLocalizedName() const { return localizedName_; }

        /// Return the list of contained bindings, modifiable.
        Vector<SharedPtr<XRBinding>>& GetBindings() { return bindings_; }
        /// Return the list of contained bindings, non-modifiable.
        const Vector<SharedPtr<XRBinding>>& GetBindings() const { return bindings_; }

    protected:
        /// Identifier of this action set.
        String name_;
        /// Localized identifier.
        String localizedName_;
        /// Contained action bindings.
        Vector<SharedPtr<XRBinding>> bindings_;
        /// Are we the default action-set?
        bool isDefault_;
    };

    /** %VRInterface component
    *   Base interface for a VR related subsystem. This is not expected to be utilized for mobile AR, it would be best to implement something else for that purpose.
    *
    *   TODO:
    *       Rig handling, should it anchor to the head in XZ each update?
    */
    class URHO3D_API VRInterface : public Object
    {
        URHO3D_OBJECT(VRInterface, Object);
    public:
        /// Construct.
        VRInterface(Context*);
        /// Destruct.
        virtual ~VRInterface();

        /// Returns true if this VR configuration is running at room scale.
        bool IsRoomScale() const { return isRoomScale_; }

        /// IPD correction factor in meters, value is minimal. Assume you can use this for UP TO an extra 0.2mm adjustment to perfect alignment beyond what physical adjusters will deal with. Will not work much beyond that because of projection matrices, it will distort.
        float GetIPDCorrection() const { return ipdCorrection_; }
        /// Height correction factor, added into vertical positioning.
        float GetHeightCorrection() const { return heightCorrection_; }
        /// Scale correction factor, premultiplied into all transforms.
        float GetScaleCorrection() const { return scaleCorrection_; }

        void SetIPDCorrection(float value) { ipdCorrection_ = value; }
        /// Height correction can also be done on the VRRig node.
        void SetHeightCorrection(float value) { heightCorrection_ = value; }
        /// Scale correction can also be done on the VRRig node.
        void SetScaleCorrection(float value) { scaleCorrection_ = value; }

        /// Returns the currently chosen MSAA level.
        int GetMSAALevel() const { return msaaLevel_; }
        /// Change MSAA level, have to call CreateEyeTextures() to update.
        void SetMSAALevel(int level) {
            if (msaaLevel_ != level)
            {
                msaaLevel_ = Clamp(level, 1, 16);
                CreateEyeTextures();
            }
        }

        /// Can use render-scale to resize targets if FPS is too low.
        float GetRenderScale() const { return renderTargetScale_; }
        /// Sets the scale factor for the render-targets, have to call CreateEyeTextures() to update.
        virtual void SetRenderScale(float value) {
            if (value != renderTargetScale_)
            {
                renderTargetScale_ = Clamp(renderTargetScale_, 0.25f, 2.0f);
                if (trueEyeTexWidth_ > 0)
                {
                    eyeTexWidth_ = trueEyeTexWidth_ * renderTargetScale_;
                    eyeTexHeight_ = trueEyeTexHeight_ * renderTargetScale_;
                }
                CreateEyeTextures();
            }
        }

        /// Returns whether we're rendering to 1 double-wide texture or 2 independent eye textures.
        bool IsSingleTexture() const { return useSingleTexture_; }
        /// Set to use a single texture.
        virtual void SetSingleTexture(bool state) { useSingleTexture_ = state; }

        /// Renders the eye-masks to depth 0 (-1 in GL) so depth-test discards pixels. Also clears the render-targets in question. So the renderpath must not clear.
        bool IsAutoDrawEyeMasks() const { return autoClearMasks_; }
        /// Set whether to render depth-0 (-1 in GL) masks so depth-test discards pixels. if true the renderpath must not clear.
        void SetAutoDrawEyeMasks(bool state) { autoClearMasks_ = state; }

        /// Viewport rectangle for left eye, required for multipass single-RT.
        IntRect GetLeftEyeRect() const { return IntRect(0, 0, eyeTexWidth_, eyeTexHeight_); }
        /// Viewport rectangle for right eye, required for multipass single-RT.
        IntRect GetRightEyeRect() const { return useSingleTexture_ ? IntRect(eyeTexWidth_, 0, eyeTexWidth_ * 2, eyeTexHeight_) : IntRect(0, 0, eyeTexWidth_, eyeTexHeight_); }

        /// Return the classification of VR runtime being used, 
        virtual VRRuntime GetRuntime() const = 0;
        /// Return a string name for the runtime, spaces are not allowed as this will be passed along to shaders.
        virtual const char* GetRuntimeName() const = 0;

        /// Constructs the backing eye textures. Overridable for APIs that work with swapchains.
        virtual void CreateEyeTextures();

        /// Constructs the head, eye, and hand nodes for a rig at a given node. The rig is effectively the worldspace locator. This rig is considered standard.
        void PrepareRig(Node* vrRig);
        /// Updates the rig calling the overloaded version by using the standardized rig constructed by PrepareRig.
        void UpdateRig(Node* vrRig, float nearDist, float farDist, bool forSinglePass);
        /// Detailed rig update with explicit parameters for non-standardized rig setup.
        void UpdateRig(Scene* scene, Node* head, Node* leftEye, Node* rightEye, float nearDist, float farDist, bool forSinglePass);
        
        /// Called by ConfigureRig to setup hands. Responsible for models and transforms
        virtual void UpdateHands(Scene* scene, Node* rigRoot, Node* leftHand, Node* rightHand) = 0; 

        /// Initializes the VR system providing a manifest.
        virtual bool Initialize(const String& manifestPath) = 0;
        /// Shutsdown the VR system.
        virtual void Shutdown() = 0;

        /// Activates a haptic for a given hand.
        virtual void TriggerHaptic(VRHand hand, float durationSeconds, float cyclesPerSec, float amplitude) = 0;

        /// Returns the transform for a given hand in head relative space.
        virtual Matrix3x4 GetHandTransform(VRHand) const = 0;
        /// Transform matrix of the hand aim base position.
        virtual Matrix3x4 GetHandAimTransform(VRHand) const = 0;
        /// Returns the aiming ray for a given hand.
        virtual Ray GetHandAimRay(VRHand) const = 0;
        /// Return linear and/or angular velocity of a hand.
        virtual void GetHandVelocity(VRHand hand, Vector3* linear, Vector3* angular) const = 0;
        /// Return the head transform in stage space (or local if no stage).
        virtual Matrix3x4 GetHeadTransform() const = 0;
        /// Return the head-relative eye transform.
        virtual Matrix3x4 GetEyeLocalTransform(VREye eye) const = 0;
        /// Return the projection matrix for an eye.
        virtual Matrix4 GetProjection(VREye eye, float nearDist, float farDist) const = 0;

        /// Draws the hidden area mask.
        virtual void DrawEyeMask();
        virtual void DrawRadialMask(const char* shader, const char* defines);

        /// Returns true if our VR system is alive, and actively rendering.
        virtual bool IsLive() const = 0;
        /// Returns true if our VR system is alive, but not actively rendering/updating (is paused).
        virtual bool IsRunning() const = 0;

        /// Attempts to retrieve an input binding.
        SharedPtr<XRBinding> GetInputBinding(const String& path);
        /// Attempts to retrieve a hand specific input binding.
        SharedPtr<XRBinding> GetInputBinding(const String& path, VRHand forHand);
        /// Returns the currently bound action set, null if no action set.
        SharedPtr<XRActionGroup> GetCurrentActionSet() const { return activeActionSet_; }
        /// Sets the current action set by name.
        virtual void SetCurrentActionSet(const String& setName);
        /// INTERFACE: Sets the current action set.
        virtual void SetCurrentActionSet(SharedPtr<XRActionGroup>) = 0;

        /// Returns the side-by-side color texture.
        SharedPtr<Texture2D> GetSharedTexture() const { return sharedTexture_; }
        /// Returns the side-by-side depth texture.
        SharedPtr<Texture2D> GetSharedDepth() const { return sharedDS_; }

        /// Returns the system name, ie. Windows Mixed Reality.
        String GetSystemName() const { return systemName_; }

    protected:
        /// Name of the system being run, ie. Windows Mixed Reality
        String systemName_;
        /// MSAA level to use, 4 is generally what is recommended.
        int msaaLevel_ = 4;
        /// Texture width API recommended.
        int trueEyeTexWidth_ = 0;
        /// Texture height API recommended.
        int trueEyeTexHeight_ = 0;
        /// Texture width after scaling.
        int eyeTexWidth_;
        /// Texture height after scaling.
        int eyeTexHeight_;
        /// External IPD adjustment.
        float ipdCorrection_ = 0.0f;
        /// Vertical correction factor.
        float heightCorrection_ = 0.0f;
        /// Scaling factor correct by.
        float scaleCorrection_ = 1.0f;
        /// Scaling factor to apply to recommended render-target resolutions. Such as going lower res or higher res.
        float renderTargetScale_ = 1.0f;
        /// Whether to automatically invoke the hidden area masks, if on then renderpath must not clear (or not clear depth at least)
        bool autoClearMasks_ = true;
        bool useSingleTexture_ = true;
        bool isRoomScale_ = false;

        SharedPtr<Texture2D> leftTexture_, rightTexture_, sharedTexture_, leftDS_, rightDS_, sharedDS_;
        /// Hidden area mesh.
        SharedPtr<Geometry> hiddenAreaMesh_[2];
        /// Visible area mesh.
        SharedPtr<Geometry> visibleAreaMesh_[2];
        /// Radial area mesh. Setup with 1.0 alpha at the edges, and 0.0 at the center can be used for edge darkening / glows / etc.
        SharedPtr<Geometry> radialAreaMesh_[2];
        /// Currently bound action-set.
        SharedPtr<XRActionGroup> activeActionSet_;
        /// Table of action sets registered.
        HashMap<String, SharedPtr<XRActionGroup> > actionSets_;

        struct ControlMesh
        {
            SharedPtr<Geometry> geometry_;
            SharedPtr<Texture2D> colorTex_;
            Urho3D::BoundingBox bounds_;
        };

    };

    void RegisterVR(Context*);
}
