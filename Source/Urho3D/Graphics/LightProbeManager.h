#pragma once

#include <Urho3D/Scene/Component.h>

namespace Urho3D
{

	class LightProbe;

	class URHO3D_API LightProbeManager : public Component
	{
		URHO3D_OBJECT(LightProbeManager, Component);
	public:
		/// Construct.
		explicit LightProbeManager(Context* context);
		/// Destruct.
		~LightProbeManager() override;
		/// Register object factory.
		static void RegisterObject(Context* context);

		LightProbe* GetNearestProbe(const Vector3& position);

		void DrawDebugGeometry(DebugRenderer* debug, bool depthTest) override;

	protected:
		/// Handle scene being assigned.
		void OnSceneSet(Scene* scene) override;

	private:
		/// Handle the scene subsystem update event.
		void HandleBeginRendering(StringHash eventType, VariantMap& eventData);

		struct OpaqueData;
		OpaqueData* cloud_;
	};

}