#pragma once

#include "../Graphics/CustomGeometry.h"
#include "../Scene/LogicComponent.h"
#include "../Math/Ray.h"
#include "../Core/Spline.h"
#include "../VR/VRInterface.h"
#include "../VR/VRUtils.h"

class btCollisionShape;

namespace Urho3D
{
    class CollisionShape;
    class CustomGeometry;
    class PhysicsWorld;
    class PhysicsRaycastResult;

    enum VRRigLocomotionMode
    {
        VR_LOCOMOTE_HEAD_DIRECTION,
        VR_LOCOMOTE_HAND_DIRECTION,
        VR_LOCOMOTE_TELEPORT
    };
    
    enum VRRigTurningMode
    {
        VR_TURNING_SMOOTH,
        VR_TURNING_SNAP
    };

    enum VRBodyPhysicsState {
        VR_PHY_STATE_WALKING,           // Walking on the ground
        VR_PHY_STATE_FALLING,           // Has no contact, or at least no contact that qualifies as ground
        VR_PHY_STATE_CLIMBING,          // Engaged in climbing, UNIMPLEMENTED
        VR_PHY_STATE_FLYING,            // Classic shooter style flight
        VR_PHY_STATE_VELOCITY_DRIVEN,   // Will move according to given velocity
        VR_PHY_STATE_JUMPING,           // Will move according to given velocity, but shed Z velocity based on gravity
        VR_PHY_STATE_STAGE_LOCKED,      // Only permit stage space motion, ie. temp loading scenes, stage-phase locks
        VR_PHY_STATE_SWIM,              // Fluid volume swimming, UNIMPLEMENTED
        VR_PHY_STATE_SPLINE_FOLLOW      // Follow a specified spline, head motion is accepted but clipped. Used to animate stair steps, climb-ups, etc.
    };

    /** %VRRigWalker component.
    *   This component is used to coordinate the distinction between Teleport and Smooth locomotion.
    *   Because the form of locomotion is ideally best left to the end user to pick it's important that it deals with both for maximum 
    *   convenience and so teleportation properly works along-side stage-space motion.
    *
    *   Turning behaviour is configurable with SNAP and SMOOTH options both of which use different fields to control their turn rates.
    *   When in Teleport mode only 1 stick binding is required as UP is used for Teleport and left/right will be used for turning.
    *   Stage-space head motion is automatic, do not include head motion vectors in your calls as the component will take care of it.
    *
    *   The controller is aware of the "floor object" and will relocate itself according to transform changes of the floor object.
    *   This will keep it anchored to elevators, moving-platforms, etc no matter how quickly they move.
    *
    *   The model used by the controller is similar to the classic Unreal '99 model. Should the Dot-product of a new contact with the old
    *   contact be <= 0 then the cross will be taken. Motion delta is better preserved this way than repeated historic plane-projections
    *   of prior contacts per step used in the quake model and 90 degree corners are less prone to becoming catch spots. Uses a 
    *   Quake-like 1.001 back off value to bias towards what would be a dot-prod == 1 result, so *= 1.001 if > 0 and /= 1.001 if < 0.
    *
    *   Attach to the Stage node. The hierarchy will then become: (! on items it will create)
    *       Stage - Node
    *           VRRigWalker - Component
    *           ! CustomGeometry - Component, used for teleport ray geometry
    *           Head - Node
    *               LeftEye - Node
    *               RightEye - Node
    *               ! VRBody - Node, this node is created in order to cancel out head rotation so the RigidBody stays upright
    *                   ! RigidBody - Component, Kinematic body
    *                   ! CollisionShape - Component, variably sized capsule
    *           LeftHand - Node
    *           RightHand - Node
    *   Events:
    *       On contact with a blocking surface that cannot be stepped over sends E_VRHITWALL, use to check for bumping stuff
    *       On contact with a floor after falling sends E_VRLANDED
    *       Once per frame while falling sends E_VRFALLING
    *       On update sends E_VRPUSH event containing a list of each encroaching kinematic body
    *       Sends an event E_VRPHYSICSCHANGE each time the physics state changes
    *
    *   Problems raised:
    *       Components need some sort of tag identifier and GetOrCreate based on tag, there are other reasons
    *           one would want to attach additional custom geometry nodes to the stage
    *
    *   %VRRichHands will interact with this component to report climbing state and optionally drive gesture motions
    *       such as using hand motion to swim.
    *
    *   TODO:
    *       Smooth stepping and smooth floor-anchoring (step-down)
    *       Animated teleportation and snap turn motion
    *       Identify cause of occasional teleport ray geometry flickering, possibly an illusional effect of perception of arc segment changes
    *           Possibly due to alpha handling of fades
    *           Add delta check to stop generating every single frame, issue seems to be one of motion though so delta check probably won't fix
    *       Implement jumping
    *       Implement climbing helpers
    *       Implement swimming
    *       Implement velocity drive
    *       Back-dash option for teleport mode, stick backwards will attempt to move backwards from head direction
    *       Local calibration
    */
    class URHO3D_API VRRigWalker : public LogicComponent
    {
        URHO3D_OBJECT(VRRigWalker, LogicComponent);
    public:
        /// Construct.
        VRRigWalker(Context*);
        /// Destruct.
        virtual ~VRRigWalker();

