#include "../Precompiled.h"

#include "TileSceneManager.h"
#include "Camera.h"
#include "../Scene/Node.h"
#include "../Scene/Scene.h"
#include "../Physics/PhysicsWorld.h"
#include "../Resource/ResourceCache.h"
#include "../IO/FileSystem.h"
#include "../Core/WorkQueue.h"
#include "../Core/Context.h"
#include "../Graphics/Graphics.h"
#include "../Core/CoreEvents.h"

namespace Urho3D
{

    #define CELLS_PATH "Data/Tiles/"

    void TileSceneManager::Thread_LoadTile(const WorkItem* item, unsigned thread)
    {
        Cell* cell = (Cell*)item->aux_;

        auto ctx = cell->node_->GetContext();

        auto dir = ctx->GetSubsystem<FileSystem>()->GetProgramDir();
        dir = AddTrailingSlash(dir);
        dir += CellFileName(cell);

        File file(ctx, dir, FILE_READ);
        auto size = file.ReadUInt64();
        cell->loadData_.Resize(size);
        file.Read(cell->loadData_.GetBuffer().Buffer(), size);
        cell->fileDataLoaded_ = 1;
    }

    void TileSceneManager::Thread_SaveTile(const WorkItem* item, unsigned thread)
    {
        Cell* cell = (Cell*)item->aux_;

        auto ctx = cell->node_->GetContext();

        auto dir = ctx->GetSubsystem<FileSystem>()->GetProgramDir();
        dir = AddTrailingSlash(dir);
        dir += CellFileName(cell);

        File file(ctx, dir, FILE_READ);
        cell->node_->Save(file);
        file.Close();
    }

    void TileSceneManager::SaveCellImmediate(Cell* cell)
    {
        auto ctx = cell->node_->GetContext();

        auto dir = ctx->GetSubsystem<FileSystem>()->GetProgramDir();
        dir = AddTrailingSlash(dir);
        dir += CellFileName(cell);

        File file(ctx, dir, FILE_READ);
        cell->node_->Save(file);
        file.Close();
    }

    TileSceneManager::TileSceneManager(Context* context) :
        SceneManager(context)
    {
        if (!GetSubsystem<Graphics>())
            SubscribeToEvent(E_RENDERUPDATE, URHO3D_HANDLER(TileSceneManager, HandleRenderUpdate));
    }

    TileSceneManager::~TileSceneManager()
    {
        for (auto c : cells_)
            delete c;
        cells_.Clear();
    }

    void TileSceneManager::Register(Context* ctx)
    {
        ctx->RegisterFactory<TileSceneManager>();
    }

    void TileSceneManager::Init(IntVector2 gridSize, int distance, float cellSize)
    {
        distance_ = distance;
        cellSize_ = cellSize;
        gridSize_ = gridSize;

        cells_.Resize(gridSize.x_ * gridSize.y_);
        auto scene = GetScene();

        for (int y = 0; y < gridSize.y_; ++y)
        {
            for (int x = 0; x < gridSize.x_; ++x)
            {
                const int idx = y * gridSize_.x_ + x;
                cells_[idx] = new Cell { 
                    SharedPtr<Octree>(new Octree(GetContext())), 
                    SharedPtr<Node>(scene->CreateChild()),
                    IntVector2(x, y), 
                    LS_UNLOADED 
                };

                BoundingBox box;
                box.min_.x_ = x * cellSize_;
                box.min_.z_ = y * cellSize_;
                box.min_.y_ = -1000.0f;
                box.max_.x_ = box.min_.x_ + cellSize_;
                box.max_.z_ = box.min_.z_ + cellSize_;
                box.min_.y_ = 1000.0f;
                cells_[idx]->octree_->SetSize(box, 6);

                String nodeName;
                nodeName.AppendWithFormat("Tile %u, %u", x, y);
                cells_[idx]->node_->SetName(nodeName);
                cells_[idx]->node_->AddTag("tile");
            }
        }
    }

    void TileSceneManager::GetDrawables(OctreeQuery& query) const
    {
        for (auto c : cells_)
        {
            if (c->loaded_ == LS_LOADED)
            {
                if (query.TestOctant(c->octree_->GetWorldBoundingBox(), false) != OUTSIDE)
                    c->octree_->GetDrawables(query);
            }
        }
    }

