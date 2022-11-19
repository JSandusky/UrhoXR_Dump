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

#include "../../Precompiled.h"

#include "../../Core/Context.h"
#include "../../Core/Profiler.h"
#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsEvents.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/Renderer.h"
#include "../../Graphics/Texture2D.h"
#include "../../IO/FileSystem.h"
#include "../../IO/Log.h"
#include "../../Resource/ResourceCache.h"
#include "../../Resource/XMLFile.h"

#include "../../DebugNew.h"

#include <nvapi/nvapi.h>

namespace Urho3D
{

void Texture2D::OnDeviceLost()
{
    // No-op on Direct3D11
}

void Texture2D::OnDeviceReset()
{
    // No-op on Direct3D11
}

void Texture2D::Release()
{
    VariantMap& eventData = GetEventDataMap();
    eventData[GPUResourceReleased::P_OBJECT] = this;
    SendEvent(E_GPURESOURCERELEASED, eventData);

    if (graphics_ && object_.ptr_)
    {
        for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        {
            if (graphics_->GetTexture(i) == this)
                graphics_->SetTexture(i, nullptr);
        }
    }

    if (vrsView_)
    {
        vrsView_->Release();
        vrsView_ = nullptr; // TODO: how to not leak without the helper?
    }

    if (renderSurface_)
        renderSurface_->Release();

    URHO3D_SAFE_RELEASE(object_.ptr_);
    URHO3D_SAFE_RELEASE(resolveTexture_);
    URHO3D_SAFE_RELEASE(shaderResourceView_);
    URHO3D_SAFE_RELEASE(sampler_);
	URHO3D_SAFE_RELEASE(uaView_);
}

bool Texture2D::SetData(unsigned level, int x, int y, int width, int height, const void* data)
{
    URHO3D_PROFILE(SetTextureData);

    if (!object_.ptr_)
    {
        URHO3D_LOGERROR("No texture created, can not set data");
        return false;
    }

    if (!data)
    {
        URHO3D_LOGERROR("Null source for setting data");
        return false;
    }

    if (level >= levels_)
    {
        URHO3D_LOGERROR("Illegal mip level for setting data");
        return false;
    }

    int levelWidth = GetLevelWidth(level);
    int levelHeight = GetLevelHeight(level);
    if (x < 0 || x + width > levelWidth || y < 0 || y + height > levelHeight || width <= 0 || height <= 0)
    {
        URHO3D_LOGERROR("Illegal dimensions for setting data");
        return false;
    }

    // If compressed, align the update region on a block
    if (IsCompressed())
    {
        x &= ~3;
        y &= ~3;
        width += 3;
        width &= 0xfffffffc;
        height += 3;
        height &= 0xfffffffc;
    }

    unsigned char* src = (unsigned char*)data;
    unsigned rowSize = GetRowDataSize(width);
    unsigned rowStart = GetRowDataSize(x);
    unsigned subResource = D3D11CalcSubresource(level, 0, levels_);

    if (usage_ == TEXTURE_DYNAMIC)
    {
        if (IsCompressed())
        {
            height = (height + 3) >> 2;
            y >>= 2;
        }

        D3D11_MAPPED_SUBRESOURCE mappedData;
        mappedData.pData = nullptr;

        HRESULT hr = graphics_->GetImpl()->GetDeviceContext()->Map((ID3D11Resource*)object_.ptr_, subResource, D3D11_MAP_WRITE_DISCARD, 0,
            &mappedData);
        if (FAILED(hr) || !mappedData.pData)
        {
            URHO3D_LOGD3DERROR("Failed to map texture for update", hr);
            return false;
        }
        else
        {
            for (int row = 0; row < height; ++row)
                memcpy((unsigned char*)mappedData.pData + (row + y) * mappedData.RowPitch + rowStart, src + row * rowSize, rowSize);
            graphics_->GetImpl()->GetDeviceContext()->Unmap((ID3D11Resource*)object_.ptr_, subResource);
        }
    }
    else
    {
        D3D11_BOX destBox;
        destBox.left = (UINT)x;
        destBox.right = (UINT)(x + width);
        destBox.top = (UINT)y;
        destBox.bottom = (UINT)(y + height);
        destBox.front = 0;
        destBox.back = 1;

        graphics_->GetImpl()->GetDeviceContext()->UpdateSubresource((ID3D11Resource*)object_.ptr_, subResource, &destBox, data,
            rowSize, 0);
    }

    return true;
}

bool Texture2D::SetData(Image* image, bool useAlpha)
{
    if (!image)
    {
        URHO3D_LOGERROR("Null image, can not load texture");
        return false;
    }

    // Use a shared ptr for managing the temporary mip images created during this function
    SharedPtr<Image> mipImage;
    unsigned memoryUse = sizeof(Texture2D);
    int quality = QUALITY_HIGH;
    Renderer* renderer = GetSubsystem<Renderer>();
    if (renderer)
        quality = renderer->GetTextureQuality();

    if (!image->IsCompressed())
    {
        // Convert unsuitable formats to RGBA
        unsigned components = image->GetComponents();
        if ((components == 1 && !useAlpha) || components == 2 || components == 3)
        {
            mipImage = image->ConvertToRGBA(); image = mipImage;
            if (!image)
                return false;
            components = image->GetComponents();
        }

        unsigned char* levelData = image->GetData();
        int levelWidth = image->GetWidth();
        int levelHeight = image->GetHeight();
        unsigned format = 0;

        // Discard unnecessary mip levels
        for (unsigned i = 0; i < mipsToSkip_[quality]; ++i)
        {
            mipImage = image->GetNextLevel(); image = mipImage;
            levelData = image->GetData();
            levelWidth = image->GetWidth();
            levelHeight = image->GetHeight();
        }

        switch (components)
        {
        case 1:
            format = Graphics::GetAlphaFormat();
            break;

        case 4:
            format = Graphics::GetRGBAFormat();
            break;

        default: break;
        }

        // If image was previously compressed, reset number of requested levels to avoid error if level count is too high for new size
        if (IsCompressed() && requestedLevels_ > 1)
            requestedLevels_ = 0;
        SetSize(levelWidth, levelHeight, format);

        for (unsigned i = 0; i < levels_; ++i)
        {
            SetData(i, 0, 0, levelWidth, levelHeight, levelData);
            memoryUse += levelWidth * levelHeight * components;

            if (i < levels_ - 1)
            {
                mipImage = image->GetNextLevel(); image = mipImage;
                levelData = image->GetData();
                levelWidth = image->GetWidth();
                levelHeight = image->GetHeight();
            }
        }
    }
    else
    {
        int width = image->GetWidth();
        int height = image->GetHeight();
        unsigned levels = image->GetNumCompressedLevels();
        unsigned format = graphics_->GetFormat(image->GetCompressedFormat());
        bool needDecompress = false;

        if (!format)
        {
            format = Graphics::GetRGBAFormat();
            needDecompress = true;
        }

        unsigned mipsToSkip = mipsToSkip_[quality];
        if (mipsToSkip >= levels)
            mipsToSkip = levels - 1;
        while (mipsToSkip && (width / (1 << mipsToSkip) < 4 || height / (1 << mipsToSkip) < 4))
            --mipsToSkip;
        width /= (1 << mipsToSkip);
        height /= (1 << mipsToSkip);

        SetNumLevels(Max((levels - mipsToSkip), 1U));
        SetSize(width, height, format);

        for (unsigned i = 0; i < levels_ && i < levels - mipsToSkip; ++i)
        {
            CompressedLevel level = image->GetCompressedLevel(i + mipsToSkip);
            if (!needDecompress)
            {
                SetData(i, 0, 0, level.width_, level.height_, level.data_);
                memoryUse += level.rows_ * level.rowSize_;
            }
            else
            {
                unsigned char* rgbaData = new unsigned char[level.width_ * level.height_ * 4];
                level.Decompress(rgbaData);
                SetData(i, 0, 0, level.width_, level.height_, rgbaData);
                memoryUse += level.width_ * level.height_ * 4;
                delete[] rgbaData;
            }
        }
    }

    SetMemoryUse(memoryUse);
    return true;
}

bool Texture2D::GetData(unsigned level, void* dest) const
{
    if (!object_.ptr_)
    {
        URHO3D_LOGERROR("No texture created, can not get data");
        return false;
    }

    if (!dest)
    {
        URHO3D_LOGERROR("Null destination for getting data");
        return false;
    }

    if (level >= levels_)
    {
        URHO3D_LOGERROR("Illegal mip level for getting data");
        return false;
    }

    if (multiSample_ > 1 && !autoResolve_)
    {
        URHO3D_LOGERROR("Can not get data from multisampled texture without autoresolve");
        return false;
    }

    if (resolveDirty_)
        graphics_->ResolveToTexture(const_cast<Texture2D*>(this));

    int levelWidth = GetLevelWidth(level);
    int levelHeight = GetLevelHeight(level);

    D3D11_TEXTURE2D_DESC textureDesc;
    memset(&textureDesc, 0, sizeof textureDesc);
    textureDesc.Width = (UINT)levelWidth;
    textureDesc.Height = (UINT)levelHeight;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = (DXGI_FORMAT)format_;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_STAGING;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* stagingTexture = nullptr;
    HRESULT hr = graphics_->GetImpl()->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &stagingTexture);
    if (FAILED(hr))
    {
        URHO3D_LOGD3DERROR("Failed to create staging texture for GetData", hr);
        URHO3D_SAFE_RELEASE(stagingTexture);
        return false;
    }

