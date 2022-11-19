#include "../Precompiled.h"
#include <Urho3D/Graphics/SceneManager.h>
#include <Urho3D/Core/Context.h>
#include "../Core/Thread.h"
#include "../IO/Log.h"
#include "../Core/Profiler.h"
#include "../Core/WorkQueue.h"
#include "../Scene/Scene.h"
#include "../Scene/SceneEvents.h"
#include "../Scene/SceneCell.h"

namespace Urho3D
{

    void UpdateDrawablesWork(const WorkItem* item, unsigned threadIndex)
    {
        const FrameInfo& frame = *(reinterpret_cast<FrameInfo*>(item->aux_));
        auto** start = reinterpret_cast<Drawable**>(item->start_);
        auto** end = reinterpret_cast<Drawable**>(item->end_);

        while (start != end)
        {
            Drawable* drawable = *start;
            if (drawable)
                drawable->Update(frame);
            ++start;
        }
    }

    void UpdateDrawablesBBWork(const WorkItem* item, unsigned threadIndex)
    {
        Drawable** start = reinterpret_cast<Drawable**>(item->start_);
        Drawable** end = reinterpret_cast<Drawable**>(item->end_);

        while (start != end)
        {
            Drawable* drawable = *start;
            if (drawable)
                const BoundingBox& box = drawable->GetWorldBoundingBox();
            ++start;
        }
    }

	SceneManager::SceneManager(Context* ctx) : Component(ctx)
	{

	}

	SceneManager::~SceneManager()
	{

	}

	extern const char* SUBSYSTEM_CATEGORY;
	void SceneManager::Register(Context* ctx)
	{
		// we are not constructible
	}

