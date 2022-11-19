
#include <Urho3D/VR/VRInterface.h>

#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Resource/XMLFile.h>

#include <Urho3D/IO/Log.h>

#include "../VR/VRRigWalker.h"

namespace Urho3D
{

static StringHash VRLastTransform = "LastTransform";
static StringHash VRLastTransformWS = "LastTransformWS";

XRBinding::XRBinding(Context* ctx) : BaseClassName(ctx)
{

}

XRBinding::~XRBinding()
{

}

void XRBinding::SetWindowSize(unsigned sz)
{
    if (sz < windowedData_.Size())
        windowedData_.Erase(0, windowedData_.Size() - sz);
}

void XRBinding::PushWindow(Variant v, float time)
{
    if (windowSize_ == 0)
        return;

    const auto& back = windowedData_.Back();

#define DO_WINDOW_UPDATE windowedData_.Pop(); windowedData_.Push({ v, time, time - windowedData_.Back().time_ });

    float span = time - back.time_;
    if (span < windowTiming_)
    {
        DO_WINDOW_UPDATE
        return;
    }

    if (dataType_ != VAR_BOOL && !windowedData_.Empty() && windowDeltaThreshold_ > 0.0f)
    {
        switch (dataType_)
        {
        case VAR_FLOAT:
        {
            if (v.GetFloat() - back.data_.GetFloat() < windowDeltaThreshold_)
            {
                DO_WINDOW_UPDATE
                return;
            }
        } break;
        case VAR_VECTOR2:
        {
            if ((v.GetVector2() - back.data_.GetVector2()).Length() < windowDeltaThreshold_)
            {
                DO_WINDOW_UPDATE
                return;
            }
        } break;
        case VAR_VECTOR3:
        {
            if ((v.GetVector3() - back.data_.GetVector3()).Length() < windowDeltaThreshold_)
            {
                DO_WINDOW_UPDATE
                return;
            }
        } break;
        case VAR_MATRIX3X4:
        {
            auto t = v.GetMatrix3x4().Translation();
            auto pT = back.data_.GetMatrix3x4().Translation();

            if ((t - pT).Length() < windowDeltaThreshold_)
            {
                DO_WINDOW_UPDATE
                return;
            }
        }
        }
    }

#undef DO_WINDOW_UPDATE

    if (windowedData_.Size() == windowSize_)
        windowedData_.Erase(0, 1);
    windowedData_.Push({ v, time, span });
}

VRInterface::VRInterface(Context* ctx) : BaseClassName(ctx)
{

}

VRInterface::~VRInterface()
{

}

void VRInterface::CreateEyeTextures()
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
        sharedTexture_->SetSize(eyeTexWidth_ * 2, eyeTexHeight_, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET, msaaLevel_);
        sharedTexture_->SetFilterMode(FILTER_BILINEAR);

        sharedDS_ = new Texture2D(GetContext());
        sharedDS_->SetNumLevels(1);
        sharedDS_->SetSize(eyeTexWidth_ * 2, eyeTexHeight_, Graphics::GetDepthStencilFormat(), TEXTURE_DEPTHSTENCIL, msaaLevel_);
        sharedTexture_->GetRenderSurface()->SetLinkedDepthStencil(sharedDS_->GetRenderSurface());
    }
    else
    {
        leftTexture_ = new Texture2D(GetContext());
        leftTexture_->SetNumLevels(1);
        leftTexture_->SetSize(eyeTexWidth_, eyeTexHeight_, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET, msaaLevel_);
        leftTexture_->SetFilterMode(FILTER_BILINEAR);

        rightTexture_ = new Texture2D(GetContext());
        rightTexture_->SetNumLevels(1);
        rightTexture_->SetSize(eyeTexWidth_, eyeTexHeight_, Graphics::GetRGBFormat(), TEXTURE_RENDERTARGET, msaaLevel_);
        rightTexture_->SetFilterMode(FILTER_BILINEAR);

        leftDS_ = new Texture2D(GetContext());
        leftDS_->SetNumLevels(1);
        leftDS_->SetSize(eyeTexWidth_, eyeTexHeight_, Graphics::GetDepthStencilFormat(), TEXTURE_DEPTHSTENCIL, msaaLevel_);

        rightDS_ = new Texture2D(GetContext());
        rightDS_->SetNumLevels(1);
        rightDS_->SetSize(eyeTexWidth_, eyeTexHeight_, Graphics::GetDepthStencilFormat(), TEXTURE_DEPTHSTENCIL, msaaLevel_);

        leftTexture_->GetRenderSurface()->SetLinkedDepthStencil(leftDS_->GetRenderSurface());
        rightTexture_->GetRenderSurface()->SetLinkedDepthStencil(rightDS_->GetRenderSurface());
    }
}

