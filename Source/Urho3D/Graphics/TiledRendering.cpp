#include "../Precompiled.h"
#include "TiledRendering.h"

#include "../Scene/Node.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Light.h"
#include "../Graphics/StructuredBuffer.h"

namespace Urho3D
{

	CellClusters::CellClusters(IntVector3 dim)
	{
		dim_ = dim;
		const int d = dim_.x_ * dim_.y_ * dim.z_;
		data_ = new ClusteredCellData[d];
		lights_ = new ClusteredLightData[d];
		decals_ = new ClusteredLightData[d];
		Reset();
	}
	CellClusters::~CellClusters()
	{
		delete[] data_;
	}

	void CellClusters::Reset()
	{
		const int d = dim_.x_ * dim_.y_ * dim_.z_;
		memset(data_, d * sizeof(ClusteredCellData), 0);
		memset(lights_, d * sizeof(ClusteredLightData), 0);
		memset(decals_, d * sizeof(ClusteredDecalData), 0);
	}

	void RecordLight(CellClusters* target, Camera* cam, Light* light)
	{
		auto lightPos = light->GetNode()->GetWorldPosition();

		ClusteredLightData data;
		data.color_.x_ = light->GetColor().r_;
		data.color_.y_ = light->GetColor().g_;
		data.color_.z_ = light->GetColor().b_;
		data.color_.w_ = light->GetColor().a_;
		ClusteredDecalData decal;

		bool isDecal = false;
		Vector3 max = Vector3(FLT_MIN, FLT_MIN, FLT_MIN);
		Vector3 min = Vector3(FLT_MAX, FLT_MAX, FLT_MAX);
		switch (light->GetLightType())
		{
		case LIGHT_POINT:
		{
			float rad = light->GetRadius();
			float adjRad = rad * 1.05f;
			max = cam->WorldToProjection(lightPos + cam->GetNode()->GetWorldRight() * adjRad - cam->GetNode()->GetWorldUp() * adjRad);
			min = cam->WorldToProjection(lightPos - cam->GetNode()->GetWorldRight() * adjRad + cam->GetNode()->GetWorldUp() * adjRad);
			data.position_ = Vector4(lightPos, 0);
			data.shapeData_.x_ = rad;
			data.data_[0] = 0;
			data.data_[1] = 1; // ramp index
		} break;
		case LIGHT_SPOT: 
		{
			auto frus = light->GetFrustum();
			for (unsigned i = 0; i < 8; ++i)
			{
				auto pt = cam->WorldToProjection(frus.vertices_[i]);
				max.x_ = Max(max.x_, pt.x_);
				max.y_ = Max(max.y_, pt.y_);
				max.z_ = Max(max.z_, pt.z_);
				min.x_ = Min(min.x_, pt.x_);
				min.y_ = Min(min.y_, pt.y_);
				min.z_ = Min(min.z_, pt.z_);
			}
			data.position_ = Vector4(lightPos, 0);
			data.shapeData_ = Vector4(light->GetNode()->GetWorldDirection(), light->GetFov());
			data.data_[0] = 1;
			data.data_[1] = 1; // ramp index
			data.data_[2] = 1; // cookie index
		} break;
		default:
			// should not have reached this point - directional lights (even non-shadow casting) are not part of clustering
			return;
		}

		IntVector3 minCell(floorf(min.x_ * target->dim_.x_), floorf(min.y_ * target->dim_.y_), floorf(min.z_ * target->dim_.z_));
		IntVector3 maxCell(floorf(max.x_ * target->dim_.x_), floorf(max.y_ * target->dim_.y_), floorf(max.z_ * target->dim_.z_));
		for (int z = minCell.z_; z < maxCell.z_ + 1; ++z)
		{
			for (int y = minCell.y_; y < maxCell.y_ + 1; ++y)
			{
				for (int x = minCell.x_; x < maxCell.x_ + 1; ++x)
				{
					const int cell = (z * target->dim_.x_ * target->dim_.y_) + (y * target->dim_.x_) + x;
					if (isDecal)
					{
						int decalIdx = target->data_[cell].decalCount_.fetch_add(1);
						target->decals_[cell + decalIdx] = data;
					}
					else
					{
						int light = target->data_[cell].lightCount_.fetch_add(1);
						target->lights_[cell + light] = data;
					}
				}
			}
		}
	}

}