    void SceneManager::Update(const FrameInfo& frame)
    {
        if (!Thread::IsMainThread())
        {
            URHO3D_LOGERROR("Octree::Update() can not be called from worker threads");
            return;
        }

        // Let drawables update themselves before reinsertion. This can be used for animation
        if (!drawableUpdates_.Empty())
        {
            URHO3D_PROFILE(UpdateDrawables);

            // Perform updates in worker threads. Notify the scene that a threaded update is going on and components
            // (for example physics objects) should not perform non-threadsafe work when marked dirty
            Scene* scene = GetScene();
            auto* queue = GetSubsystem<WorkQueue>();
            scene->BeginThreadedUpdate();

            int numWorkItems = queue->GetNumThreads() + 1; // Worker threads + main thread
            int drawablesPerItem = Max((int)(drawableUpdates_.Size() / numWorkItems), 1);

            PODVector<Drawable*>::Iterator start = drawableUpdates_.Begin();
            // Create a work item for each thread
            for (int i = 0; i < numWorkItems; ++i)
            {
                SharedPtr<WorkItem> item = queue->GetFreeItem();
                item->priority_ = M_MAX_UNSIGNED;
                item->workFunction_ = UpdateDrawablesWork;
                item->aux_ = const_cast<FrameInfo*>(&frame);

                PODVector<Drawable*>::Iterator end = drawableUpdates_.End();
                if (i < numWorkItems - 1 && end - start > drawablesPerItem)
                    end = start + drawablesPerItem;

                item->start_ = &(*start);
                item->end_ = &(*end);
                queue->AddWorkItem(item);

                start = end;
            }

            queue->Complete(M_MAX_UNSIGNED);
            scene->EndThreadedUpdate();
        }

        // If any drawables were inserted during threaded update, update them now from the main thread
        if (!threadedDrawableUpdates_.Empty())
        {
            URHO3D_PROFILE(UpdateDrawablesQueuedDuringUpdate);

            for (PODVector<Drawable*>::ConstIterator i = threadedDrawableUpdates_.Begin(); i != threadedDrawableUpdates_.End(); ++i)
            {
                Drawable* drawable = *i;
                if (drawable)
                {
                    drawable->Update(frame);
                    drawableUpdates_.Push(drawable);
                }
            }

            threadedDrawableUpdates_.Clear();
        }

        // Notify drawable update being finished. Custom animation (eg. IK) can be done at this point
        Scene* scene = GetScene();
        if (scene)
        {
            using namespace SceneDrawableUpdateFinished;

            VariantMap& eventData = GetEventDataMap();
            eventData[P_SCENE] = scene;
            eventData[P_TIMESTEP] = frame.timeStep_;
            scene->SendEvent(E_SCENEDRAWABLEUPDATEFINISHED, eventData);
        }

        // Reinsert drawables that have been moved or resized, or that have been newly added to the octree and do not sit inside
        // the proper octant yet
        if (!drawableUpdates_.Empty())
        {
            URHO3D_PROFILE(ReinsertToOctree);

            /*****parallel update world bounding box*****/
            auto* queue = GetSubsystem<WorkQueue>();
            if (queue->GetNumThreads() > 0)
            {
                int numWorkItems = queue->GetNumThreads() + 1; // Worker threads + main thread
                int drawablesPerItem = Max((int)(drawableUpdates_.Size() / numWorkItems), 1);
                auto start = drawableUpdates_.Begin();
                // Create a work item for each thread
                for (int i = 0; i < numWorkItems; ++i)
                {
                    auto item = queue->GetFreeItem();
                    item->priority_ = M_MAX_UNSIGNED;
                    item->workFunction_ = UpdateDrawablesBBWork;
                    item->aux_ = NULL;

                    auto end = drawableUpdates_.End();
                    if (i < numWorkItems - 1 && end - start > drawablesPerItem)
                        end = start + drawablesPerItem;

                    item->start_ = &(*start);
                    item->end_ = &(*end);
                    queue->AddWorkItem(item);
                    start = end;
                }
                queue->Complete(M_MAX_UNSIGNED);
            }

            for (PODVector<Drawable*>::Iterator i = drawableUpdates_.Begin(); i != drawableUpdates_.End(); ++i)
            {
                Drawable* drawable = *i;
                drawable->updateQueued_ = false;
                SceneCell* octant = drawable->GetOctant();
                const BoundingBox& box = drawable->GetWorldBoundingBox();

                // Skip if no octant or does not belong to this octree anymore
                if (!octant || octant->GetSceneManager() != this)
                    continue;
                // Skip if still fits the current octant
                if (drawable->IsOccludee() && octant->GetCullingBox().IsInside(box) == INSIDE && octant->CheckDrawableFit(box))
                    continue;

                InsertDrawable(drawable);

#ifdef _DEBUG
                // Verify that the drawable will be culled correctly
                octant = drawable->GetOctant();
                if (octant != this && octant->GetCullingBox().IsInside(box) != INSIDE)
                {
                    URHO3D_LOGERROR("Drawable is not fully inside its octant's culling bounds: drawable box " + box.ToString() +
                        " octant box " + octant->GetCullingBox().ToString());
                }
#endif
            }
        }

        drawableUpdates_.Clear();
    }

    void SceneManager::AddManualDrawable(Drawable* drawable)
    {
        if (!drawable || drawable->GetOctant())
            return;

        AddDrawable(drawable);
    }

    void SceneManager::RemoveManualDrawable(Drawable* drawable)
    {
        if (!drawable)
            return;

        SceneCell* octant = drawable->GetOctant();
        if (octant && octant->GetSceneManager() == this)
            octant->RemoveDrawable(drawable);
    }

    void SceneManager::QueueUpdate(Drawable* drawable)
    {
        Scene* scene = GetScene();
        if (scene && scene->IsThreadedUpdate())
        {
            MutexLock lock(octreeMutex_);
            threadedDrawableUpdates_.Push(drawable);
        }
        else
            drawableUpdates_.Push(drawable);

        drawable->updateQueued_ = true;
    }

    void SceneManager::CancelUpdate(Drawable* drawable)
    {
        // This doesn't have to take into account scene being in threaded update, because it is called only
        // when removing a drawable from octree, which should only ever happen from the main thread.
        drawableUpdates_.Remove(drawable);
        drawable->updateQueued_ = false;
    }

}
