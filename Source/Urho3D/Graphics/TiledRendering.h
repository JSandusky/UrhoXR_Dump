#pragma once

#include <Urho3D/Container/Vector.h>
#include <Urho3D/Math/Frustum.h>
#include <Urho3D/Math/Vector4.h>
#include <Urho3D/Math/Matrix4.h>

#include <Urho3D/Math/Sphere.h>

#include <atomic>

namespace Urho3D
{

	class Camera;
	class StructuredBuffer;
	class Light;

	struct ClusteredLightData {
		Vector4 position_;
		Vector4 shapeData_;
		Vector4 color_;
		int data_[4];
	};
	struct ClusteredDecalData {
		Vector4 position_;  // w = texture array index
		Vector4 direction_; // 
		Vector4 cross_;
	};
	struct ClusteredCellData {
		std::atomic_int16_t lightCount_;
		std::atomic_int16_t decalCount_;
	};
	struct CellClusters {
		ClusteredCellData* data_;
		ClusteredLightData* lights_;
		ClusteredLightData* decals_;
		IntVector3 dim_;

		CellClusters(IntVector3 dim);
		~CellClusters();

		void Reset();
	};

	class TiledRendering
	{
	public:

		PODVector<Frustum> ComputeTileFrustums(Matrix4 projection, int tilesX, int tilesY, int tileW, int tileH, Vector2 screenDim)
		{
			PODVector<Frustum> ret;
			ret.Resize(tilesX * tilesY);
			
			Matrix4 invProj = projection.Inverse();
			Frustum frus;
			frus.Define(projection);
			
			for (int x = 0; x < tilesX; ++x)
			{
				for (int y = 0; y < tilesY; ++y)
					ret[TileIndex(x, y, tilesX)] = Tiled_ComputeFrustum(invProj, x, y, tileW, tileH, screenDim, frus.planes_[PLANE_NEAR], frus.planes_[PLANE_FAR]);
			}
			return ret;
		}

		static void RecordLight(CellClusters* target, Camera* cam, Light* light);

	private:

		Frustum Tiled_ComputeFrustum(Matrix4 inverseProjection, int x, int y, int w, int h, Vector2 screenDimensions, Plane near, Plane far)
		{
			Vector4 screenSpace[] = {
				Vector4(x * w, y * h, -1, 1),
				Vector4((x + 1) * w, y * h, -1, 1),
				Vector4(x * w, (y + 1) * h, -1, 1),
				Vector4((x + 1) * w, (y + 1) * h, -1, 1)
			};

			Vector3 viewSpace[] = { Vector3::ZERO, Vector3::ZERO, Vector3::ZERO, Vector3::ZERO };
			for (int i = 0; i < 4; ++i)
			{
				auto vs = ScreenToView(screenSpace[i], screenDimensions, inverseProjection);
				viewSpace[i] = Vector3(vs.x_, vs.y_, vs.z_);
			}

			Frustum frus;
			frus.vertices_[0] = frus.vertices_[1] = frus.vertices_[2] = frus.vertices_[3] = Vector3::ZERO;
			frus.vertices_[FRUSV_FAR_TOP_LEFT] = viewSpace[0];
			frus.vertices_[FRUSV_FAR_TOP_RIGHT] = viewSpace[1];
			frus.vertices_[FRUSV_FAR_BOTTOM_LEFT] = viewSpace[2];
			frus.vertices_[FRUSV_FAR_BOTTOM_RIGHT] = viewSpace[3];
			frus.UpdatePlanes();

			return frus;
		}

		static inline Vector4 ClipToView(Matrix4 inverseProjection, Vector4 clip)
		{
			Vector4 view = inverseProjection * clip;
			view = view / view.w_;
			return view;
		}

		static inline Vector4 ScreenToView(Vector4 screen, Vector2 screenDimensions, Matrix4 inverseProjection)
		{
			// Convert to normalized texture coordinates
			Vector2 texCoord = Vector2(screen.x_, screen.y_) / screenDimensions;

			// Convert to clip space
			Vector4 clip(texCoord.x_ * 2 - 1, (1.0f - texCoord.y_) * 2.0f - 1.0f, screen.z_, screen.w_);

			return ClipToView(inverseProjection, clip);
		}

		static inline int TileIndex(int x, int y, int dimX) { return (y * dimX) + x; }
		static inline int TileIndex(int x, int y, int z, int dimX, int dimY) { return (z * dimY * dimX) + (y * dimX) + x; }
	};

}