        /// Register factory and attributes.
        static void Register(Context*);

        /// Per-frame update handling head motion, controls, teleport, etc.
        virtual void Update(float) override;

        /// Attempts to move along the given delta. Optionally apply gravity, you don't want to when VR_PHYS_FLYING|VR_PHYS_SWIMMING|VR_PHYS_STAGE_LOCKED
        void PhysicsMove(Vector3 delta, float dt, bool applyGravity);

        /// Handles teleportation behaviour. In the case of an attempt to teleport to a bad location it will walk back towards the aim-vector several steps to try to satisfy the spacial conditions. Optionally render diagnostics to a %DebugRenderer.
        void Teleport(VRHand hand, float dt, bool commit, DebugRenderer* debugRen = nullptr);

        /// Sets the material used to render the teleport ray.
        void SetRayMaterial(Material*);
        /// Gets the material used to render the teleport ray.
        Material* GetRayMaterial() const;

        /// Set XR input binding for smooth locomotion stick, also used for turning in Teleport mode.
        void SetMoveBinding(XRBinding* bind) { moveBinding_ = bind; }
        /// Set XR input binding for turn locomotion, both smooth and snap.
        void SetTurnBinding(XRBinding* bind) { turnBinding_ = bind; }

        /// Set color for the beam to use when teleport destination is valid.
        void SetValidTeleportArcColor(Color c) { teleportArcColor_ = c; }
        /// Set color for the beam to use when teleport destination is NOT valid.
        void SetInvalidTeleportArcColor(Color c) { invalidTeleportArcColor_ = c; }
        
        /// Set color for the teleport destination beacon when valid.
        void SetValidDestinationColor(Color c) { validDestinationColor_ = c; }
        /// Set color for the teleport destination beacon when NOT valid.
        void SetInvalidDestinationColor(Color c) { invalidDestinationColor_ = c; }

        /// Specify which hand indicates direction when using VR_LOCOMOTE_HAND_DIRECTION mode.
        void SetLocomoteHand(VRHand hand) { moveHand_ = hand; }
        /// Identify which hand is used for hand based locomotion.
        VRHand GetLocomoteHand() const { return moveHand_; }

        /// Sets the mode of linear locomotion to use.
        void SetLocomotionMode(VRRigLocomotionMode mode) { moveMode_ = mode; turnLeftCommand_.Reset(); turnRightCommand_.Reset(); }
        /// Returns the mode of linear locomotion currently used.
        VRRigLocomotionMode GetLocomotionMode() const { return moveMode_; }
        /// Sets the mode of angular change to use.
        void SetTurnMode(VRRigTurningMode mode) { turnMode_ = mode; turnLeftCommand_.Reset(); turnRightCommand_.Reset(); }
        /// Returns the mode of angular chagne currently used.
        VRRigTurningMode GetTurningMode() const { return turnMode_; }

        /// Set the dot-product result value that indicates a surface is traversaable. Dot(A,B) > upDotProduct_ == WALKABLE|CEILING.
        void SetUpDotProduct(float dot) { upDotProduct_ = dot; }
        /// Return the dot-product value that indicates an upward facing surface, Dot(A,B) < upDotProduct_ == WALL.
        float GetUpDotProduct() const { return upDotProduct_; }

        /// Set deadzone threshold for sticks.
        void SetStickThreshold(float t) { stickThreshold_ = t; }
        /// Return deadzone threshold.
        float GetStickThreshold() const { return stickThreshold_; }