    ID3D11Resource* srcResource = (ID3D11Resource*)(resolveTexture_ ? resolveTexture_ : object_.ptr_);
    unsigned srcSubResource = D3D11CalcSubresource(level, 0, levels_);

    D3D11_BOX srcBox;
    srcBox.left = 0;
    srcBox.right = (UINT)levelWidth;
    srcBox.top = 0;
    srcBox.bottom = (UINT)levelHeight;
    srcBox.front = 0;
    srcBox.back = 1;
    graphics_->GetImpl()->GetDeviceContext()->CopySubresourceRegion(stagingTexture, 0, 0, 0, 0, srcResource,
        srcSubResource, &srcBox);

    D3D11_MAPPED_SUBRESOURCE mappedData;
    mappedData.pData = nullptr;
    unsigned rowSize = GetRowDataSize(levelWidth);
    unsigned numRows = (unsigned)(IsCompressed() ? (levelHeight + 3) >> 2 : levelHeight);

    hr = graphics_->GetImpl()->GetDeviceContext()->Map((ID3D11Resource*)stagingTexture, 0, D3D11_MAP_READ, 0, &mappedData);
    if (FAILED(hr) || !mappedData.pData)
    {
        URHO3D_LOGD3DERROR("Failed to map staging texture for GetData", hr);
        URHO3D_SAFE_RELEASE(stagingTexture);
        return false;
    }
    else
    {
        for (unsigned row = 0; row < numRows; ++row)
            memcpy((unsigned char*)dest + row * rowSize, (unsigned char*)mappedData.pData + row * mappedData.RowPitch, rowSize);
        graphics_->GetImpl()->GetDeviceContext()->Unmap((ID3D11Resource*)stagingTexture, 0);
        URHO3D_SAFE_RELEASE(stagingTexture);
        return true;
    }
}