void VRInterface::PrepareRig(Node* headRoot)
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

void VRInterface::UpdateRig(Node* vrRig, float nearDist, float farDist, bool forSinglePass)
{
    auto head = vrRig->GetChild("Head");
    auto leftEye = head->GetChild("Left_Eye");
    auto rightEye = head->GetChild("Right_Eye");

    UpdateRig(head->GetScene(), head, leftEye, rightEye, nearDist, farDist, forSinglePass);

    //// track the stage directly under the head, we'll never change our why
    //auto headPos = head->GetWorldPosition();
    //auto headLocal = head->GetPosition();
    //auto selfTrans = head->GetWorldPosition();
    //
    //// track head in the XZ plane
    //vrRig->SetWorldPosition(Vector3(headPos.x_, selfTrans.y_, headPos.z_));
    //
    //// neutralize the head position, keeping vertical
    //head->SetPosition(Vector3(0, headLocal.y_, 0));
}

void VRInterface::UpdateRig(Scene* scene, Node* head, Node* leftEye, Node* rightEye, float nearDist, float farDist, bool forSinglePass)
{
    if (!IsLive())
        return;

    if (head == nullptr)
    {
        auto headRoot = scene->CreateChild("VRRig", LOCAL);
        head = headRoot->CreateChild("Head", LOCAL);
    }

    // no textures? create them now?
    if (sharedTexture_.Null() && leftTexture_.Null() && rightTexture_.Null())
        CreateEyeTextures();

    head->SetVar(VRLastTransform, head->GetTransform());
    head->SetVar(VRLastTransformWS, head->GetWorldTransform());
    head->SetTransform(GetHeadTransform());

    if (leftEye == nullptr)
        leftEye = head->CreateChild("Left_Eye", LOCAL);
    if (rightEye == nullptr)
        rightEye = head->CreateChild("Right_Eye", LOCAL);

    auto leftCam = leftEye->GetOrCreateComponent<Camera>();
    auto rightCam = rightEye->GetOrCreateComponent<Camera>();

    leftCam->SetFov(100.0f);  // junk mostly, the eye matrices will be overriden
    leftCam->SetNearClip(nearDist);
    leftCam->SetFarClip(farDist);

    rightCam->SetFov(100.0f); // junk mostly, the eye matrices will be overriden
    rightCam->SetNearClip(nearDist);
    rightCam->SetFarClip(farDist);

    leftCam->SetProjection(GetProjection(VR_EYE_LEFT, nearDist, farDist));
    rightCam->SetProjection(GetProjection(VR_EYE_RIGHT, nearDist, farDist));

    if (GetRuntime() == VR_OPENVR)
    {
        leftEye->SetTransform(GetEyeLocalTransform(VR_EYE_LEFT));
        rightEye->SetTransform(GetEyeLocalTransform(VR_EYE_RIGHT));

        // uhhh ... what, and it's only the eyes, everyone else's transforms are good
        // buddha ... I hope this isn't backend specific
        leftEye->Rotate(Quaternion(0, 0, 180), TS_LOCAL);
        rightEye->Rotate(Quaternion(0, 0, 180), TS_LOCAL);
    }
    else if (GetRuntime() == VR_OPENXR)
    {
        leftEye->SetTransform(GetEyeLocalTransform(VR_EYE_LEFT));
        rightEye->SetTransform(GetEyeLocalTransform(VR_EYE_RIGHT));
    }
    else
        URHO3D_LOGERROR("Unknown VR runtime specified");

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

        // we need to queue the update ourselves so things can get properly shutdown
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

SharedPtr<XRBinding> VRInterface::GetInputBinding(const String& path)
{
    if (activeActionSet_)
    {
        for (auto b : activeActionSet_->bindings_)
            if (b->path_.Compare(path, false) == 0)
                return b;
    }
    return nullptr;
}

SharedPtr<XRBinding> VRInterface::GetInputBinding(const String& path, VRHand hand)
{
    if (activeActionSet_)
    {
        for (auto b : activeActionSet_->bindings_)
            if (hand == b->hand_ && b->path_.Compare(path, false) == 0)
                return b;
    }
    return nullptr;
}

void VRInterface::DrawEyeMask()
{
    if (hiddenAreaMesh_[0] && hiddenAreaMesh_[1])
    {
        auto gfx = GetSubsystem<Graphics>();

        IntRect vpts[] = {
            GetLeftEyeRect(),
            GetRightEyeRect()
        };

        RenderSurface* surfaces[] = {
            sharedTexture_->GetRenderSurface(),
            sharedTexture_->GetRenderSurface()
        };

        Texture2D* ds[] = {
            sharedDS_.Get(),
            sharedDS_.Get(),
        };

        ShaderVariation* vertexShader = gfx->GetShader(VS, "VR_EyeMask", GetRuntimeName());
        ShaderVariation* pixelShader = gfx->GetShader(PS, "VR_EyeMask", GetRuntimeName());

        for (int i = 0; i < 2; ++i)
        {
            if (gfx->GetRenderTarget(0) != surfaces[i])
            {
                gfx->ResetRenderTargets();
                gfx->SetRenderTarget(0, surfaces[i]);
            }
            if (gfx->GetDepthStencil() == nullptr || gfx->GetDepthStencil()->GetParentTexture() != ds[i])
                gfx->SetDepthStencil(ds[i]);

            gfx->SetViewport(vpts[i]);
            gfx->Clear(CLEAR_COLOR | CLEAR_DEPTH | CLEAR_STENCIL);
            gfx->SetVertexBuffer(hiddenAreaMesh_[i]->GetVertexBuffer(0));
            gfx->SetShaders(vertexShader, pixelShader, nullptr, nullptr, nullptr);
            gfx->SetShaderParameter(StringHash("ProjMat"), GetProjection((VREye)i, 0, 1));
            gfx->SetDepthWrite(true);
            gfx->SetDepthTest(CMP_ALWAYS);
            gfx->SetScissorTest(false);
            gfx->SetStencilTest(false);
            gfx->SetCullMode(CULL_NONE);
            gfx->SetBlendMode(BLEND_REPLACE);
            gfx->SetColorWrite(true);
            gfx->Draw(TRIANGLE_LIST, 0, hiddenAreaMesh_[i]->GetVertexCount());
        }
    }
}

void VRInterface::DrawRadialMask(const char* shader, const char* defines)
{
    if (radialAreaMesh_[0] && radialAreaMesh_[1])
    {
        auto gfx = GetSubsystem<Graphics>();

        IntRect vpts[] = {
            GetLeftEyeRect(),
            GetRightEyeRect()
        };

        RenderSurface* surfaces[] = {
            sharedTexture_->GetRenderSurface(),
            sharedTexture_->GetRenderSurface()
        };

        Texture2D* ds[] = {
            sharedDS_.Get(),
            sharedDS_.Get(),
        };

        String defs = GetRuntimeName();
        if (defines)
            defs.AppendWithFormat(" %s", defines);

        ShaderVariation* vertexShader = gfx->GetShader(VS, shader, defs.CString());
        ShaderVariation* pixelShader = gfx->GetShader(PS, shader, defs.CString());

        for (int i = 0; i < 2; ++i)
        {
            if (gfx->GetRenderTarget(0) != surfaces[i])
                gfx->SetRenderTarget(0, surfaces[i]);
            if (gfx->GetDepthStencil() == nullptr || gfx->GetDepthStencil()->GetParentTexture() != ds[i])
                gfx->SetDepthStencil(ds[i]);

            gfx->SetViewport(vpts[i]);
            gfx->SetVertexBuffer(radialAreaMesh_[i]->GetVertexBuffer(0));
            gfx->SetShaders(vertexShader, pixelShader, nullptr, nullptr, nullptr);
            gfx->SetShaderParameter(StringHash("ProjMat"), GetProjection((VREye)i, 0, 1));
            gfx->SetDepthTest(CMP_ALWAYS);
            gfx->SetCullMode(CULL_NONE);
            gfx->SetDepthWrite(false);
            gfx->SetScissorTest(false);
            gfx->SetStencilTest(false);
            gfx->SetColorWrite(true);

            gfx->SetBlendMode(BLEND_ALPHA);
            gfx->Draw(TRIANGLE_LIST, 0, radialAreaMesh_[i]->GetVertexCount());
        }
    }
}

void VRInterface::SetCurrentActionSet(const String& setName)
{
    auto found = actionSets_.Find(setName);
    if (found != actionSets_.End())
        SetCurrentActionSet(found->second_);
}

void RegisterVR(Context* context)
{
    VRRigWalker::Register(context);
}

}