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

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Graphics/Batch.h"
#include "../Graphics/Camera.h"
#include "../Graphics/Skybox.h"
#include "../Scene/Node.h"

#include "../DebugNew.h"

#pragma optimize("", off)

namespace Urho3D
{

extern const char* GEOMETRY_CATEGORY;

Skybox::Skybox(Context* context) :
    StaticModel(context),
    lastFrame_(0)
{
}

Skybox::~Skybox() = default;

void Skybox::RegisterObject(Context* context)
{
    context->RegisterFactory<Skybox>(GEOMETRY_CATEGORY);

    URHO3D_COPY_BASE_ATTRIBUTES(StaticModel);
    URHO3D_ATTRIBUTE("Path Test", String, junk_, "", AM_DEFAULT)
        //.SetMetadata(AttributeMetadata::P_HEADER, true)
        .SetMetadata(AttributeMetadata::P_SPLIT_BEFORE, true)
        .SetMetadata(AttributeMetadata::P_INFO, String("This does some really cool stuff, I totally swear it because it's awesome!"))
        .SetMetadata(AttributeMetadata::P_IS_APP_PATH, true)
        .SetMetadata(AttributeMetadata::P_SPLIT_AFTER, true)
        .SetMetadata(AttributeMetadata::P_HEADER, true);
    URHO3D_ATTRIBUTE("PathList Test", StringVector, junk2_, StringVector(), AM_DEFAULT)
        .SetMetadata(AttributeMetadata::P_IS_PATH, true);
}

void Skybox::ProcessRayQuery(const RayOctreeQuery& query, PODVector<RayQueryResult>& results)
{
    // Do not record a raycast result for a skybox, as it would block all other results
}

void Skybox::UpdateBatches(const FrameInfo& frame)
{
    distance_ = 0.0f;

    if (frame.frameNumber_ != lastFrame_)
    {
        customWorldTransforms_.Clear();
        lastFrame_ = frame.frameNumber_;
    }

    // Add camera position to fix the skybox in space. Use effective world transform to take reflection into account
    Matrix3x4 customWorldTransform = node_->GetWorldTransform();
    customWorldTransform.SetTranslation(node_->GetWorldPosition() + frame.camera_->GetEffectiveWorldTransform().Translation());
    HashMap<Camera*, Matrix3x4>::Iterator it = customWorldTransforms_.Insert(MakePair(frame.camera_, customWorldTransform));

    for (unsigned i = 0; i < batches_.Size(); ++i)
    {
        batches_[i].worldTransform_ = &it->second_;
        batches_[i].distance_ = 0.0f;
    }
}

void Skybox::OnWorldBoundingBoxUpdate()
{
    // The skybox is supposed to be visible everywhere, so set a humongous bounding box
    worldBoundingBox_.Define(-M_LARGE_VALUE, M_LARGE_VALUE);
}

}