bool Texture2D::Create()
{
    Release();

    if (!graphics_ || !width_ || !height_)
        return false;

    levels_ = CheckMaxLevels(width_, height_, requestedLevels_);

    D3D11_TEXTURE2D_DESC textureDesc;
    memset(&textureDesc, 0, sizeof textureDesc);
    textureDesc.Format = (DXGI_FORMAT)(sRGB_ ? GetSRGBFormat(format_) : format_);

    // Disable multisampling if not supported
    if (multiSample_ > 1 && !graphics_->GetImpl()->CheckMultiSampleSupport(textureDesc.Format, multiSample_))
    {
        multiSample_ = 1;
        autoResolve_ = false;
    }

    // Set mipmapping
    if (usage_ == TEXTURE_DEPTHSTENCIL)
        levels_ = 1;
    else if (usage_ == TEXTURE_RENDERTARGET && levels_ != 1 && multiSample_ == 1)
        textureDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
    
	bool supportsUAV = false;
	if (textureDesc.Format == graphics_->GetRGBAFormat() || textureDesc.Format == graphics_->GetRGBAFloat32Format())
		supportsUAV = true;

    textureDesc.Width = (UINT)width_;
    textureDesc.Height = (UINT)height_;
    // Disable mip levels from the multisample texture. Rather create them to the resolve texture
    textureDesc.MipLevels = multiSample_ == 1 ? levels_ : 1;
    textureDesc.ArraySize = 1;
    textureDesc.SampleDesc.Count = (UINT)multiSample_;
    textureDesc.SampleDesc.Quality = graphics_->GetImpl()->GetMultiSampleQuality(textureDesc.Format, multiSample_);

	textureDesc.Usage = usage_ == TEXTURE_DYNAMIC ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    
    if (IsComputeWriteable(format_) && multiSample_ == 1)
        textureDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

    if (usage_ == TEXTURE_RENDERTARGET)
        textureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    else if (usage_ == TEXTURE_DEPTHSTENCIL)
        textureDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;

    textureDesc.CPUAccessFlags = usage_ == TEXTURE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;

    // D3D feature level 10.0 or below does not support readable depth when multisampled
    if (usage_ == TEXTURE_DEPTHSTENCIL && multiSample_ > 1 && graphics_->GetImpl()->GetDevice()->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_1)
        textureDesc.BindFlags &= ~D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = graphics_->GetImpl()->GetDevice()->CreateTexture2D(&textureDesc, nullptr, (ID3D11Texture2D**)&object_);
    if (FAILED(hr))
    {
        URHO3D_LOGD3DERROR("Failed to create texture", hr);
        URHO3D_SAFE_RELEASE(object_.ptr_);
        return false;
    }

    // Create resolve texture for multisampling if necessary
    if (multiSample_ > 1 && autoResolve_)
    {
        textureDesc.MipLevels = levels_;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.SampleDesc.Quality = 0;
        if (levels_ != 1)
            textureDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;

        HRESULT hr = graphics_->GetImpl()->GetDevice()->CreateTexture2D(&textureDesc, nullptr, (ID3D11Texture2D**)&resolveTexture_);
        if (FAILED(hr))
        {
            URHO3D_LOGD3DERROR("Failed to create resolve texture", hr);
            URHO3D_SAFE_RELEASE(resolveTexture_);
            return false;
        }
    }

    if (textureDesc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC resourceViewDesc;
        memset(&resourceViewDesc, 0, sizeof resourceViewDesc);
        resourceViewDesc.Format = (DXGI_FORMAT)GetSRVFormat(textureDesc.Format);
        resourceViewDesc.ViewDimension = (multiSample_ > 1 && !autoResolve_) ? D3D11_SRV_DIMENSION_TEXTURE2DMS :
            D3D11_SRV_DIMENSION_TEXTURE2D;
        resourceViewDesc.Texture2D.MipLevels = (UINT)levels_;

        // Sample the resolve texture if created, otherwise the original
        ID3D11Resource* viewObject = resolveTexture_ ? (ID3D11Resource*)resolveTexture_ : (ID3D11Resource*)object_.ptr_;
        hr = graphics_->GetImpl()->GetDevice()->CreateShaderResourceView(viewObject, &resourceViewDesc,
            (ID3D11ShaderResourceView**)&shaderResourceView_);
        if (FAILED(hr))
        {
            URHO3D_LOGD3DERROR("Failed to create shader resource view for texture", hr);
            URHO3D_SAFE_RELEASE(shaderResourceView_);
            return false;
        }
    }

    if (usage_ == TEXTURE_RENDERTARGET)
    {
        D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
        memset(&renderTargetViewDesc, 0, sizeof renderTargetViewDesc);
        renderTargetViewDesc.Format = textureDesc.Format;
        renderTargetViewDesc.ViewDimension = multiSample_ > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;

        hr = graphics_->GetImpl()->GetDevice()->CreateRenderTargetView((ID3D11Resource*)object_.ptr_, &renderTargetViewDesc,
            (ID3D11RenderTargetView**)&renderSurface_->renderTargetView_);
        if (FAILED(hr))
        {
            URHO3D_LOGD3DERROR("Failed to create rendertarget view for texture", hr);
            URHO3D_SAFE_RELEASE(renderSurface_->renderTargetView_);
            return false;
        }
    }
    else if (usage_ == TEXTURE_DEPTHSTENCIL)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
        memset(&depthStencilViewDesc, 0, sizeof depthStencilViewDesc);
        depthStencilViewDesc.Format = (DXGI_FORMAT)GetDSVFormat(textureDesc.Format);
        depthStencilViewDesc.ViewDimension = multiSample_ > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;

        hr = graphics_->GetImpl()->GetDevice()->CreateDepthStencilView((ID3D11Resource*)object_.ptr_, &depthStencilViewDesc,
            (ID3D11DepthStencilView**)&renderSurface_->renderTargetView_);
        if (FAILED(hr))
        {
            URHO3D_LOGD3DERROR("Failed to create depth-stencil view for texture", hr);
            URHO3D_SAFE_RELEASE(renderSurface_->renderTargetView_);
            return false;
        }

        // Create also a read-only version of the view for simultaneous depth testing and sampling in shader
        // Requires feature level 11
        if (graphics_->GetImpl()->GetDevice()->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0)
        {
            depthStencilViewDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH;
            hr = graphics_->GetImpl()->GetDevice()->CreateDepthStencilView((ID3D11Resource*)object_.ptr_, &depthStencilViewDesc,
                (ID3D11DepthStencilView**)&renderSurface_->readOnlyView_);
            if (FAILED(hr))
            {
                URHO3D_LOGD3DERROR("Failed to create read-only depth-stencil view for texture", hr);
                URHO3D_SAFE_RELEASE(renderSurface_->readOnlyView_);
            }
        }
    }

	if (textureDesc.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC descUAV;
		ZeroMemory(&descUAV, sizeof(descUAV));
		descUAV.Format = textureDesc.Format;
		descUAV.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		descUAV.Texture2D.MipSlice = 0;
		hr = graphics_->GetImpl()->GetDevice()->CreateUnorderedAccessView((ID3D11Resource*)object_.ptr_, &descUAV, (ID3D11UnorderedAccessView**)&uaView_);
		if (FAILED(hr))
		{
			URHO3D_LOGD3DERROR("Failed to create UAV for texture", hr);
			return false;
		}
	}

    return true;
}

