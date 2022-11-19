#include "../Precompiled.h"
#include "LightProbe.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/LightProbeManager.h>
#include <Urho3D/Scene/Node.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Math/Sphere.h>

namespace Urho3D
{

#define DEFAULT_RADIUS 5.0f

LightProbe::LightProbe(Context* context) : Drawable(context, DRAWABLE_PROBE),
	radius_(DEFAULT_RADIUS)
{

}

LightProbe::~LightProbe()
{

}

extern const char* SCENE_CATEGORY;

void LightProbe::RegisterObject(Context* context)
{
	context->RegisterFactory<LightProbe>(SCENE_CATEGORY);

	URHO3D_ACCESSOR_ATTRIBUTE("Is Enabled", IsEnabled, SetEnabled, bool, true, AM_DEFAULT);
	URHO3D_ACCESSOR_ATTRIBUTE("Radius", GetRadius, SetRadius, float, DEFAULT_RADIUS, AM_DEFAULT);
	URHO3D_ACCESSOR_ATTRIBUTE("Color", GetColor, SetColor, Color, Color::WHITE, AM_DEFAULT);
}

void LightProbe::SetColor(const Color& value)
{
	color_ = value;
	OnMarkedDirty(node_);
	MarkNetworkUpdate();
}

void LightProbe::SetRadius(float rad)
{
	radius_ = rad;
	OnMarkedDirty(node_);
	MarkNetworkUpdate();
}

void LightProbe::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
	debug->AddBoundingBox(GetWorldBoundingBox(), color_);
}

void LightProbe::OnSceneSet(Scene* scene)
{
	if (scene)
		scene->GetOrCreateComponent<LightProbeManager>();
}

void LightProbe::OnWorldBoundingBoxUpdate()
{
	worldBoundingBox_ = BoundingBox(Sphere(node_->GetWorldPosition(), radius_));
	worldBoundingBoxDirty_ = false;
}

}