#include "../Precompiled.h"
#include "../Graphics/DebugRenderer.h"
#include "../Graphics/Octant.h"
#include "../Graphics/Octree.h"
#include "../Scene/Scene.h"

namespace Urho3D
{


	Octant::Octant(const BoundingBox& box, unsigned level, Octant* parent, Octree* root, unsigned index) :
		level_(level),
		numDrawables_(0),
		parent_(parent),
		root_(root),
		index_(index)
	{
		Initialize(box);

		for (auto& child : children_)
			child = nullptr;
	}

	Octant::~Octant()
	{
		if (root_)
		{
			// Remove the drawables (if any) from this octant to the root octant
			for (PODVector<Drawable*>::Iterator i = drawables_.Begin(); i != drawables_.End(); ++i)
			{
				(*i)->SetOctant(root_);
				root_->drawables_.Push(*i);
				root_->QueueUpdate(*i);
			}
			drawables_.Clear();
			numDrawables_ = 0;
		}

		for (unsigned i = 0; i < NUM_OCTANTS; ++i)
			DeleteChild(i);
	}

	Octant* Octant::GetOrCreateChild(unsigned index)
	{
		if (children_[index])
			return children_[index];

		Vector3 newMin = worldBoundingBox_.min_;
		Vector3 newMax = worldBoundingBox_.max_;
		Vector3 oldCenter = worldBoundingBox_.Center();

		if (index & 1)
			newMin.x_ = oldCenter.x_;
		else
			newMax.x_ = oldCenter.x_;

		if (index & 2)
			newMin.y_ = oldCenter.y_;
		else
			newMax.y_ = oldCenter.y_;

		if (index & 4)
			newMin.z_ = oldCenter.z_;
		else
			newMax.z_ = oldCenter.z_;

		children_[index] = new Octant(BoundingBox(newMin, newMax), level_ + 1, this, root_, index);
		return children_[index];
	}

	void Octant::DeleteChild(unsigned index)
	{
		assert(index < NUM_OCTANTS);
		delete children_[index];
		children_[index] = nullptr;
	}

	void Octant::InsertDrawable(Drawable* drawable)
	{
		const BoundingBox& box = drawable->GetWorldBoundingBox();

		// If root octant, insert all non-occludees here, so that octant occlusion does not hide the drawable.
		// Also if drawable is outside the root octant bounds, insert to root
		bool insertHere;
		if (this == root_)
			insertHere = !drawable->IsOccludee() || cullingBox_.IsInside(box) != INSIDE || CheckDrawableFit(box);
		else
			insertHere = CheckDrawableFit(box);

		if (insertHere)
		{
			SceneCell* oldOctant = drawable->octant_;
			if (oldOctant != this)
			{
				// Add first, then remove, because drawable count going to zero deletes the octree branch in question
				AddDrawable(drawable);
				if (oldOctant)
					oldOctant->RemoveDrawable(drawable, false);
			}
		}
		else
		{
			Vector3 boxCenter = box.Center();
			unsigned x = boxCenter.x_ < center_.x_ ? 0 : 1;
			unsigned y = boxCenter.y_ < center_.y_ ? 0 : 2;
			unsigned z = boxCenter.z_ < center_.z_ ? 0 : 4;

			GetOrCreateChild(x + y + z)->InsertDrawable(drawable);
		}
	}

	bool Octant::CheckDrawableFit(const BoundingBox& box) const
	{
		Vector3 boxSize = box.Size();

		// If max split level, size always OK, otherwise check that box is at least half size of octant
		if (level_ >= root_->GetNumLevels() || boxSize.x_ >= halfSize_.x_ || boxSize.y_ >= halfSize_.y_ ||
			boxSize.z_ >= halfSize_.z_)
			return true;
		// Also check if the box can not fit a child octant's culling box, in that case size OK (must insert here)
		else
		{
			if (box.min_.x_ <= worldBoundingBox_.min_.x_ - 0.5f * halfSize_.x_ ||
				box.max_.x_ >= worldBoundingBox_.max_.x_ + 0.5f * halfSize_.x_ ||
				box.min_.y_ <= worldBoundingBox_.min_.y_ - 0.5f * halfSize_.y_ ||
				box.max_.y_ >= worldBoundingBox_.max_.y_ + 0.5f * halfSize_.y_ ||
				box.min_.z_ <= worldBoundingBox_.min_.z_ - 0.5f * halfSize_.z_ ||
				box.max_.z_ >= worldBoundingBox_.max_.z_ + 0.5f * halfSize_.z_)
				return true;
		}

		// Bounding box too small, should create a child octant
		return false;
	}