const char* NVAPIMsg(NvAPI_Status status)
{
#define X(S) case S: return #S;
    switch (status)
    {
        X(NVAPI_OK)
        X(NVAPI_ERROR)
        X(NVAPI_LIBRARY_NOT_FOUND)
        X(NVAPI_NO_IMPLEMENTATION)
        X(NVAPI_API_NOT_INITIALIZED)
        X(NVAPI_INVALID_ARGUMENT)
        X(NVAPI_NVIDIA_DEVICE_NOT_FOUND)
        X(NVAPI_END_ENUMERATION)
        X(NVAPI_INVALID_HANDLE)
        X(NVAPI_INCOMPATIBLE_STRUCT_VERSION)
        X(NVAPI_HANDLE_INVALIDATED)
        X(NVAPI_OPENGL_CONTEXT_NOT_CURRENT)
        X(NVAPI_INVALID_POINTER)
        X(NVAPI_NO_GL_EXPERT)
        X(NVAPI_INSTRUMENTATION_DISABLED)
        X(NVAPI_NO_GL_NSIGHT)
        
        X(NVAPI_EXPECTED_LOGICAL_GPU_HANDLE)
        X(NVAPI_EXPECTED_PHYSICAL_GPU_HANDLE)
        X(NVAPI_EXPECTED_DISPLAY_HANDLE)
        X(NVAPI_INVALID_COMBINATION)
        X(NVAPI_NOT_SUPPORTED)
        X(NVAPI_PORTID_NOT_FOUND)
        X(NVAPI_EXPECTED_UNATTACHED_DISPLAY_HANDLE)
        X(NVAPI_INVALID_PERF_LEVEL)
        X(NVAPI_DEVICE_BUSY)
        X(NVAPI_NV_PERSIST_FILE_NOT_FOUND)
        X(NVAPI_PERSIST_DATA_NOT_FOUND)
        X(NVAPI_EXPECTED_TV_DISPLAY)
        X(NVAPI_EXPECTED_TV_DISPLAY_ON_DCONNECTOR)
        X(NVAPI_NO_ACTIVE_SLI_TOPOLOGY)
        X(NVAPI_SLI_RENDERING_MODE_NOTALLOWED)
        X(NVAPI_EXPECTED_DIGITAL_FLAT_PANEL)
        X(NVAPI_ARGUMENT_EXCEED_MAX_SIZE)
        X(NVAPI_DEVICE_SWITCHING_NOT_ALLOWED)
        X(NVAPI_TESTING_CLOCKS_NOT_SUPPORTED)
        X(NVAPI_UNKNOWN_UNDERSCAN_CONFIG)
        X(NVAPI_TIMEOUT_RECONFIGURING_GPU_TOPO)
        X(NVAPI_DATA_NOT_FOUND)
        X(NVAPI_EXPECTED_ANALOG_DISPLAY)
        X(NVAPI_NO_VIDLINK)
        X(NVAPI_REQUIRES_REBOOT)
        X(NVAPI_INVALID_HYBRID_MODE)
        X(NVAPI_MIXED_TARGET_TYPES)
        X(NVAPI_SYSWOW64_NOT_SUPPORTED)
        X(NVAPI_IMPLICIT_SET_GPU_TOPOLOGY_CHANGE_NOT_ALLOWED)
        X(NVAPI_REQUEST_USER_TO_CLOSE_NON_MIGRATABLE_APPS)
        X(NVAPI_OUT_OF_MEMORY)
        X(NVAPI_WAS_STILL_DRAWING)
        X(NVAPI_FILE_NOT_FOUND)
        X(NVAPI_TOO_MANY_UNIQUE_STATE_OBJECTS)
        X(NVAPI_INVALID_CALL)
        X(NVAPI_D3D10_1_LIBRARY_NOT_FOUND)
        X(NVAPI_FUNCTION_NOT_FOUND)
        X(NVAPI_INVALID_USER_PRIVILEGE)
        
        X(NVAPI_EXPECTED_NON_PRIMARY_DISPLAY_HANDLE)
        X(NVAPI_EXPECTED_COMPUTE_GPU_HANDLE)
        X(NVAPI_STEREO_NOT_INITIALIZED)
        X(NVAPI_STEREO_REGISTRY_ACCESS_FAILED)
        X(NVAPI_STEREO_REGISTRY_PROFILE_TYPE_NOT_SUPPORTED)
        X(NVAPI_STEREO_REGISTRY_VALUE_NOT_SUPPORTED)
        X(NVAPI_STEREO_NOT_ENABLED)
        X(NVAPI_STEREO_NOT_TURNED_ON)
        X(NVAPI_STEREO_INVALID_DEVICE_INTERFACE)
        X(NVAPI_STEREO_PARAMETER_OUT_OF_RANGE)
        X(NVAPI_STEREO_FRUSTUM_ADJUST_MODE_NOT_SUPPORTED)
        X(NVAPI_TOPO_NOT_POSSIBLE)
        X(NVAPI_MODE_CHANGE_FAILED)
        X(NVAPI_D3D11_LIBRARY_NOT_FOUND)
        X(NVAPI_INVALID_ADDRESS)
        X(NVAPI_STRING_TOO_SMALL)
        X(NVAPI_MATCHING_DEVICE_NOT_FOUND)
        X(NVAPI_DRIVER_RUNNING)
        X(NVAPI_DRIVER_NOTRUNNING)
        X(NVAPI_ERROR_DRIVER_RELOAD_REQUIRED)
        X(NVAPI_SET_NOT_ALLOWED)
        X(NVAPI_ADVANCED_DISPLAY_TOPOLOGY_REQUIRED)
        X(NVAPI_SETTING_NOT_FOUND)
        X(NVAPI_SETTING_SIZE_TOO_LARGE)
        X(NVAPI_TOO_MANY_SETTINGS_IN_PROFILE)
        X(NVAPI_PROFILE_NOT_FOUND)
        X(NVAPI_PROFILE_NAME_IN_USE)
        X(NVAPI_PROFILE_NAME_EMPTY)
        X(NVAPI_EXECUTABLE_NOT_FOUND)
        X(NVAPI_EXECUTABLE_ALREADY_IN_USE)
        X(NVAPI_DATATYPE_MISMATCH)
        X(NVAPI_PROFILE_REMOVED)
        X(NVAPI_UNREGISTERED_RESOURCE)
        X(NVAPI_ID_OUT_OF_RANGE)
        X(NVAPI_DISPLAYCONFIG_VALIDATION_FAILED)
        X(NVAPI_DPMST_CHANGED)
        X(NVAPI_INSUFFICIENT_BUFFER)
        X(NVAPI_ACCESS_DENIED)
        X(NVAPI_MOSAIC_NOT_ACTIVE)
        X(NVAPI_SHARE_RESOURCE_RELOCATED)
        X(NVAPI_REQUEST_USER_TO_DISABLE_DWM)
        X(NVAPI_D3D_DEVICE_LOST)
        X(NVAPI_INVALID_CONFIGURATION)
        X(NVAPI_STEREO_HANDSHAKE_NOT_DONE)
        X(NVAPI_EXECUTABLE_PATH_IS_AMBIGUOUS)
        X(NVAPI_DEFAULT_STEREO_PROFILE_IS_NOT_DEFINED)
        X(NVAPI_DEFAULT_STEREO_PROFILE_DOES_NOT_EXIST)
        X(NVAPI_CLUSTER_ALREADY_EXISTS)
        X(NVAPI_DPMST_DISPLAY_ID_EXPECTED)
        X(NVAPI_INVALID_DISPLAY_ID)
        X(NVAPI_STREAM_IS_OUT_OF_SYNC)
        X(NVAPI_INCOMPATIBLE_AUDIO_DRIVER)
        X(NVAPI_VALUE_ALREADY_SET)
        X(NVAPI_TIMEOUT)
        X(NVAPI_GPU_WORKSTATION_FEATURE_INCOMPLETE)
        X(NVAPI_STEREO_INIT_ACTIVATION_NOT_DONE)
        X(NVAPI_SYNC_NOT_ACTIVE)
        X(NVAPI_SYNC_MASTER_NOT_FOUND)
        X(NVAPI_INVALID_SYNC_TOPOLOGY)
        X(NVAPI_ECID_SIGN_ALGO_UNSUPPORTED)
        X(NVAPI_ECID_KEY_VERIFICATION_FAILED)
        X(NVAPI_FIRMWARE_OUT_OF_DATE)
        X(NVAPI_FIRMWARE_REVISION_NOT_SUPPORTED)
        X(NVAPI_LICENSE_CALLER_AUTHENTICATION_FAILED)
        X(NVAPI_D3D_DEVICE_NOT_REGISTERED)
        X(NVAPI_RESOURCE_NOT_ACQUIRED)
        X(NVAPI_TIMING_NOT_SUPPORTED)
        X(NVAPI_HDCP_ENCRYPTION_FAILED)
        X(NVAPI_PCLK_LIMITATION_FAILED)
        X(NVAPI_NO_CONNECTOR_FOUND)
        X(NVAPI_HDCP_DISABLED)
        X(NVAPI_API_IN_USE)
        X(NVAPI_NVIDIA_DISPLAY_NOT_FOUND)
        X(NVAPI_PRIV_SEC_VIOLATION)
        X(NVAPI_INCORRECT_VENDOR)
        X(NVAPI_DISPLAY_IN_USE)
        X(NVAPI_UNSUPPORTED_CONFIG_NON_HDCP_HMD)
        X(NVAPI_MAX_DISPLAY_LIMIT_REACHED)
        X(NVAPI_INVALID_DIRECT_MODE_DISPLAY)
        X(NVAPI_GPU_IN_DEBUG_MODE)
        X(NVAPI_D3D_CONTEXT_NOT_FOUND)
        X(NVAPI_STEREO_VERSION_MISMATCH)
        X(NVAPI_GPU_NOT_POWERED)
        X(NVAPI_ERROR_DRIVER_RELOAD_IN_PROGRESS)
        X(NVAPI_WAIT_FOR_HW_RESOURCE)
        X(NVAPI_REQUIRE_FURTHER_HDCP_ACTION)
        X(NVAPI_DISPLAY_MUX_TRANSITION_FAILED)
        X(NVAPI_INVALID_DSC_VERSION)
        X(NVAPI_INVALID_DSC_SLICECOUNT)
        X(NVAPI_INVALID_DSC_OUTPUT_BPP)
    }
    return "NVAPI_OK";
}

