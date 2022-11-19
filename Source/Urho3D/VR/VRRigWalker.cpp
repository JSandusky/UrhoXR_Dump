#include "VRRigWalker.h"

#include "../Scene/Animatable.h"
#include "../Graphics/DebugRenderer.h"
#include "../Physics/CollisionShape.h"
#include "../Core/Context.h"
#include "../Graphics/Octree.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/RigidBody.h"
#include "../Scene/Scene.h"
#include "../Graphics/Material.h"
#include "../Resource/ResourceCache.h"
#include "../Scene/ValueAnimation.h"
#include "../VR/VREvents.h"
#include "../VR/VRUtils.h"
#include "../VR/XR.h"

#pragma optimize("", off)

#define INVALID_DEST Vector3(FLT_MAX, FLT_MAX, FLT_MAX)

namespace Urho3D
{

const int VRRigWalker::HANDLE_STEP_NO_CHANGE = 0;
const int VRRigWalker::HANDLE_STEP_QUICK_STEP = 1;
const int VRRigWalker::HANDLE_STEP_SLOW_STEP = 2;

bool ResultValid(PhysicsRaycastResult result)
{
    return result.distance_ != M_INFINITY;
}

void SortResults(PODVector<PhysicsRaycastResult>& results)
{
    Sort(results.Begin(), results.End(), [](const PhysicsRaycastResult& lhs, const PhysicsRaycastResult& rhs) {
        if (lhs.hitFraction_ < rhs.hitFraction_)
            return -1;
        else if (lhs.hitFraction_ > rhs.hitFraction_)
            return 1;
        return 0;
    });
}

void VelocityClip(Vector3& inOut, const Vector3& normal, Vector3* oldNormal)
{
    // project velocity onto the contact plane, then bias (1.001) a bit
    // for smoother sliding behaviour

    Vector3 dir = inOut.Normalized();

    // interior corner contact
    if (oldNormal != nullptr && normal.DotProduct(*oldNormal) <= 0.0f)
    {
        Vector3 correctedDir = normal.CrossProduct(*oldNormal);
        correctedDir.Normalize();

        float backoff = inOut.DotProduct(correctedDir);
        if (backoff < 0.0f)
            backoff *= 1.001f;
        else
            backoff /= 1.001f;
        inOut -= correctedDir * backoff;
    }
    else // simple contact
    {
        float backoff = inOut.DotProduct(normal);
        if (backoff < 0.0f)
            backoff *= 1.001f;
        else
            backoff /= 1.001f;
        inOut -= normal * backoff;
    }
}

VRRigWalker::VRRigWalker(Context* ctx) :
    LogicComponent(ctx)
{
}

VRRigWalker::~VRRigWalker()
{
    void** arrayPtr = new void*[256];
}

extern const char* LOGIC_CATEGORY;
void VRRigWalker::Register(Context* context)
{
    context->RegisterFactory<VRRigWalker>(LOGIC_CATEGORY);
}

void VRRigWalker::Update(float dt)
{
    if (!IsEnabled())
        return;

    // do not update the controller when XR is not live.
    if (!GetSubsystem<OpenXR>()->IsRunning())
        return;

    UpdateCollider();

    auto node = GetNode();
    auto head = GetNode()->GetChild("Head");

    auto curHeadPos = head->GetWorldPosition();

    // When stage locked we respect whatever we have.
    if (moveState_ == VR_PHY_STATE_STAGE_LOCKED)
        return;

    static const StringHash lastWS = "LastTransformWS";

    if (moveState_ == VR_PHY_STATE_SPLINE_FOLLOW)
    {
        timeInSpline_ += dt;
        float sampleTime = Min(timeInSpline_ / totalSplineTime_, 1.0f);
        
        auto pt = followSpline_.GetPoint(sampleTime).GetVector3();
        auto splineDelta = pt - curHeadPos;
        GetNode()->Translate(splineDelta, TS_WORLD);

        if (timeInSpline_ > totalSplineTime_)
            moveState_ = VR_PHY_STATE_WALKING;
        else
            return;
    }

    // if what we're standing on translates or rotate, we'll track with it
    if (floorObject_)
    {
        const auto floor = floorObject_.Lock();
        const auto newFloorTransform = floor->GetWorldTransform();
        const auto newFloorPos = newFloorTransform.Translation();
        const auto oldFloorPos = floorObjectPrevTransform_.Translation();

        const auto newFloorRot = newFloorTransform.Rotation();
        const auto oldFloorRot = floorObjectPrevTransform_.Rotation();

        if (oldFloorPos != newFloorPos || newFloorRot != oldFloorRot)
        {
            auto diffT = newFloorPos - oldFloorPos;
            auto diffR = oldFloorRot.Inverse() * newFloorRot;

            GetNode()->RotateAround(newFloorPos, diffR);

            // If we have translation then do as a physics move without gravity
            // This does mean there is no teleporting with the floorObject
            if (diffT.Length() > M_LARGE_EPSILON)
                PhysicsMove(diffT, dt, false);
        }

        // always update the old transform
        floorObjectPrevTransform_ = newFloorTransform;
    }

    // do stage space movement based on head
    {
        auto prev = head->GetVar(lastWS).GetMatrix3x4().Translation();

        auto delta = curHeadPos - prev;
        PhysicsMove(delta, dt, true); // we'll ignore gravity for this
    }

    if (moveMode_ == VR_LOCOMOTE_TELEPORT && moveBinding_ && moveState_ != VR_PHY_STATE_CLIMBING)
    {
        int code = JoystickAsDPad(moveBinding_, stickThreshold_);
        if (code == 1)
            Teleport(VR_HAND_LEFT, dt, false);
        else if (code == 0 && lastInputCode_ == 1)
            Teleport(VR_HAND_LEFT, dt, true);
        
        // When in teleport mode, use the left-stick for turn

        if (turnMode_ == VR_TURNING_SMOOTH)
        {
            if (turnLeftCommand_.CheckDown(code))
                node->RotateAround(Vector3(curHeadPos.x_, GetNode()->GetWorldPosition().y_, curHeadPos.z_), Quaternion(-smoothTurnRate_ * dt, Vector3::UP), TS_WORLD);
            if (turnRightCommand_.CheckDown(code))
                node->RotateAround(Vector3(curHeadPos.x_, GetNode()->GetWorldPosition().y_, curHeadPos.z_), Quaternion(smoothTurnRate_ * dt, Vector3::UP), TS_WORLD);
        }
        else // VR_TURNING_SNAP
        {
            if (turnLeftCommand_.CheckStrict(code))
                node->RotateAround(Vector3(curHeadPos.x_, GetNode()->GetWorldPosition().y_, curHeadPos.z_), Quaternion(-snapTurnAmount_, Vector3::UP), TS_WORLD);
            if (turnRightCommand_.CheckStrict(code))
                node->RotateAround(Vector3(curHeadPos.x_, GetNode()->GetWorldPosition().y_, curHeadPos.z_), Quaternion(snapTurnAmount_, Vector3::UP), TS_WORLD);
        }

        lastInputCode_ = code;
    }
    else if (moveState_ == VR_PHY_STATE_CLIMBING)
    {
        lastInputCode_ = 0;
        // this is really bloody complicated because have two possible moves
    }
    else
    {
        lastInputCode_ = 0;

        // don't do anything if we're falling, we dealt with that in head motion
        if (moveState_ == VR_PHY_STATE_FALLING)
            return;

        // process stick movement for walk
        if (moveBinding_)
        {
            auto stickDelta = moveBinding_->GetVec2();
            const float xSign = Sign(stickDelta.x_);
            const float ySign = Sign(stickDelta.y_);
            
            if (Abs(stickDelta.x_) < stickThreshold_)
                stickDelta.x_ = 0;
            else
                stickDelta.x_ = xSign * NORMALIZE(Abs(stickDelta.x_), stickThreshold_, 1.0f);
            
            if (Abs(stickDelta.y_) < stickThreshold_)
                stickDelta.y_ = 0;
            else
                stickDelta.y_ = ySign * NORMALIZE(Abs(stickDelta.y_), stickThreshold_, 1.0f);

            if (stickDelta.LengthSquared() > 0)
            {
                Vector3 fore;
                Vector3 right;

                if (moveMode_ == VR_LOCOMOTE_HEAD_DIRECTION)
                {
                    fore = head->GetWorldDirection();
                    right = head->GetWorldRight();
                }
                else
                {
                    if (auto handNode = moveHand_ == VR_HAND_LEFT ? node->GetChild("LeftHand", true) : node->GetChild("RightHand", true))
                    {
                        fore = handNode->GetWorldDirection();
                        right = handNode->GetWorldRight();
                    }
                }

                // zero out vertical as we're a stick, unless we're flying
                if (moveState_ != VR_PHY_STATE_FLYING)
                {
                    fore.y_ = 0;
                    right.y_ = 0;
                }
                fore.Normalize();
                right.Normalize();

                Vector3 moveVec = fore * stickDelta.y_ + right * stickDelta.x_;
                moveVec.Normalize(); // prevent diagonal over-acceleration
                moveVec *= speed_;
                PhysicsMove(moveVec, dt, moveState_ != VR_PHY_STATE_FLYING);
            }
        }

        // process turning
        if (turnBinding_)
        {
            int code = JoystickAsDPad(turnBinding_, stickThreshold_);
            static const StringHash ROTKEY = "Rotation";
            if (turnMode_ == VR_TURNING_SMOOTH)
            {
                if (turnLeftCommand_.CheckDown(code))
                    node->RotateAround(Vector3(curHeadPos.x_, GetNode()->GetWorldPosition().y_, curHeadPos.z_), Quaternion(-smoothTurnRate_ * dt, Vector3::UP), TS_WORLD);
                if (turnRightCommand_.CheckDown(code))
                    node->RotateAround(Vector3(curHeadPos.x_, GetNode()->GetWorldPosition().y_, curHeadPos.z_), Quaternion(smoothTurnRate_ * dt, Vector3::UP), TS_WORLD);
            }
            else // VR_TURNING_SNAP
            {
                if (turnLeftCommand_.CheckStrict(code))
                    node->RotateAround(Vector3(curHeadPos.x_, GetNode()->GetWorldPosition().y_, curHeadPos.z_), Quaternion(-snapTurnAmount_, Vector3::UP), TS_WORLD);
                if (turnRightCommand_.CheckStrict(code))
                    node->RotateAround(Vector3(curHeadPos.x_, GetNode()->GetWorldPosition().y_, curHeadPos.z_), Quaternion(snapTurnAmount_, Vector3::UP), TS_WORLD);
            }
        }
    }
}

void VRRigWalker::PhysicsMove(Vector3 delta, float dt, bool applyGravity)
{
    auto pos = GetNode()->GetChild("Head")->GetWorldPosition();
    auto oldPos = pos;
    PhysicsRaycastResult result;

    auto world = GetScene()->GetComponent<PhysicsWorld>();
    Vector3 g = world->GetGravity() * dt;

    // this will keep us anchored to vertical objects moving slower than stepheight
    //if (moveState_ == VR_PHY_STATE_WALKING)
    //    AnchorToFloor(pos, stepHeight_, true);

    if (moveState_ == VR_PHY_STATE_WALKING)
    {
        if (applyGravity)
            delta += g;

        bool didStep = false;
        PhysicsRaycastResult oldResult;
        oldResult.distance_ = M_INFINITY;

        for (int i = 0; i < 6; ++i)
        {
            if (delta.Length() < 0.0001f)
                break;

            oldResult = result;
            if (MoveInternal(delta, &result))
            {
                // check for step up / down
                if (Abs(result.normal_.y_) < upDotProduct_)
                {
                    // HandleStep will tell us if it dealt with the motion
                    const int stepResult = HandleStep(pos, delta, stepHeight_);
                    if (stepResult == HANDLE_STEP_NO_CHANGE)
                    {
                        // clip velocity so we slide along contacts
                        pos += delta * result.hitFraction_;
                        delta -= delta * result.hitFraction_;
                        VelocityClip(delta, result.normal_, ResultValid(oldResult) ? &oldResult.normal_ : nullptr);

                        // since step didn't mess with it, report the wall contact
                        auto& data = GetEventDataMap();
                        data[VRHitwall::P_NORMAL] = result.normal_;
                        SendEvent(E_VRHITWALL, data);
                    }
                    else if (stepResult == HANDLE_STEP_SLOW_STEP)
                        return;
                }
                else
                {
                    VelocityClip(delta, result.normal_, ResultValid(oldResult) ? &oldResult.normal_ : nullptr);
                    pos += delta * result.hitFraction_;
                    delta -= delta * result.hitFraction_;
                }
            }
            else // no contact, perform the move and check if it's walked off an edge
            {
                pos += delta;
                delta = Vector3::ZERO;
                AnchorToFloor(delta, stepHeight_, false);
                didStep = true;
                break;
            }
        }
        if (!didStep)
        {
            // keep us anchored on the ground or not
            Vector3 nullDelta = Vector3::ZERO;
            AnchorToFloor(nullDelta, stepHeight_, false);
        }
    }
    else if (moveState_ == VR_PHY_STATE_FLYING)
    {
        // mostly the same as walk except we do not check for stepping
        PhysicsRaycastResult oldResult;
        oldResult.distance_ = M_INFINITY;

        for (int i = 0; i < 6; ++i)
        {
            if (delta.Length() < 0.0001f)
                break;

            oldResult = result;
            if (MoveInternal(delta, &result))
            {
                // check for step up / down
                if (Abs(result.normal_.y_) < upDotProduct_)
                {
                    // clip velocity so we slide along contacts
                    pos += delta * result.hitFraction_;
                    delta -= delta * result.hitFraction_;
                    VelocityClip(delta, result.normal_, ResultValid(oldResult) ? &oldResult.normal_ : nullptr);

                    // since step didn't mess with it, report the wall contact
                    auto& data = GetEventDataMap();
                    data[VRHitwall::P_NORMAL] = result.normal_;
                    SendEvent(E_VRHITWALL, data);
                }
                else
                {
                    VelocityClip(delta, result.normal_, ResultValid(oldResult) ? &oldResult.normal_ : nullptr);
                    pos += delta * result.hitFraction_;
                    delta -= delta * result.hitFraction_;
                }
            }
            else // no contact, perform the move and check if it's walked off an edge
            {
                pos += delta;
                delta = Vector3::ZERO;
                break;
            }
        }
    }
    else if (moveState_ == VR_PHY_STATE_FALLING)
    {
        // check for collisions against sloped surface
        if (MoveInternal(g, &result))
        {
            // iterate a few few times so we can slide down steep slops
            int iters = 0;
            PhysicsRaycastResult oldResult;
            oldResult.distance_ = M_INFINITY;
            bool didLand = false;

            for (bool i = true; i == true && iters < 6; i = MoveInternal(g, &result), ++iters)
            {
                if (g.Length() < 0.0001f)
                    break;

                pos += g * result.hitFraction_;
                g -= g * result.hitFraction_;

                // did we hit a flat enough surface?
                if (result.normal_.y_ > upDotProduct_)
                {
                    // setup floor object
                    floorObject_ = result.body_ ? result.body_->GetNode() : nullptr;
                    floorObjectPrevTransform_ = floorObject_ ? floorObject_->GetWorldTransform() : Matrix3x4::IDENTITY;

                    auto& data = GetEventDataMap();
                    data[VRLanded::P_FALLTIME] = timeFalling_;
                    SendEvent(E_VRLANDED);
                    
                    moveState_ = VR_PHY_STATE_WALKING;
                    timeFalling_ = 0.0f;
                    didLand = true;
                    break;
                }

                // not flat enough, slide down the slope
                VelocityClip(g, result.normal_, ResultValid(oldResult) ? &oldResult.normal_ : nullptr);
                timeFalling_ += dt * ((iters + 1) / 6.0f);
                oldResult = result;
            }

            if (!didLand)
            {
                auto& data = GetEventDataMap();
                data[VRLanded::P_FALLTIME] = timeFalling_;
                SendEvent(E_VRFALLING, data);
            }
        }
        else 
        {
            // still falling
            pos += g;
            timeFalling_ += dt;
            
            auto& data = GetEventDataMap();
            data[VRHitwall::P_NORMAL] = timeFalling_;
            SendEvent(E_VRHITWALL, data);
        }
    }
    else if (moveState_ == VR_PHY_STATE_CLIMBING)
    {
        // delta is provided externally
        // we basically just go where we can, when climbing we're effectively sliding
        int iters = 6;
        PhysicsRaycastResult oldResult;
        oldResult.distance_ = M_INFINITY;

        while (MoveInternal(delta, &result) && --iters > 0)
        {
            pos += delta * result.hitFraction_;
            delta -= delta * result.hitFraction_;
            VelocityClip(delta, result.normal_, ResultValid(oldResult) ? &oldResult.normal_ : nullptr);

            oldResult = result;
        }
    }

    if (oldPos != pos)
    {
        auto stagePos = GetNode()->GetWorldPosition();
        auto delta = pos - oldPos;
        stagePos += delta;
        GetNode()->SetWorldPosition(stagePos);
    }
}

bool VRRigWalker::MoveInternal(Vector3 delta, PhysicsRaycastResult* outResult)
{
    if (delta.LengthSquared() == 0.0f)
        return false;

    auto head = GetNode()->GetChild("Head");
    auto vrBody = head->GetChild("VRBody");
    auto pos = head->GetWorldPosition();
    if (head == nullptr || vrBody == nullptr)
        return false;

    auto world = GetScene()->GetComponent<PhysicsWorld>();

    PhysicsRaycastResult result;
    world->NotMeConvexCast(result, vrBody->GetComponent<RigidBody>(), pos, Quaternion::IDENTITY, pos + delta, Quaternion::IDENTITY);
    if (outResult)
        *outResult = result;    

    return (result.hitFraction_ != 1.0f && result.distance_ != M_INFINITY);
}

int VRRigWalker::HandleStep(Vector3& pos, Vector3& delta, float stepHeight)
{
    auto world = GetScene()->GetComponent<PhysicsWorld>();
    auto head = GetNode()->GetChild("Head");
    auto vrBody = head->GetChild("VRBody");
    if (head == nullptr || vrBody == nullptr)
        return HANDLE_STEP_NO_CHANGE;

    auto body = vrBody->GetComponent<RigidBody>();
    const Vector3 gravity = world->GetGravity().Normalized();

    const Vector3 up = -gravity * stepHeight;
    const Vector3 down = gravity * stepHeight;

    PhysicsRaycastResult result;
    result.distance_ = M_INFINITY;

    const Vector3 keepPos = pos;
    const Vector3 keepDelta = delta;
    delta.y_ = 0;

    // try move vertically to check for clearance
    world->NotMeConvexCast(result, body, pos, Quaternion::IDENTITY, pos + up, Quaternion::IDENTITY);
    if (ResultValid(result))
    {
        // have upwards clearance, so move up and now try to move along the delta
        result.distance_ = M_INFINITY;
        pos += up * result.hitFraction_;

        delta.Normalize();
        delta *= collisionRadius_;

        world->NotMeConvexCast(result, body, pos, Quaternion::IDENTITY, pos + delta, Quaternion::IDENTITY);

        // need to fully clear the sweep so that half of our body is on the step
        // if we fail to do that then we're surely going to be in a bad situation that
        // has good odds of causing hysterics from tiny changes in head motion.
        if (result.hitFraction_ == 1.0f && result.distance_ != M_INFINITY)
        {
            pos += result.hitFraction_ * delta;
            delta -= result.hitFraction_ * delta;

            // were able to advance, now try to anchor to floor
            result.distance_ = M_INFINITY;
            world->NotMeConvexCast(result, body, pos, Quaternion::IDENTITY, pos + down, Quaternion::IDENTITY);
            if (result.body_ && (result.body_->GetCollisionLayer() & denyWalkMask_) == 0) // check walk denial mask
            {
                if (result.normal_.y_ > upDotProduct_ && (result.position_.y_+collisionHeight_) > keepPos.y_ && result.hitFraction_ < 1.0f && result.distance_ != M_INFINITY)
                {
                    // success
                    pos += result.hitFraction_ * down;

                    auto spline = CalculateStepSpline(keepPos, pos, true);
                    SetFollowSpline(spline, (result.distance_ / stepHeight) * 0.5f, VR_PHY_STATE_WALKING); // faster if short, slower if tall

                    pos = keepPos;
                    delta = Vector3::ZERO;

                    return HANDLE_STEP_SLOW_STEP;
                }
            }
            // else didn't find floor, we'll change nothing
        }
        // else couldn't advance even slightly forward along delta, not enough clearance
    }
    // else no room to step up

    // restore original positions
    pos = keepPos;
    delta = keepDelta;

    // Check for step down
    if (delta.y_ <= 0.0f) // don't check for step down if we're moving upwards
    {
        delta.Normalize();
        delta *= collisionRadius_ * 2; // have to be able to move the whole distance away

        world->NotMeConvexCast(result, body, pos, Quaternion::IDENTITY, pos + delta, Quaternion::IDENTITY);
        if (result.hitFraction_ == 1.0f && result.distance_ != M_INFINITY) // need to fully clear the sweep
        {
            pos += result.hitFraction_ * delta;
            delta -= result.hitFraction_ * delta;

            // were able to advance, now try to anchor to floor
            result.distance_ = M_INFINITY;
            world->NotMeConvexCast(result, body, pos, Quaternion::IDENTITY, pos + down, Quaternion::IDENTITY);

            // needs to be a walkable surface and needs to be lower than us
            if (result.body_ && (result.body_->GetCollisionLayer() & denyWalkMask_) == 0)
            {
                if (result.normal_.y_ > upDotProduct_ && result.hitFraction_ < 1.0f && result.hitFraction_ > 0.0001f && result.distance_ != M_INFINITY && (result.position_.y_ + collisionHeight_) < pos.y_)
                {
                    // success
                    pos += result.hitFraction_ * down + Vector3(0, 0.001f, 0);

                    auto spline = CalculateStepSpline(keepPos, pos, false);
                    SetFollowSpline(spline, (result.distance_ / stepHeight) * 0.25f, VR_PHY_STATE_WALKING);

                    pos = keepPos;
                    delta = Vector3::ZERO;

                    return HANDLE_STEP_SLOW_STEP;
                }
            }
            // else didn't find floor, we'll change nothing
        }
        // else couldn't advance even slightly forward along delta, not enough clearance
    }

    // restore original positions
    pos = keepPos;
    delta = keepDelta;

    return HANDLE_STEP_NO_CHANGE;
}

void VRRigWalker::AnchorToFloor(Vector3& pos, float stepHeight, bool checkPlatformMove)
{
    auto world = GetScene()->GetComponent<PhysicsWorld>();
    auto head = GetNode()->GetChild("Head");
    auto vrBody = head->GetChild("VRBody");
    if (head == nullptr || vrBody == nullptr)
        return;

    auto body = vrBody->GetComponent<RigidBody>();
    const Vector3 gravity = world->GetGravity().Normalized();

    const Vector3 down = gravity * stepHeight;

    PhysicsRaycastResult result;
    // unlike above only move for 1 step-height
    // try to step down, ie. we've walked off a ledge
    world->NotMeConvexCast(result, body, pos, Quaternion::IDENTITY, pos + down, Quaternion::IDENTITY);
    if (result.hitFraction_ < 1.0f && result.distance_ != M_INFINITY)
    {
        // have ground contact
        pos += down * result.hitFraction_;

        if (result.body_)
        {
            auto newFloorObject = result.body_->GetNode();

            // if found same floor object, process any platform motion from the object we're standing on moving
            if (checkPlatformMove && newFloorObject == floorObject_)
            {
                auto newTrans = newFloorObject->GetWorldTransform();
                if (floorObjectPrevTransform_ != newTrans) { // bring into old space, now bring into new space
                    pos = newTrans * (floorObjectPrevTransform_.Inverse() * pos);
                    Vector3 oldDir = floorObjectPrevTransform_.Rotation() * Vector3::FORWARD;
                    Vector3 newDir = newTrans.Rotation() * Vector3::FORWARD;
                    oldDir.y_ = 0;
                    newDir.y_ = 0;
                    oldDir.Normalize();
                    newDir.Normalize();

                    Quaternion rot(oldDir, newDir);
                    GetNode()->RotateAround(pos, -rot, TS_WORLD);
                }
            }

            floorObject_ = newFloorObject;
            floorObjectPrevTransform_ = result.body_->GetNode()->GetWorldTransform();
        }
        else
            floorObject_.Reset();
    }
    else
    {
        // don't have ground contact, transition to falling state
        // outside code will do the move
        moveState_ = VR_PHY_STATE_FALLING;
        timeFalling_ = 0.0f;
        // reset floor object
        floorObject_.Reset();
        floorObjectPrevTransform_ = Matrix3x4::IDENTITY;
        
        auto& data = GetEventDataMap();
        data[VRLanded::P_FALLTIME] = timeFalling_;
        SendEvent(E_VRFALLING, data);
    }
}

void VRRigWalker::UpdateCollider()
{
    if (auto xr = GetSubsystem<OpenXR>())
    {
        auto headNode = GetNode()->GetChild("Head");
        auto headPos = headNode->GetPosition(); // do want local here

        auto vrBody = headNode->GetChild("VRBody");
        if (vrBody == nullptr)
            vrBody = headNode->CreateChild("VRBody");

        vrBody->SetWorldRotation(Quaternion::IDENTITY);
        auto rb = vrBody->GetOrCreateComponent<RigidBody>();        

        rb->SetKinematic(true);
        rb->SetAngularFactor(Vector3::ZERO);
        if (collider_.Null())
            collider_ = vrBody->GetOrCreateComponent<CollisionShape>();

        collisionHeight_ = headPos.y_;
        collider_->SetShapeType(SHAPE_CAPSULE);
        collider_->SetSize(Vector3(collisionRadius_ * 2, headPos.y_ + collisionRadius_, 0.0f)); // not taking part of the radius out gives us head room
        collider_->SetPosition(Vector3(0, headPos.y_ * -0.5f, 0)); // head is at the top, this way we can trace from our head position
    }
}

void VRRigWalker::Teleport(VRHand hand, float dt, bool commit, DebugRenderer* debug)
{
    if (!IsEnabled())
        return;

    teleportActiveTime_ += dt;
    UpdateCollider();

    auto world = GetScene()->GetComponent<PhysicsWorld>();
    auto xr = GetSubsystem<OpenXR>();

    auto gravity = world->GetGravity();
    auto node = GetNode();

    Ray aimRay = xr->GetHandAimRay(hand).Transformed(GetNode()->GetWorldTransform());

    Vector3 aimVector = aimRay.direction_;
    aimVector.Normalize();

    Vector3 curPos = aimRay.origin_;

    const Matrix3x4 inverse = node->GetWorldTransform().Inverse();
    auto nodePos = GetNode()->GetWorldPosition();

    Vector3 velocity = aimVector * teleportReachPower_;

    const int maxIterations = 128;

    PODVector<Vector3> pointsToStrip;   
    PODVector<Vector3> rawPoints;

    destinationValid_ = false;
    altTeleportDestination_ = INVALID_DEST;

    pointsToStrip.Push(inverse * curPos);
    rawPoints.Push(curPos);

    for (int i = 0; i < maxIterations; ++i)
    {
        PhysicsRaycastResult result;
        Ray r(curPos, velocity.Normalized());
        
        world->RaycastSingle(result, r, velocity.Length() * 0.08f, teleportRayCollisionMask_);

        if (result.hitFraction_ == 0.0f)
        {
            curPos += velocity * 0.07f;
            curPos += gravity * 0.5 * 0.007f;
            velocity += gravity * 0.07f;

            pointsToStrip.Push(inverse * curPos);
            if (debug)
                rawPoints.Push(curPos);
        }
        else if (result.normal_.Normalized().DotProduct(Vector3::UP) > upDotProduct_)
        {
            pointsToStrip.Push(inverse * result.position_);
            if (debug)
                rawPoints.Push(result.position_);
            teleportDestination_ = result.position_;
            break;
        }            
    }

    // Check for validity
    Vector3 headPos, localHeadPos;
    {
        auto headNode = node->GetChild("Head");

        localHeadPos = headNode->GetPosition();
        headPos = headNode->GetWorldPosition();

        // center us
        auto startPos = teleportDestination_;
        startPos.y_ += collisionHeight_ / 2 + collisionRadius_;
        startPos.y_ += 0.1f;

        if (debug)
        {
            debug->AddLine(startPos, startPos + Vector3(0, collisionHeight_ / 2 + collisionRadius_, 0), Color::RED);
            debug->AddLine(startPos, startPos - Vector3(0, collisionHeight_ / 2 + collisionRadius_, 0), Color::RED);
            debug->AddLine(startPos, startPos + Vector3(collisionRadius_, 0, 0), Color::RED);
            debug->AddLine(startPos, startPos - Vector3(collisionRadius_, 0, 0), Color::RED);
            debug->AddLine(startPos, startPos + Vector3(0, 0, collisionRadius_), Color::RED);
            debug->AddLine(startPos, startPos - Vector3(0, 0, collisionRadius_), Color::RED);
        }

        PODVector<RigidBody*> result;
        world->GetRigidBodies(result, collider_.Get(), startPos, teleportDestCollisionMask_);
        if (result.Empty())
            destinationValid_ = true;
        else
        {
            // we'll walk back 5 radii to see if there's a good collision
            Vector3 shiftDir = Vector3(headPos.x_, teleportDestination_.y_, headPos.z_).Normalized();
            for (int i = 0; i < 5; ++i)
            {
                startPos += shiftDir * collisionRadius_;
                result.Clear();
                world->GetRigidBodies(result, collider_.Get(), startPos, teleportDestCollisionMask_);
                if (result.Empty())
                {
                    altTeleportDestination_ = startPos;
                    break;
                }
                else
                    altTeleportDestination_ = INVALID_DEST;
            }
            destinationValid_ = false;
        }
    }

    auto geom = teleportRay_ = node->GetOrCreateComponent<CustomGeometry>();
    geom->Clear();

    if (commit)
    {
        geom->SetEnabled(false);
        if (destinationValid_ || altTeleportDestination_ != INVALID_DEST)
        {
            // want delta to be so we move our head to that position
            auto delta = (destinationValid_ ? teleportDestination_ : altTeleportDestination_) - headPos;

            // need to account for the current head height
            GetNode()->SetWorldPosition(nodePos + delta + Vector3(0, localHeadPos.y_, 0));
            
            destinationValid_ = false;
        }
        teleportActiveTime_ = 0.0f;
    }
    else
    {
        geom->SetEnabled(true);
        geom->SetNumGeometries(1);
        geom->BeginGeometry(0, TRIANGLE_LIST);
        
        float lenConsumed = 0.0f;
        float totalLen;
        for (unsigned i = 0; i < pointsToStrip.Size() - 1; ++i)
        {
            auto self = pointsToStrip[i];
            auto next = pointsToStrip[i + 1];
            totalLen += (next - self).Length();
        }

        for (unsigned i = 0; i < pointsToStrip.Size() - 1; ++i)
        {
            auto self = pointsToStrip[i];
            auto next = pointsToStrip[i + 1];
            float len = (next - self).Length();

            const float startFraction = lenConsumed / totalLen;
            const float endFraction = (lenConsumed + len) / totalLen;

            auto c = destinationValid_ ? teleportArcColor_ : invalidTeleportArcColor_;

            if (debug)
                debug->AddLine(rawPoints[i], rawPoints[i+1], c);

            Color startColor = c;
            Color endColor = c;
            if (startFraction < startAlphaFadeLength_)
                startColor.a_ *= NORMALIZE(startFraction, 0.0f, startAlphaFadeLength_);
            if (endFraction < startAlphaFadeLength_)
                endColor.a_ *= NORMALIZE(endFraction, 0.0f, startAlphaFadeLength_);
            if (startFraction > (1.0f - endAlphaFadeLength_))
                startColor.a_ = 1.0f - NORMALIZE(startFraction, (1.0f - endAlphaFadeLength_), 1.0f);
            if (endFraction > (1.0f - endAlphaFadeLength_))
                endColor.a_ = 1.0f - NORMALIZE(endFraction, (1.0f - endAlphaFadeLength_), 1.0f);

            auto vec = next - self;
            auto sideVec = Vector3::UP.CrossProduct(vec.Normalized()).Normalized();

            // tri 1
            geom->DefineVertex(self + sideVec * teleportRibbonWidth_ * 0.5f);
            geom->DefineColor(startColor);

            geom->DefineVertex(self + -sideVec * teleportRibbonWidth_ * 0.5f);
            geom->DefineColor(startColor);

            geom->DefineVertex(next + sideVec * teleportRibbonWidth_ * 0.5f);
            geom->DefineColor(endColor);


            // tri 2
            geom->DefineVertex(next + sideVec * teleportRibbonWidth_ * 0.5f);
            geom->DefineColor(endColor);

            geom->DefineVertex(self + -sideVec * teleportRibbonWidth_ * 0.5f);
            geom->DefineColor(startColor);

            geom->DefineVertex(next + -sideVec * teleportRibbonWidth_ * 0.5f);
            geom->DefineColor(endColor);

            lenConsumed += len;
        }

        //if (destinationValid_)
        {
            const float animTime = Cos((M_PI * 2) * teleportActiveTime_ * teleportRingPulseRate_);
            const float animOffset = NORMALIZE(animTime, -1.0f, 1.0f) * teleportRingPulseSize_;

            Vector3 heightVec(0, teleportRingOffset_, 0);
            Sphere sphere(inverse * teleportDestination_ + heightVec, collisionRadius_ + animOffset);
            Vector3 offsetXVec(collisionRadius_, 0, 0);
            Vector3 offsetZVec(0, 0, collisionRadius_);

            Color c = destinationValid_ ? validDestinationColor_ : invalidDestinationColor_;

            for (auto i = 0; i < 360; i += 20)
            {
                const Vector3 p1 = sphere.GetPoint(i, 90);
                const Vector3 p2 = sphere.GetPoint(i + 20, 90);
                const Vector3 p1Dir = (p1 - sphere.center_).Normalized();
                const Vector3 p2Dir = (p2 - sphere.center_).Normalized();

                if (debug)
                    debug->AddSphere(Sphere(teleportDestination_, collisionRadius_ + animOffset), c);

                geom->DefineVertex(p1 + p1Dir * teleportRingWidth_);
                geom->DefineColor(c);
                geom->DefineVertex(p1 - p1Dir * teleportRingWidth_);
                geom->DefineColor(c);
                geom->DefineVertex(p2 + p2Dir * teleportRingWidth_);
                geom->DefineColor(c);

                geom->DefineVertex(p2 + p2Dir * teleportRingWidth_);
                geom->DefineColor(c);
                geom->DefineVertex(p1 - p1Dir * teleportRingWidth_);
                geom->DefineColor(c);
                geom->DefineVertex(p2 - p2Dir * teleportRingWidth_);
                geom->DefineColor(c);
            }

            if (altTeleportDestination_ != INVALID_DEST)
            {
                c = validDestinationColor_;
                for (auto i = 0; i < 360; i += 20)
                {
                    const Vector3 p1 = sphere.GetPoint(i, 90);
                    const Vector3 p2 = sphere.GetPoint(i + 20, 90);
                    const Vector3 p1Dir = (p1 - sphere.center_).Normalized();
                    const Vector3 p2Dir = (p2 - sphere.center_).Normalized();

                    if (debug)
                        debug->AddSphere(Sphere(teleportDestination_, collisionRadius_ + animOffset), c);

                    geom->DefineVertex(p1 + p1Dir * teleportRingWidth_);
                    geom->DefineColor(c);
                    geom->DefineVertex(p1 - p1Dir * teleportRingWidth_);
                    geom->DefineColor(c);
                    geom->DefineVertex(p2 + p2Dir * teleportRingWidth_);
                    geom->DefineColor(c);

                    geom->DefineVertex(p2 + p2Dir * teleportRingWidth_);
                    geom->DefineColor(c);
                    geom->DefineVertex(p1 - p1Dir * teleportRingWidth_);
                    geom->DefineColor(c);
                    geom->DefineVertex(p2 - p2Dir * teleportRingWidth_);
                    geom->DefineColor(c);
                }
            }
        }

        auto cache = GetSubsystem<ResourceCache>();
        geom->DefineGeometry(0, TRIANGLE_LIST, geom->GetNumVertices(0), false, true, false, false);
        geom->Commit();
        if (rayMaterial_)
            geom->SetMaterial(rayMaterial_);
        else
            geom->SetMaterial(rayMaterial_ = cache->GetResource<Material>("Materials/XRTeleport.xml"));

        GetScene()->GetComponent<Octree>()->AddManualDrawable(geom);
    }
}

void VRRigWalker::SetRayMaterial(Material* mat)
{
    rayMaterial_ = mat;
}

Material* VRRigWalker::GetRayMaterial() const
{
    return rayMaterial_;
}

void VRRigWalker::SetFollowSpline(const Spline& s, float duration, VRBodyPhysicsState endState)
{ 
    moveState_ = VR_PHY_STATE_SPLINE_FOLLOW;
    followSpline_ = s; 
    totalSplineTime_ = duration; 
    splineExitState_ = endState;
    timeInSpline_ = 0.0f; 
}

Spline VRRigWalker::CalculateStepSpline(Vector3 start, Vector3 end, bool verticalPriority)
{
    Spline s;
    if (verticalPriority)
    {
        s.AddKnot(start);
        s.AddKnot(Vector3(start.x_, start.y_ + (end.y_ - start.y_) * 0.5f, start.z_));
        s.AddKnot(Vector3(start.x_, end.y_, start.z_));
        s.AddKnot(end);
    }
    else
    {
        s.AddKnot(start);
        Vector3 delta = end - start;
        delta.y_ = 0;
        s.AddKnot(start + delta * 0.5f);
        s.AddKnot(start + delta);
        s.AddKnot(end);
    }
    return s;
}

}

#pragma optimize("", on)
