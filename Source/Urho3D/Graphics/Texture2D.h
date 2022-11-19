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

#include "../Container/Ptr.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Texture.h"

#ifdef URHO3D_D3D11
    // for NVAPI variable rate shading, currently only supported on 2D targets (VR is using side-by-side targets)
    // because it's NV the tile size is always 16
    struct ID3D11NvShadingRateResourceView_V1;
    typedef ID3D11NvShadingRateResourceView_V1    ID3D11NvShadingRateResourceView;
    class ID3D11DeviceContext;
    class ID3D11Texture2D;
#endif

namespace Urho3D
{

class Image;
class XMLFile;

/// 2D texture resource.
class URHO3D_API Texture2D : public Texture
{
    URHO3D_OBJECT(Texture2D, Texture);

public:
    /// Construct.
    explicit Texture2D(Context* context);
    /// Destruct.
    ~Texture2D() override;
    /// Register object factory.
    static void RegisterObject(Context* context);

    /// Load resource from stream. May be called from a worker thread. Return true if successful.
    bool BeginLoad(Deserializer& source) override;
    /// Finish resource loading. Always called from the main thread. Return true if successful.
    bool EndLoad() override;
    /// Mark the GPU resource destroyed on context destruction.
    void OnDeviceLost() override;
    /// Recreate the GPU resource and restore data if applicable.
    void OnDeviceReset() override;
    /// Release the texture.
    void Release() override;

    /// Set size, format, usage and multisampling parameters for rendertargets. Zero size will follow application window size. Return true if successful.
    /** Autoresolve true means the multisampled texture will be automatically resolved to 1-sample after being rendered to and before being sampled as a texture.
        Autoresolve false means the multisampled texture will be read as individual samples in the shader and is not supported on Direct3D9.
        */
    bool SetSize(int width, int height, unsigned format, TextureUsage usage = TEXTURE_STATIC, int multiSample = 1, bool autoResolve = true);
    /// Set data either partially or fully on a mip level. Return true if successful.
    bool SetData(unsigned level, int x, int y, int width, int height, const void* data);
    /// Set data from an image. Return true if successful. Optionally make a single channel image alpha-only.
    bool SetData(Image* image, bool useAlpha = false);

    /// Get data from a mip level. The destination buffer must be big enough. Return true if successful.
    bool GetData(unsigned level, void* dest) const;
    /// Get image data from zero mip level. Only RGB and RGBA textures are supported.
    bool GetImage(Image& image) const;
    /// Get image data from zero mip level. Only RGB and RGBA textures are supported.
    SharedPtr<Image> GetImage() const;

    /// Return render surface.
    RenderSurface* GetRenderSurface() const { return renderSurface_; }

#ifdef URHO3D_D3D11
    /// Initialize texture from an external texture. This function will not touch the reference-counts, caller is responsible.
    bool CreateFromExternal(ID3D11Texture2D*, int msaaLevel);
#endif

protected:
    /// Create the GPU texture.
    bool Create() override;

private:
    /// Handle render surface update event.
    void HandleRenderSurfaceUpdate(StringHash eventType, VariantMap& eventData);

    /// Render surface.
    SharedPtr<RenderSurface> renderSurface_;
    /// Image file acquired during BeginLoad.
    SharedPtr<Image> loadImage_;
    /// Parameter file acquired during BeginLoad.
    SharedPtr<XMLFile> loadParameters_;

#ifdef URHO3D_D3D11
    ID3D11NvShadingRateResourceView* vrsView_ = nullptr;

public:
    ID3D11NvShadingRateResourceView* GetVRSView();

    static void SetupVRS(ID3D11DeviceContext*, SharedPtr<Texture2D> tex, bool onOff);

    /// Setup this texture (size,data) for a fixed VRS shading rate given a reference full-size texture.
    void SetupForVRS(int fullSizeX, int fullSizeY, int x, int y);
    /// Setup this texture (size,data) for a side-by-side fixed foveated priority given a reference full-size texture.
    void SetupForVRSVirtualReality(int fullSizeX, int fullSizeY, bool ban4x4 = false);
    /// Setup this texture (size,data) for a fixed foveated priority given a reference full-size texture.
    void SetupForVRSFoveated(int fullSizeX, int fullSizeY, bool ban4x4 = false);
#endif
};

}