ID3D11NvShadingRateResourceView* Texture2D::GetVRSView()
{
    if (vrsView_ == nullptr && object_.ptr_ && graphics_)
    {
        NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC desc;
        desc.version = NV_D3D11_SHADING_RATE_RESOURCE_VIEW_DESC_VER;
        D3D11_TEXTURE2D_DESC resDesc;
        ((ID3D11Texture2D*)object_.ptr_)->GetDesc(&resDesc);
        desc.Format = resDesc.Format;
        desc.Texture2D.MipSlice = 0;
        desc.ViewDimension = NV_SRRV_DIMENSION_TEXTURE2D;
        auto result = NvAPI_D3D11_CreateShadingRateResourceView(graphics_->GetImpl()->GetDevice(), (ID3D11Resource*)object_.ptr_, &desc, &vrsView_);
        if (result != NVAPI_OK)
            URHO3D_LOGERRORF("NVAPI Error CreateShadingRateResourceView: %s", NVAPIMsg(result));
    }

    return vrsView_;
}

void Texture2D::SetupVRS(ID3D11DeviceContext* ctx, SharedPtr<Texture2D> tex, bool on)
{
    NV_D3D11_VIEWPORTS_SHADING_RATE_DESC desc;
    desc.version = NV_D3D11_VIEWPORTS_SHADING_RATE_DESC_VER;
    desc.numViewports = 1;

    NV_D3D11_VIEWPORT_SHADING_RATE_DESC_V1 rates;
    rates.enableVariablePixelShadingRate = tex != nullptr && on ? true : false;
    rates.shadingRateTable[0] = NV_PIXEL_X1_PER_RASTER_PIXEL;
    rates.shadingRateTable[1] = NV_PIXEL_X1_PER_2X1_RASTER_PIXELS;
    rates.shadingRateTable[2] = NV_PIXEL_X1_PER_1X2_RASTER_PIXELS;
    rates.shadingRateTable[3] = NV_PIXEL_X1_PER_2X2_RASTER_PIXELS;
    rates.shadingRateTable[4] = NV_PIXEL_X1_PER_4X4_RASTER_PIXELS;
    rates.shadingRateTable[5] = NV_PIXEL_X4_PER_RASTER_PIXEL;
    for (int i = 6; i < 16; ++i)
        rates.shadingRateTable[i] = NV_PIXEL_X1_PER_RASTER_PIXEL;
    desc.pViewports = &rates;

    auto result = NvAPI_D3D11_RSSetViewportsPixelShadingRates(ctx, &desc);
    if (result != NVAPI_OK)
        URHO3D_LOGERRORF("NVAPI Error RSSetViewportsPixelShadingRates: %s", NVAPIMsg(result));

    if (tex)
    {
        result = NvAPI_D3D11_RSSetShadingRateResourceView(ctx, tex->GetVRSView());
        if (result != NVAPI_OK)
            URHO3D_LOGERRORF("NVAPI Error RSSetViewportsPixelShadingRates: %s", NVAPIMsg(result));
    }
    else
        NvAPI_D3D11_RSSetShadingRateResourceView(ctx, nullptr);
}

