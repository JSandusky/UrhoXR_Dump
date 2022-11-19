#include <Urho3D/Graphics/SceneManager.h>
#include <Urho3D/Graphics/Octree.h>
#include "../IO/VectorBuffer.h"

#include <atomic>

namespace Urho3D
{

	class URHO3D_API TileSceneManager : public SceneManager
	{
		URHO3D_OBJECT(TileSceneManager, SceneManager);
	public:
		TileSceneManager(Context*);
		virtual ~TileSceneManager();

        static void Register(Context*);

        void Init(IntVector2 gridSize, int distance = 2, float cellSize = 128.0f);

		/// Return drawable objects by a query.
		virtual void GetDrawables(OctreeQuery& query) const override;
		/// Return drawable objects by a ray query.
		virtual void Raycast(RayOctreeQuery& query) const override;
		/// Return the closest drawable object by a ray query.
		virtual void RaycastSingle(RayOctreeQuery& query) const override;

		/// Visualize the component as debug geometry.
		virtual void DrawDebugGeometry(bool depthTest) override;

        enum LoadStatus
        {
            LS_UNLOADED,
            LS_STREAMING,
            LS_LOADED,
            LS_PERSISTING,
            LS_PERSIST_FINISHED
        };

        struct Cell {
            SharedPtr<Octree> octree_;
            SharedPtr<Node> node_;
            IntVector2 position_;
            std::atomic<LoadStatus> loaded_;
            
            VectorBuffer loadData_;
            std::atomic<int> fileDataLoaded_;
        };

        virtual void Update(const FrameInfo& frame) override;

        virtual void AddDrawable(Drawable* drawable) override;
        virtual void InsertDrawable(Drawable* drawable) override;

        void UpdateCamera(Camera* camera, bool isTeleport);

    private:
        void LoadCell(Cell*, bool threaded);
        void UnloadCell(Cell*);
        void SaveCell(Cell*);
        void SaveCellImmediate(Cell*);

        void HandleRenderUpdate(StringHash eventType, VariantMap& eventData);
        
        static String CellFileName(Cell*);
        static void Thread_SaveTile(const WorkItem*, unsigned);
        static void Thread_LoadTile(const WorkItem*, unsigned);

        Vector<Cell*> cells_;
        IntVector2 gridSize_;
        int distance_ = 2;
        int persistDistance_ = 3;
        float cellSize_ = 128.0f;

        IntVector2 position_;
        IntVector2 offsets_;
	};

}