#include "../StructuredBuffer.h"
#include "../Graphics.h"
#include "../../Graphics/GraphicsEvents.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../IO/Log.h"
#include "../../Graphics/Renderer.h"
#include "../../Resource/ResourceCache.h"

namespace Urho3D
{

    void StructuredBuffer::OnDeviceLost()
    {
        GPUObject::OnDeviceLost();
    }

    void StructuredBuffer::OnDeviceReset()
    {
        if (!object_.name_ || dataPending_)
        {
            // If has a resource file, reload through the resource cache. Otherwise just recreate.
            auto* cache = GetSubsystem<ResourceCache>();
            if (cache->Exists(GetName()))
                dataLost_ = !cache->ReloadResource(this);

            if (!object_.name_)
            {
                Create();
                dataLost_ = true;
            }
        }

        dataPending_ = false;
    }

    void StructuredBuffer::Release()
    {
        if (object_.name_)
        {
            if (!graphics_)
                return;

            if (!graphics_->IsDeviceLost())
            {
                for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
                {
                    if (graphics_->GetTexture(i) == this)
                        graphics_->SetTexture(i, nullptr);
                }

                glDeleteTextures(1, &object_.name_);
            }

            object_.name_ = 0;
        }

        resolveDirty_ = false;
        levelsDirty_ = false;
    }

    bool StructuredBuffer::Create()
    {
        Release();

        if (!graphics_ || !dataSize_ || !structSize_)
            return false;

        if (graphics_->IsDeviceLost())
        {
            URHO3D_LOGWARNING("Texture creation while device is lost");
            return true;
        }

        return true;
    }

    bool StructuredBuffer::SetData(unsigned char* data, unsigned dataSize)
    {
        return false;
    }

    bool StructuredBuffer::GetData(void* data, unsigned recordStart, unsigned recordCt)
    {
        return false;
    }

}