#pragma once

#include <Urho3D/Scene/Component.h>
#include <Urho3D/Graphics/OctreeQuery.h>
#include "../Core/Mutex.h"

namespace Urho3D
{

class Octant;
class Octree;
class Node;

/// Base class for scene structures, such as the standard octree or the streamer.
class URHO3D_API SceneManager : public Component
{
	URHO3D_OBJECT(SceneManager, Component);
public:
	explicit SceneManager(Context*);
	virtual ~SceneManager();
	static void Register(Context*);

	/// Return drawable objects by a query.
	virtual void GetDrawables(OctreeQuery& query) const abstract;
	/// Return drawable objects by a ray query.
	virtual void Raycast(RayOctreeQuery& query) const abstract;
	/// Return the closest drawable object by a ray query.
	virtual void RaycastSingle(RayOctreeQuery& query) const abstract;

	/// Mark drawable object as requiring an update and a reinsertion.
	virtual void QueueUpdate(Drawable* drawable);
	/// Cancel drawable object's update.
	virtual void CancelUpdate(Drawable* drawable);
	/// Visualize the component as debug geometry.
	virtual void DrawDebugGeometry(bool depthTest) abstract;

    /// Update and reinsert drawable objects.
    virtual void Update(const FrameInfo& frame);
    /// Add a drawable manually.
    void AddManualDrawable(Drawable* drawable);
    /// Remove a manually added drawable.
    void RemoveManualDrawable(Drawable* drawable);

    virtual void AddDrawable(Drawable* drawable) = 0;
    virtual void InsertDrawable(Drawable* drawable) = 0;

protected:
    /// Drawable objects that require update.
    PODVector<Drawable*> drawableUpdates_;
    /// Drawable objects that were inserted during threaded update phase.
    PODVector<Drawable*> threadedDrawableUpdates_;
    /// Mutex for octree reinsertions.
    Mutex octreeMutex_;
};

}