    void TileSceneManager::Raycast(RayOctreeQuery& query) const
    {
        for (auto c : cells_)
        {
            if (c->loaded_ == LS_LOADED)
            {
                float octantDist = query.ray_.HitDistance(c->octree_->GetWorldBoundingBox());
                if (octantDist >= query.maxDistance_)
                    continue;

                c->octree_->Raycast(query);
            }
        }
    }
    
    void TileSceneManager::RaycastSingle(RayOctreeQuery& query) const
    {
        for (auto c : cells_)
        {
            if (c->loaded_ == LS_LOADED)
            {
                float octantDist = query.ray_.HitDistance(c->octree_->GetWorldBoundingBox());
                if (octantDist >= query.maxDistance_)
                    continue;

                c->octree_->RaycastSingle(query);
            }
        }
    }

    void TileSceneManager::DrawDebugGeometry(bool depthTest)
    {
        for (auto c : cells_)
        {
            if (c->loaded_ == LS_LOADED)
                c->octree_->DrawDebugGeometry(depthTest);
        }
    }

    void TileSceneManager::Update(const FrameInfo& frame)
    {
        SceneManager::Update(frame);
    }

    void TileSceneManager::UpdateCamera(Camera* camera, bool isTeleport)
    {
        bool anyLoaded = false;

        for (auto c : cells_)
        {
            if (c->loaded_ == LS_LOADED)
            {
                anyLoaded = true;
                break;
            }
        }

        for (auto c : cells_)
        {
            if (c->loaded_ == LS_STREAMING && c->fileDataLoaded_)
            {
                c->node_->Load(c->loadData_);
                c->fileDataLoaded_ = 0;
                c->loaded_ = LS_LOADED;
                c->loadData_.Clear();
            }
        }

        if (camera)
        {
            auto camNode = camera->GetNode();
            auto worldPos = camNode->GetWorldPosition();

            IntVector2 shiftBy;
            while (worldPos.x_ < 0)
                shiftBy.x_ -= 1;
            while (worldPos.x_ > cellSize_)
                shiftBy.x_ += 1;
            while (worldPos.z_ < 0)
                shiftBy.y_ -= 1;
            while (worldPos.z_ > cellSize_)
                shiftBy.y_ += 1;

            // verify bounds
            if (position_.x_ + shiftBy.x_ > gridSize_.x_ - 1 || position_.x_ + shiftBy.x_ < 0)
                shiftBy.x_ = 0;
            if (position_.y_ + shiftBy.y_ > gridSize_.y_ - 1 || position_.y_ + shiftBy.y_ < 0)
                shiftBy.y_ = 0;

            // do we need to shift the world?
            if (shiftBy.x_ != 0 && shiftBy.y_ != 0)
            {
                position_ += shiftBy;

                Vector3 shiftAmount = Vector3(shiftBy.x_ * cellSize_, 0, shiftBy.y_ * cellSize_);

                auto phyWorld = GetScene()->GetComponent<PhysicsWorld>();
                phyWorld->SetSuspendActivation(true);
                phyWorld->ShiftOrigin(shiftAmount);

                for (auto c : cells_)
                {
                    if (c->loaded_)
                    {
                        c->octree_->Shift(shiftAmount);

                        // shift objects and check parenting
                        auto nodes = c->node_->GetChildren(true);
                        c->node_->SetWorldPosition(c->node_->GetWorldPosition() + shiftAmount);
                        for (auto node : nodes)
                        {
                            auto worldPos = node->GetWorldPosition();
                            IntVector2 pos = { (int)floorf(worldPos.x_ / cellSize_), (int)floorf(worldPos.z_ / cellSize_) };
                            const int idx = pos.x_ + pos.y_ * gridSize_.x_;
                            if (node->GetParent() != cells_[idx]->node_)
                                node->SetParent(cells_[idx]->node_);
                        }
                    }
                }

                phyWorld->SetSuspendActivation(false);
            }
            else
            {
                // didn't need to shift the world, just check dynamic object shifts
                for (auto c : cells_)
                {
                    if (c->loaded_)
                    {
                        auto nodes = c->node_->GetChildren(true);
                        for (auto node : nodes)
                        {
                            auto worldPos = node->GetWorldPosition();
                            IntVector2 pos = { (int)floorf(worldPos.x_ / cellSize_), (int)floorf(worldPos.z_ / cellSize_) };

                            const int idx = pos.x_ + pos.y_ * gridSize_.x_;
                            if (node->GetParent() != cells_[idx]->node_)
                                node->SetParent(cells_[idx]->node_);
                        }
                    }
                }
            }
        }

        for (auto cell : cells_)
        {
            // reminder, position is in tile-space
            const auto posDiff = position_ - cell->position_;
            const auto diffX = Abs(posDiff.x_);
            const auto diffY = Abs(posDiff.y_);

            // avoid any risk of atomic divergence in tests
            const auto loadState = cell->loaded_.load();
            if (diffX <= distance_ && diffY <= distance_) // in range?
            {
                if (loadState != LS_STREAMING && loadState != LS_LOADED)
                    LoadCell(cell, anyLoaded && !isTeleport);
            }
            else
            {
                if (loadState == LS_LOADED || (loadState == LS_PERSISTING && (diffX >= persistDistance_ || diffY >= persistDistance_)))
                {
                    UnloadCell(cell); // queues persistance if LS_LOADED
                }
            }
        }
    }