        /// Sets how many meters high to vertically offset the teleport ring.
        void SetTeleportRingOffset(float vert) { teleportRingOffset_ = vert; }
        /// Return how high off the destination to set the teleport ring.
        float GetTeleportRingOffset() const { return teleportRingOffset_; }

        /// Set how vertically thick the teleport ring is.
        void SetTeleportRingHeight(float height) { teleportRingHeight_ = height; }
        /// Get how vertically thick the teleport ring is.
        float GetTeleportRingHeight() { return teleportRingHeight_; }

        /// Sets the thickness of the teleport ring.
        void SetTeleportRingWidth(float width) { teleportRingWidth_ = width; }
        /// Gets the thickness of the teleport ring.
        float GetTeleportRingWidth() { return teleportRingWidth_; }

        /// Sets how wide (in meters) the teleport beam is.
        void SetTeleportRibbonWidth(float width) { teleportRibbonWidth_ = width; }
        /// Returns how wide the teleport beam is in meters.
        float GetTeleportRibbonWidth() { return teleportRibbonWidth_; }

        /// Sets alpha value at the start of the teleport beam.
        void SetStartAlphaFade(float fraction) { startAlphaFadeLength_ = fraction; }
        /// Returns alpha value at the start of the teleport beam.
        float GetStartAlphaFade() const { return startAlphaFadeLength_; }

        /// Sets alpha value at the end of the teleport beam.
        void SetEndAlphaFade(float fraction) { endAlphaFadeLength_ = fraction; }
        /// Returns alpha value at the end of the teleport beam.
        float GetEndAlphaFade() const { return endAlphaFadeLength_; }

        /// Sets the size variance of the teleport ring pulse.
        void SetPulseSize(float dist) { teleportRingPulseSize_ = dist; }
        /// Gets the size variance of the teleport ring pulse.
        float GetPulseSize() const { return teleportRingPulseSize_; }

        /// Sets how fast the teleport ring pulses.
        void SetPulseRate(float rate) { teleportRingPulseRate_ = rate; }
        /// Returns how fast the teleport ring pulses.
        float GetPulseRate() const { return teleportRingPulseRate_; }

        /// Set the magnitude of the teleport beam.
        void SetTeleportReachPower(float power) { teleportReachPower_ = power; }
        /// Return magnitude of teleport beam.
        float GetTeleportReachPower() const { return teleportReachPower_; }

        /// Sets collision mask that indicates what the ray-casts are allowed to contact, effectively indicates world geometry.
        void SetTeleportRayCollisionMask(unsigned mask) { teleportRayCollisionMask_ = mask; }
        /// Gets collisdion mask used for teleport rays.
        unsigned GetTeleportRayCollisionMask() const { return teleportRayCollisionMask_; }

        /// Sets collision mask used to query if there is sufficient clearance to teleport.
        void SetTeleportDestCollisionMask(unsigned mask) { teleportDestCollisionMask_ = mask; }
        /// Get collision mask for teleport locale.
        unsigned GetTeleportDestCollisionMask() const { return teleportDestCollisionMask_; }

        /// Set the movement speed in meters.
        void SetSpeed(float spd) { speed_ = spd; }
        /// Get the movement speed in meters.
        float GetSpeed() const { return speed_; }

        /// Set the radius of collion for the walker, in meters.
        void SetCollisionRadius(float rad) { collisionRadius_ = rad; }
        /// Return the radius of the collision for the walker, in meters.
        float GetCollisionRadius() const { return collisionRadius_; }

        /// Set how many degrees to turn each snap turn step.
        void SetSnapTurnAmount(float deg) { snapTurnAmount_ = deg; }
        /// Return degrees turned per snap turn.
        float GetSnapTurnAmount() const { return snapTurnAmount_; }

        /// Set degrees per second for smooth turn mode.
        void SetSmoothTurnRate(float deg) { smoothTurnRate_ = deg; }
        /// Return degrees per second to turn smoothly.
        float GetSmoothTurnRate() const { return smoothTurnRate_; }

        /// Set the height in meters that is allowed to step over.
        void SetStepHeight(float meters) { stepHeight_ = meters; }
        /// Return height to step over short obstacles in meters.
        float GetStepHeight() const { return stepHeight_; }