void Texture2D::SetupForVRS(int fullSizeX, int fullSizeY, int x, int y)
{
    const int tilesX = (fullSizeX + NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH - 1) / NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH;
    const int tilesY = (fullSizeY + NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT - 1) / NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT;

    SetSize(tilesX, tilesY, DXGI_FORMAT_R8_UINT);

    x = Min(x, 4);
    y = Min(y, 4);

    if (GetFormat() != DXGI_FORMAT_R8_UINT)
    {
        URHO3D_LOGERRORF("Unsupported DXGI_FORMAT for VRS: must be R8_UINT, is %u", GetFormat());
        return;
    }

    unsigned char val = 0;
    if (x == 2 && y == 1)
        val = 1;
    else if (x == 1 && y == 2)
        val = 2;
    else if (x == 2 && y == 2)
        val = 3;
    else if (x == 4 && y == 4)
        val = 4;

    PODVector<unsigned char> data;
    data.Resize(GetWidth() * GetHeight());
    memset(data.Buffer(), val, GetWidth() * GetHeight());
    SetData(0, 0, 0, GetWidth(), GetHeight(), data.Buffer());
}

void Texture2D::SetupForVRSVirtualReality(int fullSizeX, int fullSizeY, bool ban4x4)
{
    const int w = fullSizeX;
    const int h = fullSizeY;

    const int tilesX = (fullSizeX + NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH - 1) / NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH;
    const int tilesY = (fullSizeY + NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT - 1) / NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT;

    SetSize(tilesX, tilesY, DXGI_FORMAT_R8_UINT);

    const int halfX = tilesX / 2;
    const int quartX = tilesX / 4;
    const int halfY = tilesY / 2;
    const int quartY = tilesY / 4;
    const int miniY = tilesY / 8;

    const int eyeRadius = quartY + miniY;
    const int innerEyeRadius = quartY;

    const Vector2 leftEye = Vector2(quartX, halfY);
    const Vector2 rightEye = Vector2(halfX + quartX, halfY);

    PODVector<unsigned char> data;
    data.Resize(GetWidth() * GetHeight());
    memset(data.Buffer(), ban4x4 ? 3 : 4, data.Size()); // set everything to 4x4 (or 2x2 if banned)

    for (int yy = 0; yy < tilesY; ++yy)
    {
        for (int xx = 0; xx < tilesX; ++xx)
        {
            Vector2 ps(xx, yy);
            const float dist = Min(Abs((ps - leftEye).Length()), Abs((ps - rightEye).Length()));
            if (dist < innerEyeRadius)
                data[yy * GetWidth() + xx] = 0; // 1x1
            else if (dist < eyeRadius)
                data[yy * GetWidth() + xx] = 3; // 2x2
        }
    }

    SetData(0, 0, 0, GetWidth(), GetHeight(), data.Buffer());
}

