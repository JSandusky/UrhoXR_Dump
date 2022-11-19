#pragma once

#include "../Container/Ptr.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Texture.h"

namespace Urho3D
{

	enum StructuredBufferType
	{
		SBUFF_STRUCTURE,
		SBUFF_UINT,
		SBUFF_FLOAT4
	};

	/// Structured buffer class, padding is the responsibility of the end user
    class URHO3D_API StructuredBuffer : public Texture
    {
        URHO3D_OBJECT(StructuredBuffer, Texture);
    public:
        /// Construct.
        StructuredBuffer(Context* ctx);
        /// Destruct.
        virtual ~StructuredBuffer();
        /// Register object factory.
        static void RegisterObject(Context* context);

        /// Mark the GPU resource destroyed on context destruction.
        void OnDeviceLost() override;
        /// Recreate the GPU resource and restore data if applicable.
        void OnDeviceReset() override;
        /// Release the texture.
        void Release() override;

        bool SetSize(unsigned dataSize, unsigned structStride);

        template<typename T>
		bool SetSize(unsigned recordCt) { type_ = SBUFF_STRUCTURE; return SetSize(sizeof(T) * recordCt, sizeof(T)); }

		template<>
		bool SetSize<unsigned>(unsigned recordCt) { type_ = SBUFF_UINT; return SetSize(sizeof(unsigned) * recordCt, sizeof(unsigned)); }

		template<>
		bool SetSize<Color>(unsigned recordCt) { type_ = SBUFF_FLOAT4; return SetSize(sizeof(Color) * recordCt, sizeof(Color)); }

        bool SetData(unsigned char* data, unsigned dataSize);
        
        template<typename T>
        bool SetData(T* data, unsigned recordCt) { return SetData((unsigned char*)data, sizeof(T) * recordCt); }

		bool GetData(void* data, unsigned recordStart, unsigned recordCt);

        unsigned GetDataSize() const { return dataSize_; }
        unsigned GetStructSize() const { return structSize_; }
		bool PrepareAccessView(bool append);
		void* GetAccessView() const;
		StructuredBufferType GetBufferType() const { return type_; }

    protected:
        /// Create the GPU texture.
        bool Create() override;

    private:
        unsigned dataSize_;
        unsigned structSize_;
		StructuredBufferType type_;
#ifdef URHO3D_D3D11
		void* uav_;
#endif
    };

}