        /// Return how long the teleport ray has been displayed.
        float GetTeleportShowingTime() const { return teleportActiveTime_; }
        /// Return time in seconds spent falling.
        float GetFallingTime() const { return timeFalling_; }
    
        /// Fetch the current floor object. Useable for arbitrary identification, ie. the Floor is Lava.
        Node* GetFloorObject() const { return floorObject_.Lock(); }

        /// Returns the spline that we'll be tracking against.
        Spline GetFollowSpline() const { return followSpline_; }
        /// Set a spline to track along for the given duration.
        void SetFollowSpline(const Spline& s, float duration, VRBodyPhysicsState endState);

        static Spline CalculateStepSpline(Vector3 start, Vector3 end, bool stepUp);

    protected:
        /// We've done nothing, 0.
        const static int HANDLE_STEP_NO_CHANGE;
        /// We've mapped a spline because the step gap is too large.
        const static int HANDLE_STEP_SLOW_STEP;
        /// The stepping delta was small enough to just snap.
        const static int HANDLE_STEP_QUICK_STEP;

        /// Handles internal move, returns true if we hit anything and writes the result out.
        bool MoveInternal(Vector3 delta, PhysicsRaycastResult* outResult = nullptr);
        /** Deals with stepping up and down based on stepHeight_. Consequently, this also resolves slopes whose degree of acceptance is the angle
        *   of the hypotenuse of the triangle formed from from Vec2(0, 0), Vec2(radius, 0), and Vec2(radius, stepHeight) should the contact
        *   pass the specified normal thresholds by default only checks one step level but can check multiple
        *   */
        int HandleStep(Vector3& pos, Vector3& delta, float stepHeight);
        /// Anchor ourselves to the floor if within step height.
        void AnchorToFloor(Vector3& pos, float stepHeight, bool checkPlatformMove);
        /// Recalculates collider.
        void UpdateCollider();
    
        /// Constructed geometry for the teleport ray.
        SharedPtr<CustomGeometry> teleportRay_;
        /// Collision shape.
        SharedPtr<CollisionShape> collider_;
        /// Must be a Vec2 input binding for movement, likely a stick (or touchpad on wand).
        SharedPtr<XRBinding> moveBinding_;
        /// Must be a Vec2 input binding for turning, likely a stick (or touchpad on wand).
        SharedPtr<XRBinding> turnBinding_;
        /// Optional input binding for grabbing. Not currently used. Intended to talk to an interactions framework for climbing/vaulting/etc.
        SharedPtr<XRBinding> grabBindingLeft_;
        /// Optional input binding for grabbing. Not currently used. Intended to talk to an interactions framework for climbing/vaulting/etc.
        SharedPtr<XRBinding> grabBindingRight_;
        /// Material used for teleport ray ray display, a default will be pulled if not specified.
        SharedPtr<Material> rayMaterial_;
        