void Texture2D::SetupForVRSFoveated(int fullSizeX, int fullSizeY, bool ban4x4)
{
    const int w = fullSizeX;
    const int h = fullSizeY;

    int tilesX = (fullSizeX + NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH - 1) / NV_VARIABLE_PIXEL_SHADING_TILE_WIDTH;
    int tilesY = (fullSizeY + NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT - 1) / NV_VARIABLE_PIXEL_SHADING_TILE_HEIGHT;

    SetNumLevels(1);
    SetSize(tilesX, tilesY, DXGI_FORMAT_R8_UINT, TEXTURE_DYNAMIC);

    int halfX = tilesX / 2;
    int quartX = tilesX / 4;
    int halfY = tilesY / 2;
    int quartY = tilesY / 4;
    int miniY = tilesY / 8;

    int eyeRadius = halfY;// quartY + miniY;
    int innerEyeRadius = quartY;

    Vector2 leftEye = Vector2(halfX, halfY);

    auto dataSize = tilesX * tilesY;
    PODVector<unsigned char> data;
    data.Resize(dataSize);
    memset(data.Buffer(), ban4x4 ? 3 : 4, data.Size()); // set everything to 4x4 (or 2x2 if banned)

    for (int yy = 0; yy < tilesY; ++yy)
    {
        for (int xx = 0; xx < tilesX; ++xx)
        {
            Vector2 ps(xx, yy);
            const float dist = Abs((ps - leftEye).Length());
            if (dist < innerEyeRadius)
                data[yy * GetWidth() + xx] = 0; // 1x1
            else if (dist < eyeRadius)
                data[yy * GetWidth() + xx] = 3; // 2x2
        }
    }

    SetData(0, 0, 0, GetWidth(), GetHeight(), data.Buffer());
}

