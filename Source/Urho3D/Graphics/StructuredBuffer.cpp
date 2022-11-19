#include "../Precompiled.h"
#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/GraphicsImpl.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Core/Profiler.h>
#include <Urho3D/Graphics/StructuredBuffer.h>

namespace Urho3D
{

    /// Construct.
    StructuredBuffer::StructuredBuffer(Context* ctx) : 
        Texture(ctx),
        dataSize_(0), 
        structSize_(0)
    {
#ifdef URHO3D_OPENGL
        // nothing to do here
#else
		uav_ = nullptr;
#endif
    }

    StructuredBuffer::~StructuredBuffer()
    {
        Release();
    }
    
    void StructuredBuffer::RegisterObject(Context* context)
    {
        context->RegisterFactory<StructuredBuffer>();
    }

    bool StructuredBuffer::SetSize(unsigned dataSize, unsigned structStride)
    {
        // Don't recreate if resizing to the same size.
        if (dataSize == dataSize_ && structStride == structSize_)
            return true;

        dataSize_ = dataSize;
        structSize_ = structStride;
        return Create();
    }

}