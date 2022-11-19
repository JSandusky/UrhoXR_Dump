#pragma once

#include "../Core/Object.h"
#include "../Graphics/OctreeQuery.h"
#include "../Scene/SceneCell.h"

namespace Urho3D
{

	class SceneManager;

	static const int NUM_OCTANTS = 8;
	static const unsigned ROOT_INDEX = M_MAX_UNSIGNED;

	/// %Octree octant
	class URHO3D_API Octant : public SceneCell
	{
	public:
		/// Construct.
		Octant(const BoundingBox& box, unsigned level, Octant* parent, Octree* root, unsigned index = ROOT_INDEX);
		/// Destruct. Move drawables to root if available (detach if not) and free child octants.
		virtual ~Octant();

		/// Return or create a child octant.
		Octant* GetOrCreateChild(unsigned index);
		/// Delete child octant.
		void DeleteChild(unsigned index);
		/// Insert a drawable object by checking for fit recursively.
		void InsertDrawable(Drawable* drawable);
		/// Check if a drawable object fits.
		bool CheckDrawableFit(const BoundingBox& box) const;

		/// Add a drawable object to this octant.
		void AddDrawable(Drawable* drawable)
		{
			drawable->SetOctant(this);
			drawables_.Push(drawable);
			IncDrawableCount();
		}

		/// Remove a drawable object from this octant.
		void RemoveDrawable(Drawable* drawable, bool resetOctant = true)
		{
			if (drawables_.Remove(drawable))
			{
				if (resetOctant)
					drawable->SetOctant(nullptr);
				DecDrawableCount();
			}
		}

		/// Return world-space bounding box.
		const BoundingBox& GetWorldBoundingBox() const { return worldBoundingBox_; }

		/// Return bounding box used for fitting drawable objects.
		const BoundingBox& GetCullingBox() const { return cullingBox_; }

		/// Return subdivision level.
		unsigned GetLevel() const { return level_; }

		/// Return parent octant.
		Octant* GetParent() const { return parent_; }

		/// Return octree root.
		Octree* GetRoot() const { return root_; }

        virtual SceneManager* GetSceneManager() const override;

		/// Return number of drawables.
		unsigned GetNumDrawables() const { return numDrawables_; }

		/// Return true if there are no drawable objects in this octant and child octants.
		bool IsEmpty() { return numDrawables_ == 0; }

		/// Reset root pointer recursively. Called when the whole octree is being destroyed.
		void ResetRoot();
		/// Draw bounds to the debug graphics recursively.
		void DrawDebugGeometry(DebugRenderer* debug, bool depthTest);

        void Treadmill(int x, int y, int z, int level);

	protected:
		/// Initialize bounding box.
		void Initialize(const BoundingBox& box);
		/// Return drawable objects by a query, called internally.
		void GetDrawablesInternal(OctreeQuery& query, bool inside) const;
		/// Return drawable objects by a ray query, called internally.
		void GetDrawablesInternal(RayOctreeQuery& query) const;
		/// Return drawable objects only for a threaded ray query, called internally.
		void GetDrawablesOnlyInternal(RayOctreeQuery& query, PODVector<Drawable*>& drawables) const;

		/// Increase drawable object count recursively.
		void IncDrawableCount()
		{
			++numDrawables_;
			if (parent_)
				parent_->IncDrawableCount();
		}

		/// Decrease drawable object count recursively and remove octant if it becomes empty.
		void DecDrawableCount()
		{
			Octant* parent = parent_;

			--numDrawables_;
			if (!numDrawables_)
			{
				if (parent)
					parent->DeleteChild(index_);
			}

			if (parent)
				parent->DecDrawableCount();
		}

		/// World bounding box.
		BoundingBox worldBoundingBox_;
		/// Bounding box used for drawable object fitting.
		BoundingBox cullingBox_;
		/// Drawable objects.
		PODVector<Drawable*> drawables_;
		/// Child octants.
		Octant* children_[NUM_OCTANTS];
		/// World bounding box center.
		Vector3 center_;
		/// World bounding box half size.
		Vector3 halfSize_;
		/// Subdivision level.
		unsigned level_;
		/// Number of drawable objects in this octant and child octants.
		unsigned numDrawables_;
		/// Parent octant.
		Octant* parent_;
		/// Octree root.
		Octree* root_;
		/// Octant index relative to its siblings or ROOT_INDEX for root octant
		unsigned index_;

        mutable SceneManager* cachedSceneManager_ = nullptr;

        friend class TileSceneManager;
        void Shift(Vector3 amount);
	};

}