        /// color to use for the teleporter arc.
        Color teleportArcColor_ = { Color::GREEN };
        /// color to use for the teleporter arc when invalid destination.
        Color invalidTeleportArcColor_ = { Color::RED };
        /// Color to use when the hit point is valid.
        Color validDestinationColor_ = { Color::GREEN };
        /// Color to use when the hit point is invalid.
        Color invalidDestinationColor_ = { Color::RED };
        /// Current aimray for teleporter.
        Ray aimRay_;
        /// Specified movement mode.
        VRRigLocomotionMode moveMode_ = { VR_LOCOMOTE_HEAD_DIRECTION };
        /// Specified turning mode.
        VRRigTurningMode turnMode_ = { VR_TURNING_SNAP};
        /// Which hand to use when in VR_LOCOMOTE_HAND_DIRECTION mode.
        VRHand moveHand_ = { VR_HAND_LEFT };
        /// A Dot(normal, Vector3::UP) GREATER than this will pass as a walkable surface. LESS than this means a wall.
        float upDotProduct_ = { 0.7f };
        /// Threshold to take off joystick inputs.
        float stickThreshold_ = { 0.3f };
        /// Angle in degrees to launch from aim pose, if zero it will be along the aim ray.
        float teleportationAngle_ = { 45.0f };
        /// Vertical offset for teleport ring.
        float teleportRingOffset_ = { 0.05f };
        /// How thick/tall the ring is, added onto the Y of the destination to get the height position.
        float teleportRingHeight_ = { 0.1f };
        /// Thickness of the ring, not to be confused with the radius of the ring. Outside of ring is collisionRadius_ + teleportRingWidth_ and inside is collisionRadius_ - teleportRingWidth_.
        float teleportRingWidth_ = { 0.1f };
        /// Width in meters of ribbon streak.
        float teleportRibbonWidth_ = { 0.05f /*6 inches*/};
        /// How much distance along the ribbon the alpha will fade.
        float startAlphaFadeLength_ = { 0.25f };
        /// How much distance along the end the ribbon will fade to alpha 0.0. Measured going backwards.
        float endAlphaFadeLength_ = { 0.25f };
        /// Maximum amount added/subtracted
        float teleportRingPulseSize_ = { 0.15f };
        /// Speed at which teleport ring will pulse.
        float teleportRingPulseRate_ = { 20.0f };
        /// How quickly the teleport will move, if zero then it'll be instant.
        float teleportSpeed_ = { 0.0f };
        /// How long it takes the teleport arc to fade in.
        float teleportFadeInTime_ = { 0.15f };
        /// How long it takes the teleport arc to fade out.
        float teleportFadeOutTime_ = { 0.0f };
        /// How powerful the reach of the teleport is. Bigger goes farther.
        float teleportReachPower_ = { 10.0f };
        /// Mask to use when casting rays.
        unsigned teleportRayCollisionMask_ = { UINT_MAX };
        /// Mask to use when verifying the ring.
        unsigned teleportDestCollisionMask_ = { UINT_MAX };
        /// Moving speed, in meters.
        float speed_ = { 0.05f };
        /// Also used for the teleport ring radius, in meters.
        float collisionRadius_ = { 0.33f };
        /// Pulled from the merged eye positions, in meters.
        float collisionHeight_;
        /// How many degrees does 1 snap turn take.
        float snapTurnAmount_ = { 45.0f };
        /// How many degrees per second to turn when turning smoothly.
        float smoothTurnRate_ = { 25.0f };
        /// Maximum clean step height.
        float stepHeight_ = { 1.0f };
        /// For distance smaller than this stepping will snap into position.
        float snapStepHeight_ = { 0.25f };
        /// Will be bitwise AND'ed collision hits and if NOT-ZERO means we'll reject. Use to specify objects that shouldn't be considered for walking on (like an enemy).
        unsigned denyWalkMask_ = { 0 };


        /// Is the target space valid
        bool teleportionTargetValid_ = { false };
        /// Have we moved the rig?
        bool isMoving_ = { false };
        /// What is the current motion profile to be used.
        VRBodyPhysicsState moveState_ = { VR_PHY_STATE_WALKING };
        /// Destination to teleport to.
        Vector3 teleportDestination_;
        /// Backup destination that's valid.
        Vector3 altTeleportDestination_ = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
        /// Indicates if teleport beacon destination is valid.
        bool destinationValid_ = { false };
        /// Used to animate teleport ring pulsing, reset each time teleport casting is activated so animation is consistent every time.        
        float teleportActiveTime_ = { 0.0f };

        /// Total seconds the character has been following.
        float timeFalling_ = { 0.0f };

        /// Input code state tracking for teleport.
        int lastInputCode_ = { 0 };

        /// Command helper for snap turn to only activate snap-turn with each round-trip of the stick.
        ButtonCommand turnLeftCommand_ = { 4 };
        /// Command helper for snap turn to only activate snap-turn with each round-trip of the stick.
        ButtonCommand turnRightCommand_ = { 2 };


        /// Object we're standing on, detected in AnchorToFloor.
        WeakPtr<Node> floorObject_;
        /// Last transform we saw from the floor object. Used in order to transform along with floor object.
        Matrix3x4 floorObjectPrevTransform_;
        /// Spline currently being followed, only used when in VR_PHYS_SPLINE_FOLLOW mode.
        Spline followSpline_;
        /// State to transition to when spline is complete.
        VRBodyPhysicsState splineExitState_;
        /// Current time in spline tracking, only used when in VR_PHYS_SPLINE_FOLLOW mode.
        float timeInSpline_ = 0.0f;
        /// Total duration in seconds of spline animation, only used when in VR_PHYS_SPLINE_FOLLOW mode.
        float totalSplineTime_ = 0.0f;
    };
    
}
