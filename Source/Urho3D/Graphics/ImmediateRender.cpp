#include "ImmediateRender.h"

#include "../Core/Context.h"
#include "../Graphics/VertexBuffer.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Graphics.h"
#include "../Scene/Node.h"
#include "../Input/Input.h"
#include "../Graphics/View.h"
#include "../Graphics/Viewport.h"

namespace Urho3D
{
    ImmediateRenderer::ImmediateRenderer(Context* context) : 
        Component(context)
    {
        vertexBuffer_ = new VertexBuffer(context);
    }
    /// Destruct.
    ImmediateRenderer::~ImmediateRenderer()
    {

    }
    /// Register object factory.
    void ImmediateRenderer::RegisterObject(Context* context)
    {
        context->RegisterFactory<ImmediateRenderer>();
    }

    void ImmediateRenderer::EndFrame()
    {
        Im3d::EndFrame();
    }

    void ImmediateRenderer::Render()
    {
        Im3d::SetContext(imContext_);
        unsigned vertCt = 0;
        for (unsigned i = 0; i < Im3d::GetDrawListCount(); ++i)
        {
            const Im3d::DrawList& drawList = Im3d::GetDrawLists()[i];
            vertCt += drawList.m_vertexCount;
        }

        if (vertexBuffer_->GetVertexCount() < vertCt)
        {
            vertexBuffer_->SetSize(vertCt, {
                VertexElement(TYPE_VECTOR4, SEM_POSITION, 0, false),
                VertexElement(TYPE_UBYTE4_NORM, SEM_COLOR, 0, false) });
        }

        auto* dest = (Im3d::VertexData*)vertexBuffer_->Lock(0, vertCt, true);
        if (!dest)
            return;

        for (unsigned i = 0; i < Im3d::GetDrawListCount(); ++i)
        {
            const Im3d::DrawList& drawList = Im3d::GetDrawLists()[i];
            unsigned cpyCt = drawList.m_vertexCount * sizeof(Im3d::VertexData);
            memcpy(dest, drawList.m_vertexData, cpyCt);
            dest += drawList.m_vertexCount;
        }

        vertexBuffer_->Unlock();

        auto graphics = GetSubsystem<Graphics>();

        ShaderVariation* vsLines = graphics->GetShader(VS, "IM3D", "LINES VERTEX_SHADER");
        ShaderVariation* gsLines = graphics->GetShader(GS, "IM3D", "LINES GEOMETRY_SHADER");
        ShaderVariation* psLines = graphics->GetShader(PS, "IM3D", "LINES PIXEL_SHADER");
        ShaderVariation* vsPoints = graphics->GetShader(VS, "IM3D", "POINTS VERTEX_SHADER");
        ShaderVariation* gsPoints = graphics->GetShader(GS, "IM3D", "POINTS GEOMETRY_SHADER");
        ShaderVariation* psPoints = graphics->GetShader(PS, "IM3D", "POINTS PIXEL_SHADER");
        ShaderVariation* vsTris = graphics->GetShader(VS, "IM3D", "TRIS VERTEX_SHADER");
        ShaderVariation* psTris = graphics->GetShader(PS, "IM3D", "TRIS PIXEL_SHADER");

        graphics->SetBlendMode(BLEND_ALPHA);
        graphics->SetColorWrite(true);
        graphics->SetCullMode(CULL_NONE);
        graphics->SetDepthWrite(false);
        graphics->SetLineAntiAlias(false);
        graphics->SetScissorTest(false);
        graphics->SetStencilTest(false);
        graphics->SetVertexBuffer(vertexBuffer_);
        graphics->SetDepthTest(CMP_ALWAYS);

        static const auto depthTestLayer = Im3d::MakeId("DepthTest");
        static const auto depthLessLayer = Im3d::MakeId("DepthGreater");
        static const auto noDepthLayer = Im3d::MakeId("NoDepth");
        Im3d::Id lastID = 0;
        unsigned vertStart = 0;
        for (unsigned i = 0; i < Im3d::GetDrawListCount(); ++i)
        {
            const Im3d::DrawList& drawList = Im3d::GetDrawLists()[i];
            if (drawList.m_layerId != lastID)
            {
                if (drawList.m_layerId == depthTestLayer)
                {
                    graphics->SetDepthWrite(true);
                    graphics->SetDepthTest(CMP_LESSEQUAL);
                }
                else if (drawList.m_layerId == noDepthLayer || drawList.m_layerId == 0)
                {
                    graphics->SetDepthWrite(false);
                    graphics->SetDepthTest(CMP_ALWAYS);
                }
                else if (drawList.m_layerId == depthLessLayer)
                {
                    graphics->SetDepthWrite(false);
                    graphics->SetDepthTest(CMP_GREATER);
                }
            }
            switch (drawList.m_primType)
            {
            case Im3d::DrawPrimitive_Lines:
                graphics->SetShaders(vsLines, psLines, gsLines, nullptr, nullptr);
                break;
            case Im3d::DrawPrimitive_Points:
                graphics->SetShaders(vsPoints, psPoints, gsPoints, nullptr, nullptr);
                break;
            case Im3d::DrawPrimitive_Triangles:
                graphics->SetShaders(vsTris, psTris, nullptr, nullptr, nullptr);
                break;
            }

            graphics->SetShaderParameter(VSP_MODEL, Matrix3x4::IDENTITY);
            graphics->SetShaderParameter(VSP_VIEW, view_);
            graphics->SetShaderParameter(VSP_VIEWINV, view_.Inverse());
            graphics->SetShaderParameter(VSP_VIEWPROJ, gpuProjection_ * view_);
            
            auto& appData = Im3d::GetAppData();
            static const StringHash VSP_VIEWSIZE = "Viewport";
            graphics->SetShaderParameter(VSP_VIEWSIZE, Vector2(appData.m_viewportSize.x, appData.m_viewportSize.y));

            switch (drawList.m_primType)
            {
            case Im3d::DrawPrimitive_Lines:
                graphics->Draw(LINE_LIST, vertStart, drawList.m_vertexCount);
                break;
            case Im3d::DrawPrimitive_Points:
                graphics->Draw(POINT_LIST, vertStart, drawList.m_vertexCount);
                break;
            case Im3d::DrawPrimitive_Triangles:
                graphics->Draw(TRIANGLE_LIST, vertStart, drawList.m_vertexCount);
                break;
            }
            vertStart += drawList.m_vertexCount;
        }
        graphics->SetShaders(nullptr, nullptr, nullptr, nullptr, nullptr);
    }