bool Texture2D::CreateFromExternal(ID3D11Texture2D* tex, int msaaLevel)
{
    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    // Allow amping up the MSAA of what we get, it's a bit of extra checks but makes everything 'just work' without added resolve work
    // what happens is auto-resolve is forced on and it the given texture is used as the resolve texture instead of the main one which will be created
    const bool differingMSAA = msaaLevel != desc.SampleDesc.Count;

    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    format_ = desc.Format;
    width_ = desc.Width;
    height_ = desc.Height;
    levels_ = desc.MipLevels;
    multiSample_ = Max(desc.SampleDesc.Count, msaaLevel);
    depth_ = 1;
    if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        sRGB_ = true;

    if (differingMSAA) // construct an MSAA fake texture for it
    {
        auto hr = graphics_->GetImpl()->GetDevice()->CreateTexture2D(&desc, nullptr, (ID3D11Texture2D**)&object_);
        if (FAILED(hr))
        {
            URHO3D_LOGD3DERROR("Failed to create MSAA upsample shadow for texture", hr);
            URHO3D_SAFE_RELEASE(object_.ptr_);
            return false;
        }
    }
    else // use as is, no MSAA front-man
        object_.ptr_ = tex;
    
    if (desc.BindFlags & D3D11_BIND_RENDER_TARGET)
    {
        filterMode_ = FILTER_NEAREST;
        renderSurface_ = new RenderSurface(this);

        SubscribeToEvent(E_RENDERSURFACEUPDATE, URHO3D_HANDLER(Texture2D, HandleRenderSurfaceUpdate));
        usage_ = TEXTURE_RENDERTARGET;
        D3D11_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
        memset(&renderTargetViewDesc, 0, sizeof renderTargetViewDesc);
        renderTargetViewDesc.Format = desc.Format;
        renderTargetViewDesc.ViewDimension = multiSample_ > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        renderTargetViewDesc.Texture2D.MipSlice = 0;

        auto hr = graphics_->GetImpl()->GetDevice()->CreateRenderTargetView((ID3D11Resource*)object_.ptr_, &renderTargetViewDesc,
            (ID3D11RenderTargetView**)&renderSurface_->renderTargetView_);
        if (FAILED(hr))
        {
            URHO3D_LOGD3DERROR("Failed to create rendertarget view for texture", hr);
            URHO3D_SAFE_RELEASE(renderSurface_->renderTargetView_);
            return false;
        }

        autoResolve_ = true;

        if (!differingMSAA && desc.SampleDesc.Count > 1)
        {
            desc.MipLevels = levels_;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            if (levels_ != 1)
                desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;

            HRESULT hr = graphics_->GetImpl()->GetDevice()->CreateTexture2D(&desc, nullptr, (ID3D11Texture2D**)&resolveTexture_);
            if (FAILED(hr))
            {
                URHO3D_LOGD3DERROR("Failed to create resolve texture", hr);
                URHO3D_SAFE_RELEASE(resolveTexture_);
                return false;
            }
        }
        else
        {
            resolveTexture_ = tex;
        }
    }
    else if (desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE)
        usage_ = TEXTURE_DYNAMIC;
    else
        usage_ = TEXTURE_STATIC;

    owned_ = false;

    return true;
}

}
