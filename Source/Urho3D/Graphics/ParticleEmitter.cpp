//
// Copyright (c) 2008-2018 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/Profiler.h"
#include "../Graphics/DrawableEvents.h"
#include "../Graphics/ParticleEffect.h"
#include "../Graphics/ParticleEmitter.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ResourceEvents.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"
#include "../Engine/Engine.h"

#include "../DebugNew.h"

namespace Urho3D
{

extern const char* GEOMETRY_CATEGORY;
extern const char* faceCameraModeNames[];
static const unsigned MAX_PARTICLES_IN_FRAME = 100;

extern const char* autoRemoveModeNames[];

ParticleEmitter::ParticleEmitter(Context* context) :
    BillboardSet(context),
    periodTimer_(0.0f),
    emissionTimer_(0.0f),
    lastTimeStep_(0.0f),
    lastUpdateFrameNumber_(M_MAX_UNSIGNED),
    emitting_(true),
    needUpdate_(false),
    serializeParticles_(true),
    sendFinishedEvent_(true),
    autoRemove_(REMOVE_DISABLED),
    previousPosition_(FLT_MIN, FLT_MIN, FLT_MIN),
	nextParticleID_(0),
    warmStart_(false)
{
    SetNumParticles(DEFAULT_NUM_PARTICLES);
}

ParticleEmitter::~ParticleEmitter() = default;

void ParticleEmitter::RegisterObject(Context* context)
{
    context->RegisterFactory<ParticleEmitter>(GEOMETRY_CATEGORY);

    URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, true, AM_DEFAULT);
    URHO3D_MIXED_ACCESSOR_ATTRIBUTE("Effect", GetEffectAttr, SetEffectAttr, ResourceRef, ResourceRef(ParticleEffect::GetTypeStatic()),
        AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Can Be Occluded", IsOccludee, SetOccludee, bool, true, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Cast Shadows", bool, castShadows_, false, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Draw Distance", GetDrawDistance, SetDrawDistance, float, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Shadow Distance", GetShadowDistance, SetShadowDistance, float, 0.0f, AM_DEFAULT);
    URHO3D_ACCESSOR_ATTRIBUTE("Animation LOD Bias", GetAnimationLodBias, SetAnimationLodBias, float, 1.0f, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Warm Start", bool, warmStart_, false, AM_DEFAULT);
    URHO3D_ATTRIBUTE("Is Emitting", bool, emitting_, true, AM_FILE);
    URHO3D_ATTRIBUTE("Period Timer", float, periodTimer_, 0.0f, AM_FILE | AM_NOEDIT);
    URHO3D_ATTRIBUTE("Emission Timer", float, emissionTimer_, 0.0f, AM_FILE | AM_NOEDIT);
    URHO3D_ACCESSOR_ATTRIBUTE("Generate Points", IsGeneratePoints, SetGeneratePoints, bool, false, AM_DEFAULT);
    URHO3D_ENUM_ATTRIBUTE("Autoremove Mode", autoRemove_, autoRemoveModeNames, REMOVE_DISABLED, AM_DEFAULT);
    URHO3D_COPY_BASE_ATTRIBUTES(Drawable);
    URHO3D_MIXED_ACCESSOR_ATTRIBUTE("Particles", GetParticlesAttr, SetParticlesAttr, VariantVector, Variant::emptyVariantVector,
        AM_FILE | AM_NOEDIT);
    URHO3D_MIXED_ACCESSOR_ATTRIBUTE("Billboards", GetParticleBillboardsAttr, SetBillboardsAttr, VariantVector, Variant::emptyVariantVector,
        AM_FILE | AM_NOEDIT);
    URHO3D_ATTRIBUTE("Serialize Particles", bool, serializeParticles_, true, AM_FILE);
}

void ParticleEmitter::OnSetEnabled()
{
    BillboardSet::OnSetEnabled();

    Scene* scene = GetScene();
    if (scene)
    {
        if (IsEnabledEffective())
            SubscribeToEvent(scene, E_SCENEPOSTUPDATE, URHO3D_HANDLER(ParticleEmitter, HandleScenePostUpdate));
        else
            UnsubscribeFromEvent(scene, E_SCENEPOSTUPDATE);
    }
}

void ParticleEmitter::Update(const FrameInfo& frame)
{
    if (!effect_)
        return;

    // Cancel update if has only moved but does not actually need to animate the particles
    if (!needUpdate_)
        return;

    if (previousPosition_ == Vector3(FLT_MIN, FLT_MIN, FLT_MIN))
        previousPosition_ = node_->GetWorldPosition();
    currentPosition_ = node_->GetWorldDirection();

    // If there is an amount mismatch between particles and billboards, correct it
    if (particles_.Size() != billboards_.Size())
        SetNumBillboards(particles_.Size());

    bool needCommit = false;

    bool doWarmStart = false;
    if (warmStart_ && lastViewFrameNumber_ < frame.frameNumber_ - 1 && !CheckActiveParticles())
    {
        doWarmStart = true;
        const float maxStep = GetSubsystem<Engine>()->GetMinFps(); //default is 10, so 1/10 below in most cases.
        lastTimeStep_ = Max(lastTimeStep_, 1.0f / maxStep);
    }

    do {
        // Check active/inactive period switching
        periodTimer_ += lastTimeStep_;
        if (emitting_)
        {
            float activeTime = effect_->GetActiveTime();
            if (activeTime && periodTimer_ >= activeTime)
            {
                emitting_ = false;
                periodTimer_ -= activeTime;
            }
        }
        else
        {
            float inactiveTime = effect_->GetInactiveTime();
            if (inactiveTime && periodTimer_ >= inactiveTime)
            {
                emitting_ = true;
                sendFinishedEvent_ = true;
                periodTimer_ -= inactiveTime;
            }
            // If emitter has an indefinite stop interval, keep period timer reset to allow restarting emission in the editor
            if (inactiveTime == 0.0f)
                periodTimer_ = 0.0f;
        }

        // Check for emitting new particles
        if (emitting_)
        {
            emissionTimer_ += lastTimeStep_;

            float intervalMin = 1.0f / effect_->GetMaxEmissionRate();
            float intervalMax = 1.0f / effect_->GetMinEmissionRate();

            // If emission timer has a longer delay than max. interval, clamp it
            if (emissionTimer_ < -intervalMax)
                emissionTimer_ = -intervalMax;

            unsigned counter = MAX_PARTICLES_IN_FRAME;

            float particleCount = 0.0f;
            float tempEmissionTimer = emissionTimer_;

            while (emissionTimer_ > 0.0f && counter)
            {
                tempEmissionTimer -= Lerp(intervalMin, intervalMax, Random(1.0f));
                --counter;
                particleCount += 1.0f;
            }

            counter = MAX_PARTICLES_IN_FRAME;
            float step = 1.0f / (particleCount - 1);
            float currentDelta = 0.0f;
            while (emissionTimer_ > 0.0f && counter)
            {
                emissionTimer_ -= Lerp(intervalMin, intervalMax, Random(1.0f));
                if (EmitNewParticle(currentDelta))
                {
                    --counter;
                    needCommit = true;
                    currentDelta += step;
                }
                else
                    break;
            }
        }

        // Update existing particles
        Vector3 relativeConstantForce = node_->GetWorldRotation().Inverse() * effect_->GetConstantForce();
        // If billboards are not relative, apply scaling to the position update
        Vector3 scaleVector = Vector3::ONE;
        if (scaled_ && !relative_)
            scaleVector = node_->GetWorldScale();

        auto vortexAxis = node_->GetWorldRotation() * effect_->GetVortexAxis();
        const auto& effectSplines = effect_->GetSplines();
        bool isSpline = effect_->GetSplines().Size() > 0;
        const Matrix3x4 nodeWorldTransform = node_->GetWorldTransform();
        const Quaternion& nodeWorldRotation = node_->GetWorldRotation();
        for (unsigned i = 0; i < particles_.Size(); ++i)
        {
            Particle& particle = particles_[i];
            Billboard& billboard = billboards_[i];

            if (billboard.enabled_)
            {
                needCommit = true;

                // Time to live
                if (particle.timer_ >= particle.timeToLive_)
                {
                    billboard.enabled_ = false;
                    doWarmStart = false;
                    continue;
                }
                const float prevTime = particle.timer_;
                particle.timer_ += lastTimeStep_;

                // Velocity & position
                const Vector3& constantForce = effect_->GetConstantForce();
                if (constantForce != Vector3::ZERO)
                {
                    if (relative_)
                        particle.velocity_ += lastTimeStep_ * relativeConstantForce;
                    else
                        particle.velocity_ += lastTimeStep_ * constantForce;
                }

                const Vector2& vortexForce = effect_->GetVortexForce();
                if (vortexForce != Vector2::ZERO)
                    particle.velocity_ = Quaternion(Random(vortexForce.x_, vortexForce.y_) * lastTimeStep_, vortexAxis) * particle.velocity_;

                float dampingForce = effect_->GetDampingForce();
                if (dampingForce != 0.0f)
                {
                    Vector3 force = -dampingForce * particle.velocity_;
                    particle.velocity_ += lastTimeStep_ * force;
                }

                if (!isSpline)
                {
                    billboard.position_ += lastTimeStep_ * particle.velocity_ * scaleVector;
                    billboard.direction_ = particle.velocity_.Normalized();
                }
                else
                {
                    unsigned idx = SeededRand(particle.identifier_ * GetID());
                    Vector3 oldPos = effectSplines[idx % effectSplines.Size()].GetPoint(prevTime / particle.timeToLive_).GetVector3();
                    Vector3 newPos = effectSplines[idx % effectSplines.Size()].GetPoint(particle.timer_ / particle.timeToLive_).GetVector3();
                    billboard.position_ = nodeWorldTransform * newPos;
                    billboard.direction_ = nodeWorldRotation * (billboard.position_ - oldPos).Normalized();
                }

                // Rotation
                billboard.rotation_ += lastTimeStep_ * particle.rotationSpeed_;

                // Scaling
                float sizeAdd = effect_->GetSizeAdd();
                float sizeMul = effect_->GetSizeMul();
                if (sizeAdd != 0.0f || sizeMul != 1.0f)
                {
                    particle.scale_ += lastTimeStep_ * sizeAdd;
                    if (particle.scale_ < 0.0f)
                        particle.scale_ = 0.0f;
                    if (sizeMul != 1.0f)
                        particle.scale_ *= (lastTimeStep_ * (sizeMul - 1.0f)) + 1.0f;
                    billboard.size_ = particle.size_ * particle.scale_;
                }

                // Color interpolation
                unsigned& index = particle.colorIndex_;
                const Vector<ColorFrame>& colorFrames_ = effect_->GetColorFrames();
                if (index < colorFrames_.Size())
                {
                    if (index < colorFrames_.Size() - 1)
                    {
                        if (particle.timer_ >= colorFrames_[index + 1].time_)
                            ++index;
                    }
                    if (index < colorFrames_.Size() - 1)
                        billboard.color_ = colorFrames_[index].Interpolate(colorFrames_[index + 1], particle.timer_);
                    else
                        billboard.color_ = colorFrames_[index].color_;
                }

                // Texture animation
                unsigned& texIndex = particle.texIndex_;
                const Vector<TextureFrame>& textureFrames_ = effect_->GetTextureFrames();
                if (textureFrames_.Size() && texIndex < textureFrames_.Size() - 1)
                {
                    if (particle.timer_ >= textureFrames_[texIndex + 1].time_)
                    {
                        billboard.uv_ = textureFrames_[texIndex + 1].uv_;
                        ++texIndex;
                    }
                }
            }
        }
    } while (emitting_ && doWarmStart);

    if (needCommit)
        Commit();

    previousPosition_ = currentPosition_;
    needUpdate_ = false;
}

void ParticleEmitter::SetEffect(ParticleEffect* effect)
{
    if (effect == effect_)
        return;

    Reset();

    // Unsubscribe from the reload event of previous effect (if any), then subscribe to the new
    if (effect_)
        UnsubscribeFromEvent(effect_, E_RELOADFINISHED);

    effect_ = effect;

    if (effect_)
        SubscribeToEvent(effect_, E_RELOADFINISHED, URHO3D_HANDLER(ParticleEmitter, HandleEffectReloadFinished));

    ApplyEffect();
    MarkNetworkUpdate();
}

void ParticleEmitter::SetNumParticles(unsigned num)
{
    // Prevent negative value being assigned from the editor
    if (num > M_MAX_INT)
        num = 0;

    particles_.Resize(num);
    SetNumBillboards(num);
}

void ParticleEmitter::SetEmitting(bool enable)
{
    if (enable != emitting_)
    {
        emitting_ = enable;

        // If stopping emission now, and there are active particles, send finish event once they are gone
        sendFinishedEvent_ = enable || CheckActiveParticles();
        periodTimer_ = 0.0f;
        // Note: network update does not need to be marked as this is a file only attribute
    }
}

void ParticleEmitter::SetSerializeParticles(bool enable)
{
    serializeParticles_ = enable;
    // Note: network update does not need to be marked as this is a file only attribute
}

void ParticleEmitter::SetAutoRemoveMode(AutoRemoveMode mode)
{
    autoRemove_ = mode;
    MarkNetworkUpdate();
}

void ParticleEmitter::ResetEmissionTimer()
{
    emissionTimer_ = 0.0f;
}

void ParticleEmitter::RemoveAllParticles()
{
    for (PODVector<Billboard>::Iterator i = billboards_.Begin(); i != billboards_.End(); ++i)
        i->enabled_ = false;

    Commit();
}

void ParticleEmitter::Reset()
{
    RemoveAllParticles();
    ResetEmissionTimer();
    SetEmitting(true);
}

void ParticleEmitter::ApplyEffect()
{
    if (!effect_)
        return;

    SetMaterial(effect_->GetMaterial());
    SetNumParticles(effect_->GetNumParticles());
    SetRelative(effect_->IsRelative());
    SetScaled(effect_->IsScaled());
    SetSorted(effect_->IsSorted());
    SetFixedScreenSize(effect_->IsFixedScreenSize());
    SetAnimationLodBias(effect_->GetAnimationLodBias());
    SetFaceCameraMode(effect_->GetFaceCameraMode());
}

ParticleEffect* ParticleEmitter::GetEffect() const
{
    return effect_;
}

void ParticleEmitter::SetEffectAttr(const ResourceRef& value)
{
    auto* cache = GetSubsystem<ResourceCache>();
    SetEffect(cache->GetResource<ParticleEffect>(value.name_));
}

ResourceRef ParticleEmitter::GetEffectAttr() const
{
    return GetResourceRef(effect_, ParticleEffect::GetTypeStatic());
}

void ParticleEmitter::SetParticlesAttr(const VariantVector& value)
{
    unsigned index = 0;
    SetNumParticles(index < value.Size() ? value[index++].GetUInt() : 0);

    for (PODVector<Particle>::Iterator i = particles_.Begin(); i != particles_.End() && index < value.Size(); ++i)
    {
        i->velocity_ = value[index++].GetVector3();
        i->size_ = value[index++].GetVector2();
        i->timer_ = value[index++].GetFloat();
        i->timeToLive_ = value[index++].GetFloat();
        i->scale_ = value[index++].GetFloat();
        i->rotationSpeed_ = value[index++].GetFloat();
        i->colorIndex_ = (unsigned)value[index++].GetInt();
        i->texIndex_ = (unsigned)value[index++].GetInt();
    }
}

VariantVector ParticleEmitter::GetParticlesAttr() const
{
    VariantVector ret;
    if (!serializeParticles_)
    {
        ret.Push(particles_.Size());
        return ret;
    }

    ret.Reserve(particles_.Size() * 8 + 1);
    ret.Push(particles_.Size());
    for (PODVector<Particle>::ConstIterator i = particles_.Begin(); i != particles_.End(); ++i)
    {
        ret.Push(i->velocity_);
        ret.Push(i->size_);
        ret.Push(i->timer_);
        ret.Push(i->timeToLive_);
        ret.Push(i->scale_);
        ret.Push(i->rotationSpeed_);
        ret.Push(i->colorIndex_);
        ret.Push(i->texIndex_);
    }
    return ret;
}

VariantVector ParticleEmitter::GetParticleBillboardsAttr() const
{
    VariantVector ret;
    if (!serializeParticles_)
    {
        ret.Push(billboards_.Size());
        return ret;
    }

    ret.Reserve(billboards_.Size() * 7 + 1);
    ret.Push(billboards_.Size());

    for (PODVector<Billboard>::ConstIterator i = billboards_.Begin(); i != billboards_.End(); ++i)
    {
        ret.Push(i->position_);
        ret.Push(i->size_);
        ret.Push(Vector4(i->uv_.min_.x_, i->uv_.min_.y_, i->uv_.max_.x_, i->uv_.max_.y_));
        ret.Push(i->color_);
        ret.Push(i->rotation_);
        ret.Push(i->direction_);
        ret.Push(i->enabled_);
    }

    return ret;
}

void ParticleEmitter::OnSceneSet(Scene* scene)
{
    BillboardSet::OnSceneSet(scene);

    if (scene && IsEnabledEffective())
        SubscribeToEvent(scene, E_SCENEPOSTUPDATE, URHO3D_HANDLER(ParticleEmitter, HandleScenePostUpdate));
    else if (!scene)
         UnsubscribeFromEvent(E_SCENEPOSTUPDATE);
}

bool ParticleEmitter::EmitNewParticle(float interpDelta)
{
    unsigned index = GetFreeParticle();
    if (index == M_MAX_UNSIGNED)
        return false;
    assert(index < particles_.Size());
    Particle& particle = particles_[index];
    Billboard& billboard = billboards_[index];

    Vector3 startDir;
    Vector3 startPos;

    startDir = effect_->GetRandomDirection();
    startDir.Normalize();

    // if a spawn point list is present then use that instead.
    if (!effect_->GetSpawnPoints().Empty())
    {
		const auto& spawnList = effect_->GetSpawnPoints();
        Vector4 p = spawnList[Random(0, spawnList.Size())];
        startPos = Vector3(p.x_, p.y_, p.z_) + Vector3(Random(-p.w_, p.w_), Random(-p.w_, p.w_), Random(-p.w_, p.w_));
    }
	else if (!effect_->GetSplines().Empty() && !effect_->SpawnOnSpline())
	{
		auto& splines = effect_->GetSplines();
		unsigned splineIdx = SeededRand(GetID() * (nextParticleID_ + 1));
		Vector3 p = splines[splineIdx % splines.Size()].GetPoint(0).GetVector3();
		startPos = p;
		particle.velocity_ = splines[splineIdx % splines.Size()].GetPoint(0.001f).GetVector3();
	}
    else
    {
        switch (effect_->GetEmitterType())
        {
        case EMITTER_SPHERE:
        {
            Vector3 dir(
                Random(2.0f) - 1.0f,
                Random(2.0f) - 1.0f,
                Random(2.0f) - 1.0f
            );
            dir.Normalize();
            startPos = effect_->GetEmitterSize() * dir * 0.5f;
        }
        break;

        case EMITTER_RING:
        {
            float angle = Random(effect_->GetEmitterSize().x_, effect_->GetEmitterSize().y_);
            Vector3 dir = Quaternion(angle, Vector3::UP) * Vector3::FORWARD;
            dir.Normalize();
            startPos = dir * Max(effect_->GetEmitterSize().z_, 0.2f) * 0.5f;
            particle.velocity_ = dir * Abs(effect_->GetRandomVelocity());
        }
        break;

        case EMITTER_BOX:
        {
            const Vector3& emitterSize = effect_->GetEmitterSize();
            startPos = Vector3(
                Random(emitterSize.x_) - emitterSize.x_ * 0.5f,
                Random(emitterSize.y_) - emitterSize.y_ * 0.5f,
                Random(emitterSize.z_) - emitterSize.z_ * 0.5f
            );
        }
        break;
        }

        if (effect_->SpawnOnSpline() && !effect_->GetSplines().Empty())
        {
            const int ct = effect_->GetSplines().Size();
            const auto idx = Random(ct);
            float randomPos = Random();
            startPos = effect_->GetSplines()[idx].GetPoint(randomPos).GetVector3();
        }
    }

    // note, we're going backwards, in the event of per-frame count hits want to emit at the current position
    // with higher priority than the previous one
    auto travelDelta = previousPosition_ - currentPosition_;

    startPos += travelDelta * interpDelta;
    particle.size_ = effect_->GetRandomSize();
    particle.timer_ = 0.0f;
    particle.timeToLive_ = effect_->GetRandomTimeToLive();
    particle.scale_ = 1.0f;
    particle.rotationSpeed_ = effect_->GetRandomRotationSpeed();
    particle.colorIndex_ = 0;
    particle.texIndex_ = 0;

    if (faceCameraMode_ == FC_DIRECTION)
    {
        startPos += startDir * particle.size_.y_;
    }

    if (!relative_)
    {
        startPos = node_->GetWorldTransform() * startPos;
        startDir = node_->GetWorldRotation() * startDir;
    };

    if (effect_->GetEmitterType() != EMITTER_RING && effect_->GetSplines().Size() == 0)
        particle.velocity_ = effect_->GetRandomVelocity() * startDir;

	particle.identifier_ = ++nextParticleID_;

    billboard.position_ = startPos;
    billboard.size_ = particles_[index].size_;
    const Vector<TextureFrame>& textureFrames_ = effect_->GetTextureFrames();
    billboard.uv_ = textureFrames_.Size() ? textureFrames_[0].uv_ : Rect::POSITIVE;
    billboard.rotation_ = effect_->GetRandomRotation();
    const Vector<ColorFrame>& colorFrames_ = effect_->GetColorFrames();
    billboard.color_ = colorFrames_.Size() ? colorFrames_[0].color_ : Color();
    billboard.enabled_ = true;
    billboard.direction_ = startDir;

    return true;
}

unsigned ParticleEmitter::GetFreeParticle() const
{
    for (unsigned i = 0; i < billboards_.Size(); ++i)
    {
        if (!billboards_[i].enabled_)
            return i;
    }

    return M_MAX_UNSIGNED;
}

bool ParticleEmitter::CheckActiveParticles() const
{
    for (unsigned i = 0; i < billboards_.Size(); ++i)
    {
        if (billboards_[i].enabled_)
        {
            return true;
            break;
        }
    }

    return false;
}

void ParticleEmitter::HandleScenePostUpdate(StringHash eventType, VariantMap& eventData)
{
    // Store scene's timestep and use it instead of global timestep, as time scale may be other than 1
    using namespace ScenePostUpdate;

    lastTimeStep_ = eventData[P_TIMESTEP].GetFloat();

    // If no invisible update, check that the billboardset is in view (framenumber has changed)
    if ((effect_ && effect_->GetUpdateInvisible()) || viewFrameNumber_ != lastUpdateFrameNumber_)
    {
        lastUpdateFrameNumber_ = viewFrameNumber_;
        needUpdate_ = true;
        MarkForUpdate();
    }

    // Send finished event only once all particles are gone
    if (node_ && !emitting_ && sendFinishedEvent_ && !CheckActiveParticles())
    {
        sendFinishedEvent_ = false;

        // Make a weak pointer to self to check for destruction during event handling
        WeakPtr<ParticleEmitter> self(this);

        using namespace ParticleEffectFinished;

        VariantMap& eventData = GetEventDataMap();
        eventData[P_NODE] = node_;
        eventData[P_EFFECT] = effect_;

        node_->SendEvent(E_PARTICLEEFFECTFINISHED, eventData);

        if (self.Expired())
            return;

        DoAutoRemove(autoRemove_);
    }
}

void ParticleEmitter::HandleEffectReloadFinished(StringHash eventType, VariantMap& eventData)
{
    // When particle effect file is live-edited, remove existing particles and reapply the effect parameters
    Reset();
    ApplyEffect();
}

}