    SceneManager* Octant::GetSceneManager() const
    {
        return GetRoot();
    }

	void Octant::ResetRoot()
	{
		root_ = nullptr;

		// The whole octree is being destroyed, just detach the drawables
		for (PODVector<Drawable*>::Iterator i = drawables_.Begin(); i != drawables_.End(); ++i)
			(*i)->SetOctant(nullptr);

		for (auto& child : children_)
		{
			if (child)
				child->ResetRoot();
		}
	}

	void Octant::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
	{
		if (debug && debug->IsInside(worldBoundingBox_))
		{
			debug->AddBoundingBox(worldBoundingBox_, Color(0.25f, 0.25f, 0.25f), depthTest);

			for (auto& child : children_)
			{
				if (child)
					child->DrawDebugGeometry(debug, depthTest);
			}
		}
	}

	void Octant::Initialize(const BoundingBox& box)
	{
		worldBoundingBox_ = box;
		center_ = box.Center();
		halfSize_ = 0.5f * box.Size();
		cullingBox_ = BoundingBox(worldBoundingBox_.min_ - halfSize_, worldBoundingBox_.max_ + halfSize_);
	}

	void Octant::GetDrawablesInternal(OctreeQuery& query, bool inside) const
	{
		if (this != root_)
		{
			Intersection res = query.TestOctant(cullingBox_, inside);
			if (res == INSIDE)
				inside = true;
			else if (res == OUTSIDE)
			{
				// Fully outside, so cull this octant, its children & drawables
				return;
			}
		}

		if (drawables_.Size())
		{
			auto** start = const_cast<Drawable**>(&drawables_[0]);
			Drawable** end = start + drawables_.Size();
			query.TestDrawables(start, end, inside);
		}

		for (auto child : children_)
		{
			if (child)
				child->GetDrawablesInternal(query, inside);
		}
	}

	void Octant::GetDrawablesInternal(RayOctreeQuery& query) const
	{
		float octantDist = query.ray_.HitDistance(cullingBox_);
		if (octantDist >= query.maxDistance_)
			return;

		if (drawables_.Size())
		{
			auto** start = const_cast<Drawable**>(&drawables_[0]);
			Drawable** end = start + drawables_.Size();

			while (start != end)
			{
				Drawable* drawable = *start++;

				if ((drawable->GetDrawableFlags() & query.drawableFlags_) && (drawable->GetViewMask() & query.viewMask_))
					drawable->ProcessRayQuery(query, query.result_);
			}
		}

		for (auto child : children_)
		{
			if (child)
				child->GetDrawablesInternal(query);
		}
	}

	void Octant::GetDrawablesOnlyInternal(RayOctreeQuery& query, PODVector<Drawable*>& drawables) const
	{
		float octantDist = query.ray_.HitDistance(cullingBox_);
		if (octantDist >= query.maxDistance_)
			return;

		if (drawables_.Size())
		{
			auto** start = const_cast<Drawable**>(&drawables_[0]);
			Drawable** end = start + drawables_.Size();

			while (start != end)
			{
				Drawable* drawable = *start++;

				if ((drawable->GetDrawableFlags() & query.drawableFlags_) && (drawable->GetViewMask() & query.viewMask_))
					drawables.Push(drawable);
			}
		}

		for (auto child : children_)
		{
			if (child)
				child->GetDrawablesOnlyInternal(query, drawables);
		}
	}

    void Octant::Treadmill(int x, int y, int z, int level)
    {
        if (level < level_ - 1)
        {
            for (auto child : children_)
                child->Treadmill(x, y, z, level);
        }
        else
        {
            for (auto child : children_)
            {

            }
        }
    }

    void Octant::Shift(Vector3 shiftAmount)
    {
        worldBoundingBox_.min_ += shiftAmount;
        worldBoundingBox_.max_ += shiftAmount;
        Initialize(worldBoundingBox_);

        for (auto child : children_)
            if (child)
                child->Shift(shiftAmount);
    }
}