#include <Urho3D/Core/Context.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Core/Profiler.h>
#include <Urho3D/Graphics/StructuredBuffer.h>

#include <Urho3D/Graphics/Direct3D11/D3D11GraphicsImpl.h>

#include <d3d11.h>

namespace Urho3D
{

    void StructuredBuffer::OnDeviceLost()
    {
#ifdef URHO3D_DIRECT3D11
        // No-op on Direct3D11
#endif
    }

    void StructuredBuffer::OnDeviceReset()
    {
#ifdef URHO3D_DIRECT3D11
        // No-op on Direct3D11
#endif
    }

	void StructuredBuffer::Release()
	{
		if (graphics_ && object_.ptr_)
		{
			for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
			{
				if (graphics_->GetTexture(i) == this)
					graphics_->SetTexture(i, nullptr);
			}
		}
		if (graphics_ && uav_)
		{
			//??clear the uav?
		}

		URHO3D_SAFE_RELEASE(uav_);
		URHO3D_SAFE_RELEASE(object_.ptr_);
		URHO3D_SAFE_RELEASE(resolveTexture_);
		URHO3D_SAFE_RELEASE(shaderResourceView_);
		URHO3D_SAFE_RELEASE(sampler_);
	}

    bool StructuredBuffer::SetData(unsigned char* data, unsigned dataSize)
    {
        URHO3D_PROFILE(SetStructuredBufferData);
        if (!object_.ptr_)
        {
            URHO3D_LOGERROR("No StructuredBuffer created, can not set data");
            return false;
        }

        if (!data)
        {
            URHO3D_LOGERROR("Null source for setting data");
            return false;
        }

        graphics_->GetImpl()->GetDeviceContext()->UpdateSubresource((ID3D11Resource*)object_.ptr_, 0, nullptr, data, 0, 0);
    }

	void* StructuredBuffer::GetAccessView() const
	{
		return uav_;
	}

	bool StructuredBuffer::PrepareAccessView(bool append)
	{
		if (!graphics_ || !object_.ptr_)
			return false;

		int flags = 0;
		if (append)
			flags |= D3D11_BUFFER_UAV_FLAG_APPEND;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
		memset(&uavDesc, 0, sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC));
		uavDesc.Buffer.Flags = flags;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = dataSize_ / structSize_;
		switch (type_)
		{
		case SBUFF_STRUCTURE:
			uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			break;
		case SBUFF_UINT:
			uavDesc.Format = DXGI_FORMAT_R32_UINT;
			break;
		case SBUFF_FLOAT4:
			uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			break;
		}
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;

		int hr = graphics_->GetImpl()->GetDevice()->CreateUnorderedAccessView((ID3D11Resource*)object_.ptr_, &uavDesc, (ID3D11UnorderedAccessView**)&uav_);
		if (FAILED(hr))
		{
			URHO3D_LOGERROR("Failed to create UAV for StructuredBuffer");
			uav_ = nullptr;
			return false;
		}
		return true;
	}

    bool StructuredBuffer::Create()
    {
        Release();

        if (!graphics_ || !dataSize_ || !structSize_)
            return false;

        D3D11_BUFFER_DESC bufferDesc;
        memset((void*)&bufferDesc, 0, sizeof(D3D11_BUFFER_DESC));
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
		if (type_ == SBUFF_STRUCTURE)
			bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.ByteWidth = dataSize_;
        bufferDesc.StructureByteStride = structSize_;

        HRESULT hr = graphics_->GetImpl()->GetDevice()->CreateBuffer(&bufferDesc, nullptr, 
            (ID3D11Buffer**)&object_);
        if (FAILED(hr))
        {
            URHO3D_LOGERROR("Failed to create structured buffer");
            URHO3D_SAFE_RELEASE(object_.ptr_);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC resourceViewDesc;
        memset(&resourceViewDesc, 0, sizeof resourceViewDesc);
		switch (type_)
		{
		case SBUFF_STRUCTURE:
			resourceViewDesc.Format = DXGI_FORMAT_UNKNOWN;
			break;
		case SBUFF_UINT:
			resourceViewDesc.Format = DXGI_FORMAT_R32_UINT;
			break;
		case SBUFF_FLOAT4:
			resourceViewDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			break;
		}
        resourceViewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        resourceViewDesc.Buffer.FirstElement = 0;
		resourceViewDesc.Buffer.NumElements = dataSize_ / structSize_;

        hr = graphics_->GetImpl()->GetDevice()->CreateShaderResourceView((ID3D11Resource*)object_.ptr_, &resourceViewDesc,
            (ID3D11ShaderResourceView**)&shaderResourceView_);
        if (FAILED(hr))
        {
            URHO3D_LOGERROR("Failed to create shader resource view for StructuredBuffer");
            URHO3D_SAFE_RELEASE(shaderResourceView_);
            return false;
        }

        return true;
    }

	bool StructuredBuffer::GetData(void* data, unsigned recordStart, unsigned recordCt)
	{
		if (graphics_ && object_.ptr_)
		{
			D3D11_MAPPED_SUBRESOURCE res;
			if (S_OK == graphics_->GetImpl()->GetDeviceContext()->Map((ID3D11Resource*)object_.ptr_, 0, D3D11_MAP_READ, 0, &res))
			{
				memcpy(data, ((unsigned*)res.pData) + recordStart * structSize_, recordCt * structSize_);
				graphics_->GetImpl()->GetDeviceContext()->Unmap((ID3D11Resource*)object_.ptr_, 0);
				return true;
			}
			URHO3D_LOGERROR("Could not map structured buffer for read");
		}
		return false;
	}
}