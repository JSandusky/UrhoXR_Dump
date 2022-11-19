#include "../Precompiled.h"
#include "LightProbeManager.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/GraphicsEvents.h>
#include <Urho3D/Graphics/LightProbe.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Scene/Scene.h>

#include <ThirdParty/nanoflann/nanoflann.h>

#include <Urho3D/DebugNew.h>

namespace Urho3D
{

struct LPPointCloud
{
	PODVector<Pair<Vector3, LightProbe*> > points;

	inline size_t kdtree_get_point_count() const { return points.Size(); }

	inline float kdtree_get_pt(const size_t idx, const size_t dim) const
	{
		if (dim == 0) return points[idx].first_.x_;
		else if (dim == 1) return points[idx].first_.y_;
		else return points[idx].first_.z_;
	}

	template <class BBOX>
	bool kdtree_get_bbox(BBOX& /* bb */) const { return false; }
};

typedef nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, LPPointCloud>, LPPointCloud, 3 /* dim */> LP_KDTreeTable;

struct LightProbeManager::OpaqueData
{
	LPPointCloud pointList_;
	LP_KDTreeTable* table_;

	OpaqueData() {
		table_ = nullptr;
	}
	~OpaqueData() {
		if (table_)
			delete table_;
	}
};

LightProbeManager::LightProbeManager(Context* context) : Component(context)
{
	cloud_ = new OpaqueData();
}

LightProbeManager::~LightProbeManager()
{
	if (cloud_)
		delete cloud_;
	cloud_ = nullptr;
}

extern const char* SCENE_CATEGORY;
void LightProbeManager::RegisterObject(Context* context)
{
	context->RegisterFactory<LightProbeManager>(SCENE_CATEGORY);
}

LightProbe* LightProbeManager::GetNearestProbe(const Vector3& position)
{
	if (cloud_ && cloud_->table_)
	{
		size_t foundIdx = 0;
		float dist = 0.0f;
		size_t foundCt = cloud_->table_->knnSearch(position.Data(), 1, &foundIdx, &dist);
		if (foundCt > 0)
			return cloud_->pointList_.points[foundIdx].second_;
	}
	return nullptr;
}

void LightProbeManager::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
	if (cloud_)
	{
		for (auto pt : cloud_->pointList_.points)
		{
			auto pos = pt.second_->GetNode()->GetWorldPosition();
			auto color = pt.second_->GetColor();
			debug->AddSphere(Sphere(pos, 2), color);
			debug->AddCross(pos, 2, color);
		}
	}
}

void LightProbeManager::OnSceneSet(Scene* scene)
{
	if (scene)
	{
		using namespace BeginRendering;
		SubscribeToEvent(E_BEGINRENDERING, URHO3D_HANDLER(LightProbeManager, HandleBeginRendering));
	}
	else
		UnsubscribeFromAllEvents();
}


void LightProbeManager::HandleBeginRendering(StringHash eventType, VariantMap& eventData)
{
	if (auto scene = GetScene())
	{
		PODVector<LightProbe*> probes;
		scene->GetComponents<LightProbe>(probes, true);
		cloud_->pointList_.points.Clear();
		for (auto probe : probes)
		{
			if (probe->IsEnabledEffective())
				cloud_->pointList_.points.Push(Pair<Vector3, LightProbe*>(probe->GetNode()->GetWorldPosition(), probe));
		}

		if (cloud_->table_)
			delete cloud_->table_;

		if (probes.Empty())
			cloud_->table_ = nullptr;
		else
		{
			cloud_->table_ = new LP_KDTreeTable(3, cloud_->pointList_, nanoflann::KDTreeSingleIndexAdaptorParams(10));
			cloud_->table_->buildIndex();
		}
	}
}

}