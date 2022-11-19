#pragma once

namespace Urho3D
{

enum VR_HAND_STATE
{
    VR_HAND_
};

class URHO3D_API VRRichHands : LogicComponent
{
    URHO3D_OBJECT(VRRichHands, LogicComponent);
public:
    VRRichHand(Context*);
    virtual ~VRRichHand();
    
    virtual void Update(float td) override;

protected:
    struct HandState {
        WeakPtr<Node> grabbedNode_;
        bool active_ = true;
    };
    
    HandState handStates_[2];
}
    
}
