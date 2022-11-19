#pragma once

#include "../Math/Color.h"
#include "../Graphics/Drawable.h"

namespace Urho3D
{

class Context;

class URHO3D_API LightProbe : public Drawable
{
	URHO3D_OBJECT(LightProbe, Drawable);
public:
	/// Construct.
	explicit LightProbe(Context* context);
	/// Destruct.
	~LightProbe() override;
	/// Register object factory. Drawable must be registered first.
	static void RegisterObject(Context* context);

	float GetRadius() const { return radius_; }
	void SetRadius(float value);

	Color GetColor() const { return color_; }
	void SetColor(const Color& value);

	void DrawDebugGeometry(DebugRenderer* debug, bool depthTest) override;

protected:
	/// Ensure there's a probe manager if we're in a scene.
	virtual void OnSceneSet(Scene* scene) override;
	/// Recalculate the world-space bounding box.
	void OnWorldBoundingBoxUpdate() override;

private:
	Color color_;
	float radius_;
};

}