    void TileSceneManager::AddDrawable(Drawable* drawable)
    {
        auto worldPos = drawable->GetNode()->GetWorldPosition();
        IntVector2 pos = { (int)floorf(worldPos.x_ / cellSize_), (int)floorf(worldPos.z_ / cellSize_) };

        pos += position_;

        int idx = pos.y_ * gridSize_.x_ + pos.x_;
        cells_[idx]->octree_->AddDrawable(drawable);
    }

    void TileSceneManager::InsertDrawable(Drawable* drawable)
    {
        auto worldPos = drawable->GetNode()->GetWorldPosition();
        IntVector2 pos = { (int)floorf(worldPos.x_ / cellSize_), (int)floorf(worldPos.z_ / cellSize_) };

        pos += position_;

        int idx = pos.y_ * gridSize_.x_ + pos.x_;
        cells_[idx]->octree_->InsertDrawable(drawable);
    }

    void TileSceneManager::LoadCell(Cell* cell, bool threaded)
    {
        if (threaded)
        {
            SharedPtr<WorkItem> item(new WorkItem());
            item->workFunction_ = Thread_LoadTile;
            item->aux_ = cell;
            cell->fileDataLoaded_ = 0;
            cell->loaded_ = LS_STREAMING;
            GetSubsystem<WorkQueue>()->AddWorkItem(item);
        }
        else // force the load, will be called if we have any unloaded
        {
            auto ctx = cell->node_->GetContext();

            auto dir = ctx->GetSubsystem<FileSystem>()->GetProgramDir();
            dir = AddTrailingSlash(dir);
            dir += CellFileName(cell);

            File file(ctx, dir, FILE_READ);
            auto size = file.ReadUInt64();
            cell->loadData_.Resize(size);
            file.Read(cell->loadData_.GetBuffer().Buffer(), size);
            file.Close();

            cell->node_->Load(cell->loadData_);

            cell->fileDataLoaded_ = 0;
            cell->loaded_ = LS_LOADED;
            cell->loadData_.Clear();
        }
    }

    void TileSceneManager::SaveCell(Cell* cell)
    {
        SharedPtr<WorkItem> item(new WorkItem());
        item->workFunction_ = Thread_SaveTile;
        item->aux_ = cell;
        GetSubsystem<WorkQueue>()->AddWorkItem(item);
    }

    void TileSceneManager::UnloadCell(Cell* cell)
    {
        if (cell->loaded_ == LS_LOADED)
        {
            cell->loaded_ = LS_PERSISTING;
        }
        else
        {
            cell->node_->RemoveAllChildren();
            cell->loaded_ = LS_UNLOADED;
        }
    }

    String TileSceneManager::CellFileName(Cell* cell)
    {
        String fn;
        fn.AppendWithFormat(CELLS_PATH "%u_%u.cel", cell->position_.x_, cell->position_.y_);
        return fn;
    }

    void TileSceneManager::HandleRenderUpdate(StringHash eventType, VariantMap& eventData)
    {
        // When running in headless mode, update the Octree manually during the RenderUpdate event
        Scene* scene = GetScene();
        if (!scene || !scene->IsUpdateEnabled())
            return;

        using namespace RenderUpdate;

        FrameInfo frame;
        frame.frameNumber_ = GetSubsystem<Time>()->GetFrameNumber();
        frame.timeStep_ = eventData[P_TIMESTEP].GetFloat();
        frame.camera_ = nullptr;

        Update(frame);
    }
}