    void ImmediateRenderer::BeginFrame(Viewport* viewport, Vector2 mousePos, IntRect r, float timeStep, bool grabInput)
    {
        Im3d::SetContext(imContext_);
        auto& appData = Im3d::GetAppData();
        auto camera = GetNode()->GetComponent<Camera>();

        view_ = camera->GetView();
        projection_ = camera->GetProjection();
        gpuProjection_ = camera->GetGPUProjection();

        auto frustum = camera->GetFrustum();
        for (int i = 0; i < 6; ++i)
            appData.m_cullFrustum[i] = ToIm(Vector4(frustum.planes_[i].normal_, frustum.planes_[i].d_));
        
        appData.m_viewDirection = ToIm(GetNode()->GetWorldDirection());
        appData.m_viewOrigin = ToIm(GetNode()->GetWorldPosition());
        appData.m_deltaTime = timeStep;
        appData.m_worldUp = ToIm(Vector3::UP);
        appData.m_projOrtho = camera->IsOrthographic();
        appData.m_projScaleY = Tan(camera->GetFov()) * 3;
        appData.m_viewportSize = ToIm(r.Size());

        auto input = GetSubsystem<Input>();
        auto mouseP = input->GetMousePosition();
        appData.m_cursorRayOrigin = appData.m_viewOrigin;
        appData.m_cursorRayDirection = ToIm(camera->GetScreenRay(mousePos.x_, mousePos.y_).direction_);

        if (!grabInput)
        {
            for (int i = 0; i < Im3d::Key_Count; ++i)
                appData.m_keyDown[i] = false;
        }
        else
        {
            for (int i = 0; i < Im3d::Key_Count; ++i)
                appData.m_keyDown[i] = false;
            appData.m_keyDown[Im3d::Mouse_Left] = input->GetMouseButtonDown(MOUSEB_LEFT);
        }

        Im3d::NewFrame();
    }

