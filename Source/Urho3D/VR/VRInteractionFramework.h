#pragma once

enum VRInteractHighlight
{
    
};

namespace Urho3D
{
    
class VRInteractionWorld;
class VRInteractionWorld::Cell;
    
class URHO3D_API VRInteractable : public Component
{
    URHO3D_OBJECT(VRInteractable, Component);
public:
    VRInteractable();
    virtual ~VRInteractable();
protected:
    void AddToWorld();
    void RemoveFromWorld();

    VRInteractionWorld::Cell* cell_;
    WeakPtr<VRInteractionWorld> world_;
};

/// The associated Bullet rigid-body can be interacted with, if constrained it is treated as a fixed world object.
/// If not constrained it is treated as something grabbable.
class URHO3D_API VRInteractablePhysics : public VRInteractable
{
    URHO3D_OBJECT(VRInteractablePhysics, VRInteractable);
public:
private:
    /// Starting point on which we'll bias grasp action to.
    Vector3 localGrabPointStart_;
    /// Ending point on which we'll bias grasp action to.
    Vector3 localGrabPointEnd_;
    /// Axis facing away from the grab point.
    Vector3 localGrabNormal_;
    /// Axis of grab-point behaviour that represets the direction of the hand with the thumb "UP", specify for left hand and right hand will automatically use the reverse.
    Vector3 localGrabHandAxis_;
    /// Specifies to use a designated location for grasping.
    bool useGrabPoint_;
};

/// %VRinteractableUI
class URHO3D_API VRInteractableUI : public VRInteractable
{
public:
};

/// Fake wheel interaction object. Useable for wheels, bandages, tapes, etc.
class URHO3D_API VRInteractableWheel : public VRInteractable
{
public:
};

/// Fake lever interaction object. Useable for doors, levers, large-switches, etc.
class URHO3D_API VRInteractableLever : public VRInteractable
{
public:
};

/// Fake button interaction object.
class URHO3D_API VRInteractableButton : public VRInteractable
{
public:
};

/// Fake slider interaction object. Use for drawers.
class URHO3D_API VRInteractableSlider : public VRInteractable
{
public:
};

/// Grabbable thing, can be a line (with radius) or a sphere
class URHO3D_API VRInteractableGrab : public VRInteractable
{
public:

protected:
    /// Special identifier that will be reported with events.
    unsigned tag_ = { 0 };
    /// Starting point in local space.
    Vector3 localStart_;
    /// Ending point in local space.
    Vector3 localEnd_;
    /// Radius of the line segment or point.
    float radius_;
    /// Axis along which to check for "tug".
    Vector3 localTugAxis_;
    /// Distance of delta after which a "tug" event will be sent.
    float tugThreshold_;
    /// Interactable cannot be parented to the hand.
    bool fixedInPlace_;
    /// Grabbing this.
    bool initiateGrabMove_;
    /// Can grab with both hands.
    bool allowTwoHand_;
};

/// %VRInteractableFrog is a belt-frog like object that is used to store another object within in.
class URHO3D_API VRInteractableFrog : public VRInteractable
{
public:
protected:
};

class URHO3D_API VRInteractionWorld
{
public:

protected:
    friend class VRInteractable;

    struct Cell {
        /// Test intersection with an AABB.
        bool Test(const BoundingBox&) const;
        /// Test intersection with a sphere.
        bool Test(const Sphere&) const;
        /// Test intersection with a cone.
        bool Test(const Vector3& start, const Vector3& dir, float containDot) const;
        
        std::vector< WeakPtr<VRinteractable> > interactables_;
        Cell* children_[8] = { };
        Cell* parent_ = { nullptr };
    };
};

}
