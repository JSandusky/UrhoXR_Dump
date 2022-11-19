//
// Copyright (c) 2008-2018 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "../Container/List.h"
#include "../Core/Mutex.h"
#include "../Graphics/Drawable.h"
#include "../Graphics/Octant.h"
#include "../Graphics/OctreeQuery.h"
#include "../Graphics/SceneManager.h"

namespace Urho3D
{

/// %Octree component. Should be added only to the root scene node
class URHO3D_API Octree : public SceneManager, public Octant
{
    friend class TileSceneManager;
    friend void RaycastDrawablesWork(const WorkItem* item, unsigned threadIndex);

    URHO3D_OBJECT(Octree, Component);

public:
    /// Construct.
    explicit Octree(Context* context);
    /// Destruct.
    ~Octree() override;
    /// Register object factory.
    static void RegisterObject(Context* context);

    /// Visualize the component as debug geometry.
    void DrawDebugGeometry(DebugRenderer* debug, bool depthTest) override;

    /// Set size and maximum subdivision levels. If octree is not empty, drawable objects will be temporarily moved to the root.
    void SetSize(const BoundingBox& box, unsigned numLevels);

    /// Return drawable objects by a query.
    void GetDrawables(OctreeQuery& query) const override;
    /// Return drawable objects by a ray query.
    void Raycast(RayOctreeQuery& query) const override;
    /// Return the closest drawable object by a ray query.
    void RaycastSingle(RayOctreeQuery& query) const override;

    /// Return subdivision levels.
    unsigned GetNumLevels() const { return numLevels_; }

    /// Visualize the component as debug geometry.
    void DrawDebugGeometry(bool depthTest);

    void AddDrawable(Drawable* d) { Octant::AddDrawable(d); }
    void InsertDrawable(Drawable* drawable) { Octant::InsertDrawable(drawable); }

private:
    /// Handle render update in case of headless execution.
    void HandleRenderUpdate(StringHash eventType, VariantMap& eventData);
    /// Update octree size.
    void UpdateOctreeSize() { SetSize(worldBoundingBox_, numLevels_); }

    /// Ray query temporary list of drawables.
    mutable PODVector<Drawable*> rayQueryDrawables_;
    /// Subdivision level.
    unsigned numLevels_;
};

}