    void ImmediateRenderer::PushMatrix(const Matrix4& mat)
    {
        Im3d::Mat4 newMat;
        memcpy(&newMat, mat.Data(), sizeof(Matrix4));
        Im3d::PushMatrix(newMat);
    }

    void ImmediateRenderer::PushMatrix(const Matrix3x4& mat)
    {
        Im3d::Mat4 newMat;
        newMat.setIdentity();
        for (int i = 0; i < 3; ++i)
            newMat.setRow(i, ToIm(mat.Row(i)));
        Im3d::PushMatrix(newMat);
    }

    bool ImmediateRenderer::GizmoTranslation(const String& id, Vector3& translation, bool local, bool small)
    {
        return Im3d::GizmoTranslation(id.CString(), const_cast<float*>(translation.Data()), local, small);
    }
    bool ImmediateRenderer::GizmoRotation(const String& id, Matrix3& rotation, bool local)
    {
        return Im3d::GizmoRotation(id.CString(), const_cast<float*>(rotation.Data()), local);
    }
    bool ImmediateRenderer::GizmoScale(const String& id, Vector3& scale)
    {
        return Im3d::GizmoScale(id.CString(), const_cast<float*>(scale.Data()));
    }

    bool ImmediateRenderer::GizmoPoint(const String& id, const Vector3& pt, Color c, float radius)
    {
        return Im3d::GizmoPoint(id.CString(), ToIm(pt), ToIm(c), radius);
    }

    bool ImmediateRenderer::GizmoNormal(const String& id, const Vector3& origin, Vector3& normal)
    {
        Quaternion q;
        q.FromRotationTo(Vector3::FORWARD, normal);

        DrawArrow(origin, origin + normal * 2, -1, 16);
        auto rotMat = q.RotationMatrix();
        PushMatrix(Matrix3x4(origin, q, 1));
        bool ret = false;
        if (GizmoRotation(id, rotMat, true))
        {
            ret = true;
            normal = (rotMat * Vector3::FORWARD).Normalized();
        }
        PopMatrix();
        return ret;
    }

    bool ImmediateRenderer::GizmoArrow(const String& id, const Vector3& oOrigin, const Vector3& dir, float& position)
    {
        auto nDir = dir.Normalized();

        Vector3 origin = oOrigin;
        float junk = 0.0f;
        if (Im3d::GizmoDir(Im3d::MakeId(id.CString()), (float*)origin.Data(), ToIm(nDir), ToIm(Color::GREEN), junk))
        {
            Urho3D::Plane plane(nDir, origin);
            position = plane.d_;
            return true;
        }
        return false;
    }

    bool ImmediateRenderer::GizmoPlane(const String& id, const Vector3& oOrigin, Plane& plane)
    {
        Vector3 origin = plane.Origin();
        float junk = 0.0f;
        if (GizmoArrow(id, oOrigin, plane.normal_, plane.d_))
        {
            
        }
        //if (Im3d::GizmoDir(Im3d::MakeId(id.CString()), (float*)origin.Data(), ToIm(plane.normal_), ToIm(Color::GREEN), junk))
        //{
        //    plane = Urho3D::Plane(plane.normal_, origin);
        //    return true;
        //}
        return false;
    }

    void ImmediateRenderer::PointLine(const Vector3& a, const Vector3& b, float size, const Color& color, float spacing)
    {
        Vertex(a, size, color);
        Vertex(b, size, color);
        auto d = a.DistanceToPoint(b);
        for (float f = spacing; f < d; f += spacing)
            Vertex(a.Lerp(b, f / d), size, color);
    }
    void ImmediateRenderer::PointSpline(const Spline& spline, float size, const Color& color, float spacing)
    {
        float length = 0.0f;
        Vector3 a = spline.GetKnot(0).GetVector3();
        for (auto i = 0; i <= 100; ++i)
        {
            Vector3 b = spline.GetPoint(i / 100.f).GetVector3();
            length += Abs((a - b).Length());
            a = b;
        }

        Vertex(spline.GetPoint(0.0f).GetVector3(), size, color);
        Vertex(spline.GetPoint(1.0f).GetVector3(), size, color);
        for (float f = spacing; f < length; f += spacing)
            Vertex(spline.GetPoint(f/length).GetVector3(), size, color);
    }
}