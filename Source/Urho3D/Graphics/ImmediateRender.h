#pragma once

#include "../Math/Color.h"
#include "../Math/Frustum.h"
#include "../Scene/Component.h"
#include "../Core/Spline.h"

#include <im3d/im3d.h>
// someone in the include tree of im3d.h has included the word small ... and it messes with VS
#undef small

namespace Urho3D
{

    class BoundingBox;
    class Camera;
    class Polyhedron;
    class Drawable;
    class Light;
    class Matrix3x4;
    class Renderer;
    class Skeleton;
    class Sphere;
    class VertexBuffer;
    class Viewport;

    class URHO3D_API ImmediateRenderer : public Component
    {
        URHO3D_OBJECT(ImmediateRenderer, Component);

    public:
        /// Construct.
        explicit ImmediateRenderer(Context* context);
        /// Destruct.
        ~ImmediateRenderer() override;
        /// Register object factory.
        static void RegisterObject(Context* context);

        void BeginFrame(Viewport*, Vector2, IntRect, float timeStep, bool grabInput);
        void EndFrame();
        void Render();

        void PushMatrix(const Matrix4&);
        void PushMatrix(const Matrix3x4&);
        void PopMatrix() { Im3d::PopMatrix(); }
        void SetIdentity() { Im3d::SetIdentity(); }
        void PushColor(const Color& in) { Im3d::PushColor(ToIm(in)); }
        void PopColor() { Im3d::PopColor(); }

        void PushLayer(const String& layer) { Im3d::PushLayerId(layer.CString()); }
        void PopLayer() { Im3d::PopLayerId(); }

        static Im3d::Vec2 ToIm(const IntVector2& in) { return Im3d::Vec2(in.x_, in.y_); }
        static Im3d::Vec2 ToIm(const Vector2& in) { return Im3d::Vec2(in.x_, in.y_); }
        static Im3d::Vec3 ToIm(const Vector3& in) { return Im3d::Vec3(in.x_, in.y_, in.z_); }
        static Im3d::Vec4 ToIm(const Vector4& in) { return Im3d::Vec4(in.x_, in.y_, in.z_, in.w_); }
        static Im3d::Color ToIm(const Color& in) { return Im3d::Color(in.r_, in.g_, in.b_, in.a_); }

        void  DrawXyzAxes() { Im3d::DrawXyzAxes(); }
        void  DrawPoint(const Vector3& p, float s, Color c) { Im3d::DrawPoint(ToIm(p), s, ToIm(c)); }
        void  DrawLine(const Vector3& a, const Vector3& b, float size, Color color) { Im3d::DrawLine(ToIm(a), ToIm(b), size, ToIm(color)); }
        void  DrawQuad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d) { Im3d::DrawQuad(ToIm(a), ToIm(b), ToIm(c), ToIm(d)); }
        void  DrawQuad(const Vector3& origin, const Vector3& normal, const Vector2& size) { Im3d::DrawQuad(ToIm(origin), ToIm(normal), ToIm(size)); }
        void  DrawQuadFilled(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d) { Im3d::DrawQuadFilled(ToIm(a), ToIm(b), ToIm(c), ToIm(d)); }
        void  DrawQuadFilled(const Vector3& origin, const Vector3& normal, const Vector2& size) { Im3d::DrawQuadFilled(ToIm(origin), ToIm(normal), ToIm(size)); }
        void  DrawCircle(const Vector3& origin, const Vector3& normal, float radius, int detail = -1) { Im3d::DrawCircle(ToIm(origin), ToIm(normal), radius, detail); }
        void  DrawCircleFilled(const Vector3& origin, const Vector3& normal, float radius, int detail = -1) { Im3d::DrawCircleFilled(ToIm(origin), ToIm(normal), radius, detail); }
        void  DrawSphere(const Vector3& origin, float radius, int detail = -1) { Im3d::DrawSphere(ToIm(origin), radius, detail); }
        void  DrawSphereFilled(const Vector3& origin, float radius, int detail = -1) { Im3d::DrawSphereFilled(ToIm(origin), radius, detail); }
        void  DrawAlignedBox(const Vector3& min, const Vector3& max) { Im3d::DrawAlignedBox(ToIm(min), ToIm(max)); }
        void  DrawAlignedBoxFilled(const Vector3& min, const Vector3& max) { Im3d::DrawAlignedBoxFilled(ToIm(min), ToIm(max)); }
        void  DrawCylinder(const Vector3& start, const Vector3& end, float radius, int detail = -1) { Im3d::DrawCylinder(ToIm(start), ToIm(end), radius, detail); }
        void  DrawCapsule(const Vector3& start, const Vector3& end, float radius, int detail = -1) { Im3d::DrawCapsule(ToIm(start), ToIm(end), radius, detail); }
        void  DrawPrism(const Vector3& start, const Vector3& end, float radius, int sides) { Im3d::DrawPrism(ToIm(start), ToIm(end), radius, sides); }
        void  DrawArrow(const Vector3& start, const Vector3& end, float headLength = -1.0f, float headThickness = -1.0f) { Im3d::DrawArrow(ToIm(start), ToIm(end), headLength, headThickness); }

        bool GizmoTranslation(const String& id, Vector3& translation, bool local, bool small);
        bool GizmoRotation(const String& id, Matrix3& rotation, bool local = false);
        bool GizmoScale(const String& id, Vector3& scale); // local scale only
        bool GizmoPoint(const String& id, const Vector3& pt, Color c, float radius);
        bool GizmoNormal(const String& id, const Vector3& origin, Vector3& normal);
        bool GizmoArrow(const String& id, const Vector3& origin, const Vector3& dir, float& position);
        bool GizmoPlane(const String& id, const Vector3& origin, Plane& plane);

        void BeginTriangles() { Im3d::BeginTriangles(); }
        void BeginPoints() { Im3d::BeginPoints(); }
        void BeginLines() { Im3d::BeginLines(); }
        void Vertex(const Vector3& pt) { Im3d::Vertex(ToIm(pt)); }
        void Vertex(const Vector3& pt, float size) { Im3d::Vertex(ToIm(pt), size); }
        void Vertex(const Vector3& pt, float size, const Color& color) { Im3d::Vertex(ToIm(pt), size, ToIm(color)); }
        void End() { Im3d::End(); }

        void PointLine(const Vector3& a, const Vector3& b, float size, const Color&, float spacing);
        void PointSpline(const Spline& spline, float size, const Color&, float spacing);

    private:
        Im3d::Context imContext_;
        SharedPtr<VertexBuffer> vertexBuffer_;
        Matrix3x4 view_;
        Matrix4 projection_;
        Matrix4 gpuProjection_;
    };

}