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

#include "../../Graphics/Graphics.h"
#include "../../Graphics/GraphicsImpl.h"
#include "../../Graphics/Shader.h"
#include "../../Graphics/VertexBuffer.h"
#include "../../IO/File.h"
#include "../../IO/FileSystem.h"
#include "../../IO/Log.h"
#include "../../Resource/ResourceCache.h"

#include <d3dcompiler.h>

#include "../../DebugNew.h"

namespace Urho3D
{

const char* ShaderVariation::elementSemanticNames[] =
{
    "POSITION",
    "NORMAL",
    "BINORMAL",
    "TANGENT",
    "TEXCOORD",
    "COLOR",
    "BLENDWEIGHT",
    "BLENDINDICES",
    "OBJECTINDEX",
    nullptr
};

void ShaderVariation::OnDeviceLost()
{
    // No-op on Direct3D11
}

bool ShaderVariation::Create()
{
    Release();

    if (!graphics_)
        return false;

    if (!owner_)
    {
        compilerOutput_ = "Owner shader has expired";
        return false;
    }

    // Check for up-to-date bytecode on disk
    String path, name, extension;
    SplitPath(owner_->GetName(), path, name, extension);

    // Using SM5 when compute is available for structured buffer support.
    const bool usingSM5 = graphics_->GetComputeSupport();
    switch (type_)
    {
    case VS:
        extension = usingSM5 ? ".vs5" : ".vs4";
        break;
    case PS:
        extension = usingSM5 ? ".ps5" : ".ps4";
        break;
    case GS:
        extension = usingSM5 ? ".gs5" : ".gs4";
        break;
    case HS:
        extension = ".hs5";
        break;
    case DS:
        extension = ".ds5";
        break;
    case CS:
        extension = ".cs5";
        break;
    }

    String binaryShaderName = graphics_->GetShaderCacheDir() + name + "_" + StringHash(defines_).ToString() + extension;

    if (!LoadByteCode(binaryShaderName))
    {
        // Compile shader if don't have valid bytecode
        if (!Compile())
            return false;
        // Save the bytecode after successful compile, but not if the source is from a package
        if (owner_->GetTimeStamp())
            SaveByteCode(binaryShaderName);
    }

    // Then create shader from the bytecode
    ID3D11Device* device = graphics_->GetImpl()->GetDevice();
    if (type_ == VS)
    {
        if (device && byteCode_.Size())
        {
            HRESULT hr = device->CreateVertexShader(&byteCode_[0], byteCode_.Size(), nullptr, (ID3D11VertexShader**)&object_.ptr_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(object_.ptr_);
                compilerOutput_ = "Could not create vertex shader (HRESULT " + ToStringHex((unsigned)hr) + ")";
            }
        }
        else
            compilerOutput_ = "Could not create vertex shader, empty bytecode";
    }
    else if (type_ == PS)
    {
        if (device && byteCode_.Size())
        {
            HRESULT hr = device->CreatePixelShader(&byteCode_[0], byteCode_.Size(), nullptr, (ID3D11PixelShader**)&object_.ptr_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(object_.ptr_);
                compilerOutput_ = "Could not create pixel shader (HRESULT " + ToStringHex((unsigned)hr) + ")";
            }
        }
        else
            compilerOutput_ = "Could not create pixel shader, empty bytecode";
    }
    else if (type_ == GS)
    {
        if (device && byteCode_.Size())
        {
            HRESULT hr = device->CreateGeometryShader(&byteCode_[0], byteCode_.Size(), nullptr, (ID3D11GeometryShader**)&object_.ptr_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(object_.ptr_);
                compilerOutput_ = "Could not create geometry shader (HRESULT " + ToStringHex((unsigned)hr) + ")";
            }
        }
        else
            compilerOutput_ = "Could not create geometry shader, empty bytecode";
    }
    else if (type_ == HS)
    {
        if (device && byteCode_.Size())
        {
            HRESULT hr = device->CreateHullShader(&byteCode_[0], byteCode_.Size(), nullptr, (ID3D11HullShader**)&object_.ptr_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(object_.ptr_);
                compilerOutput_ = "Could not create hull shader (HRESULT " + ToStringHex((unsigned)hr) + ")";
            }
        }
        else
            compilerOutput_ = "Could not create hull shader, empty bytecode";
    }
    else if (type_ == DS)
    {
        if (device && byteCode_.Size())
        {
            HRESULT hr = device->CreateDomainShader(&byteCode_[0], byteCode_.Size(), nullptr, (ID3D11DomainShader**)&object_.ptr_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(object_.ptr_);
                compilerOutput_ = "Could not create domain shader (HRESULT " + ToStringHex((unsigned)hr) + ")";
            }
        }
        else
            compilerOutput_ = "Could not create domain shader, empty bytecode";
    }
    else if (type_ == CS)
    {
        if (device && byteCode_.Size())
        {
            HRESULT hr = device->CreateComputeShader(&byteCode_[0], byteCode_.Size(), nullptr, (ID3D11ComputeShader**)&object_.ptr_);
            if (FAILED(hr))
            {
                URHO3D_SAFE_RELEASE(object_.ptr_);
                compilerOutput_ = "Could not create compute shader (HRESULT " + ToStringHex((unsigned)hr) + ")";
            }
        }
        else
            compilerOutput_ = "Could not create compute shader, empty bytecode";
    }

    return object_.ptr_ != nullptr;
}

void ShaderVariation::Release()
{
    if (object_.ptr_)
    {
        if (!graphics_)
            return;

        graphics_->CleanupShaderPrograms(this);

        if (type_ == VS)
        {
            if (graphics_->GetVertexShader() == this)
                graphics_->SetShaders(nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        else if (type_ == PS)
        {
            if (graphics_->GetPixelShader() == this)
                graphics_->SetShaders(nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        else if (type_ == GS)
        {
            if (graphics_->GetGeometryShader() == this)
                graphics_->SetShaders(nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        else if (type_ == HS)
        {
            if (graphics_->GetHullShader() == this)
                graphics_->SetShaders(nullptr, nullptr, nullptr, nullptr, nullptr);
        }
        else if (type_ == DS)
        {
            if (graphics_->GetDomainShader() == this)
                graphics_->SetShaders(nullptr, nullptr, nullptr, nullptr, nullptr);
        }

        URHO3D_SAFE_RELEASE(object_.ptr_);
    }

    compilerOutput_.Clear();

    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
        useTextureUnits_[i] = false;
    for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        constantBufferSizes_[i] = 0;
    parameters_.Clear();
    byteCode_.Clear();
    elementHash_ = 0;
}

void ShaderVariation::SetDefines(const String& defines)
{
    defines_ = defines;

    // Internal mechanism for appending the CLIPPLANE define, prevents runtime (every frame) string manipulation
    definesClipPlane_ = defines;
    if (!definesClipPlane_.EndsWith(" CLIPPLANE"))
        definesClipPlane_ += " CLIPPLANE";
}

bool ShaderVariation::LoadByteCode(const String& binaryShaderName)
{
    ResourceCache* cache = owner_->GetSubsystem<ResourceCache>();
    if (!cache->Exists(binaryShaderName))
        return false;

    FileSystem* fileSystem = owner_->GetSubsystem<FileSystem>();
    unsigned sourceTimeStamp = owner_->GetTimeStamp();
    // If source code is loaded from a package, its timestamp will be zero. Else check that binary is not older
    // than source
    if (sourceTimeStamp && fileSystem->GetLastModifiedTime(cache->GetResourceFileName(binaryShaderName)) < sourceTimeStamp)
        return false;

    SharedPtr<File> file = cache->GetFile(binaryShaderName);
    if (!file || file->ReadFileID() != "USHD")
    {
        URHO3D_LOGERROR(binaryShaderName + " is not a valid shader bytecode file");
        return false;
    }

    /// \todo Check that shader type and model match
    /*unsigned short shaderType = */file->ReadUShort();
    /*unsigned short shaderModel = */file->ReadUShort();
    elementHash_ = file->ReadUInt();
    elementHash_ <<= 32;

    unsigned numParameters = file->ReadUInt();
    for (unsigned i = 0; i < numParameters; ++i)
    {
        String name = file->ReadString();
        unsigned buffer = file->ReadUByte();
        unsigned offset = file->ReadUInt();
        unsigned size = file->ReadUInt();

        parameters_[StringHash(name)] = ShaderParameter{type_, name, offset, size, buffer};
    }

    unsigned numTextureUnits = file->ReadUInt();
    for (unsigned i = 0; i < numTextureUnits; ++i)
    {
        /*String unitName = */file->ReadString();
        unsigned reg = file->ReadUByte();

        if (reg < MAX_TEXTURE_UNITS)
            useTextureUnits_[reg] = true;
    }

    unsigned byteCodeSize = file->ReadUInt();
    if (byteCodeSize)
    {
        byteCode_.Resize(byteCodeSize);
        file->Read(&byteCode_[0], byteCodeSize);

        if (type_ == VS)
            URHO3D_LOGDEBUG("Loaded cached vertex shader " + GetFullName());
        else if (type_ == PS)
            URHO3D_LOGDEBUG("Loaded cached pixel shader " + GetFullName());
        else if (type_ == GS)
            URHO3D_LOGDEBUG("Loaded cached geometry shader " + GetFullName());
        else if (type_ == HS)
            URHO3D_LOGDEBUG("Loaded cached hull shader " + GetFullName());
        else if (type_ == DS)
            URHO3D_LOGDEBUG("Loaded cached domain shader " + GetFullName());
        else if (type_ == CS)
            URHO3D_LOGDEBUG("Loaded cached compute shader " + GetFullName());

        CalculateConstantBufferSizes();
        return true;
    }
    else
    {
        URHO3D_LOGERROR(binaryShaderName + " has zero length bytecode");
        return false;
    }
}

bool ShaderVariation::Compile()
{
    const String& sourceCode = owner_->GetSourceCode(type_);
    Vector<String> defines = defines_.Split(' ');

    // Set the entrypoint, profile and flags according to the shader being compiled
    const char* entryPoint = nullptr;
    const char* profile = nullptr;
    unsigned flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;

    defines.Push("D3D11");

    // If compute is supported then the v5 profiles will be used, which add structured buffer support
    const bool shouldUseV5 = graphics_->GetComputeSupport();
    if (type_ == VS)
    {
        entryPoint = "VS";
        defines.Push("COMPILEVS");
        profile = shouldUseV5 ? "vs_5_0" : "vs_4_0";
    }
    else if (type_ == PS)
    {
        entryPoint = "PS";
        defines.Push("COMPILEPS");
        profile = shouldUseV5 ? "ps_5_0" : "ps_4_0";
        flags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
    }
    else if (type_ == GS)
    {
        entryPoint = "GS";
        defines.Push("COMPILEGS");
        profile = shouldUseV5 ? "gs_5_0" : "gs_4_0";
    }
    else if (type_ == HS)
    {
        entryPoint = "HS";
        defines.Push("COMPILEHS");
        profile = "hs_5_0";
    }
    else if (type_ == DS) 
    {
        entryPoint = "DS";
        defines.Push("COMPILEDS");
        profile = "ds_5_0";
    }
	else if (type_ == CS)
	{
		entryPoint = "CS";
		defines.Push("COMPILECS");
		profile = "cs_5_0";
	}

    defines.Push("MAXBONES=" + String(Graphics::GetMaxBones()));

    // Collect defines into macros
    Vector<String> defineValues;
    PODVector<D3D_SHADER_MACRO> macros;

    for (unsigned i = 0; i < defines.Size(); ++i)
    {
        unsigned equalsPos = defines[i].Find('=');
        if (equalsPos != String::NPOS)
        {
            defineValues.Push(defines[i].Substring(equalsPos + 1));
            defines[i].Resize(equalsPos);
        }
        else
            defineValues.Push("1");
    }
    for (unsigned i = 0; i < defines.Size(); ++i)
    {
        D3D_SHADER_MACRO macro;
        macro.Name = defines[i].CString();
        macro.Definition = defineValues[i].CString();
        macros.Push(macro);

        // In debug mode, check that all defines are referenced by the shader code
#ifdef _DEBUG
        if (sourceCode.Find(defines[i]) == String::NPOS)
            URHO3D_LOGWARNING("Shader " + GetFullName() + " does not use the define " + defines[i]);
#endif
    }

    D3D_SHADER_MACRO endMacro;
    endMacro.Name = nullptr;
    endMacro.Definition = nullptr;
    macros.Push(endMacro);

    // Compile using D3DCompile
    ID3DBlob* shaderCode = nullptr;
    ID3DBlob* errorMsgs = nullptr;
#ifdef DEBUG
	shaderCode_ = sourceCode;
#endif
    HRESULT hr = D3DCompile(sourceCode.CString(), sourceCode.Length(), owner_->GetName().CString(), &macros.Front(), nullptr,
        entryPoint, profile, flags, 0, &shaderCode, &errorMsgs);
    if (FAILED(hr))
    {
        // Do not include end zero unnecessarily
        compilerOutput_ = String((const char*)errorMsgs->GetBufferPointer(), (unsigned)errorMsgs->GetBufferSize() - 1);
    }
    else
    {
        if (type_ == VS)
            URHO3D_LOGDEBUG("Compiled vertex shader " + GetFullName());
        else if (type_ == PS)
            URHO3D_LOGDEBUG("Compiled pixel shader " + GetFullName());
        else if (type_ == GS)
            URHO3D_LOGDEBUG("Compiled geometry shader " + GetFullName());
        else if (type_ == HS)
            URHO3D_LOGDEBUG("Compiled hull shader " + GetFullName());
        else if (type_ == DS)
            URHO3D_LOGDEBUG("Compiled domain shader " + GetFullName());
		else if (type_ == CS)
			URHO3D_LOGDEBUG("Compiled compute shader " + GetFullName());

        unsigned char* bufData = (unsigned char*)shaderCode->GetBufferPointer();
        unsigned bufSize = (unsigned)shaderCode->GetBufferSize();
        // Use the original bytecode to reflect the parameters
        ParseParameters(bufData, bufSize);
        CalculateConstantBufferSizes();

        // Then strip everything not necessary to use the shader
        ID3DBlob* strippedCode = nullptr;
        D3DStripShader(bufData, bufSize,
            D3DCOMPILER_STRIP_REFLECTION_DATA | D3DCOMPILER_STRIP_DEBUG_INFO | D3DCOMPILER_STRIP_TEST_BLOBS, &strippedCode);
        byteCode_.Resize((unsigned)strippedCode->GetBufferSize());
        memcpy(&byteCode_[0], strippedCode->GetBufferPointer(), byteCode_.Size());
        strippedCode->Release();
    }

    URHO3D_SAFE_RELEASE(shaderCode);
    URHO3D_SAFE_RELEASE(errorMsgs);

    return !byteCode_.Empty();
}

void ShaderVariation::ParseParameters(unsigned char* bufData, unsigned bufSize)
{
    ID3D11ShaderReflection* reflection = nullptr;
    D3D11_SHADER_DESC shaderDesc;

    HRESULT hr = D3DReflect(bufData, bufSize, IID_ID3D11ShaderReflection, (void**)&reflection);
    if (FAILED(hr) || !reflection)
    {
        URHO3D_SAFE_RELEASE(reflection);
        URHO3D_LOGD3DERROR("Failed to reflect vertex shader's input signature", hr);
        return;
    }

    reflection->GetDesc(&shaderDesc);

    if (type_ == CS)
    {
        UINT x, y, z;
        reflection->GetThreadGroupSize(&x, &y, &z);
        dispatchSize_ = IntVector3(x, y, z);
    }


    if (type_ == VS)
    {
        elementHash_ = 0;
        for (unsigned i = 0; i < shaderDesc.InputParameters; ++i)
        {
            D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
            reflection->GetInputParameterDesc((UINT)i, &paramDesc);
            VertexElementSemantic semantic = (VertexElementSemantic)GetStringListIndex(paramDesc.SemanticName, elementSemanticNames, MAX_VERTEX_ELEMENT_SEMANTICS, true);
            if (semantic != MAX_VERTEX_ELEMENT_SEMANTICS)
            {
                elementHash_ <<= 4;
                elementHash_ += ((int)semantic + 1) * (paramDesc.SemanticIndex + 1);
            }
        }
        elementHash_ <<= 32;
    }

    HashMap<String, unsigned> cbRegisterMap;

    for (unsigned i = 0; i < shaderDesc.BoundResources; ++i)
    {
        D3D11_SHADER_INPUT_BIND_DESC resourceDesc;
        reflection->GetResourceBindingDesc(i, &resourceDesc);
        String resourceName(resourceDesc.Name);
        if (resourceDesc.Type == D3D_SIT_CBUFFER)
            cbRegisterMap[resourceName] = resourceDesc.BindPoint;
        else if (resourceDesc.Type == D3D_SIT_SAMPLER && resourceDesc.BindPoint < MAX_TEXTURE_UNITS)
            useTextureUnits_[resourceDesc.BindPoint] = true;
    }

    for (unsigned i = 0; i < shaderDesc.ConstantBuffers; ++i)
    {
        ID3D11ShaderReflectionConstantBuffer* cb = reflection->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC cbDesc;
        cb->GetDesc(&cbDesc);
        unsigned cbRegister = cbRegisterMap[String(cbDesc.Name)];

        for (unsigned j = 0; j < cbDesc.Variables; ++j)
        {
            ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(j);
            D3D11_SHADER_VARIABLE_DESC varDesc;
            var->GetDesc(&varDesc);
            String varName(varDesc.Name);
            if (varName[0] == 'c')
            {
                varName = varName.Substring(1); // Strip the c to follow Urho3D constant naming convention
                parameters_[varName] = ShaderParameter{type_, varName, varDesc.StartOffset, varDesc.Size, cbRegister};
            }
        }
    }

    reflection->Release();
}

void ShaderVariation::SaveByteCode(const String& binaryShaderName)
{
    ResourceCache* cache = owner_->GetSubsystem<ResourceCache>();
    FileSystem* fileSystem = owner_->GetSubsystem<FileSystem>();

    // Filename may or may not be inside the resource system
    String fullName = binaryShaderName;
    if (!IsAbsolutePath(fullName))
    {
        // If not absolute, use the resource dir of the shader
        String shaderFileName = cache->GetResourceFileName(owner_->GetName());
        if (shaderFileName.Empty())
            return;
        fullName = shaderFileName.Substring(0, shaderFileName.Find(owner_->GetName())) + binaryShaderName;
    }
    String path = GetPath(fullName);
    if (!fileSystem->DirExists(path))
        fileSystem->CreateDir(path);

    SharedPtr<File> file(new File(owner_->GetContext(), fullName, FILE_WRITE));
    if (!file->IsOpen())
        return;

    file->WriteFileID("USHD");
    file->WriteShort((unsigned short)type_);
    file->WriteShort(4);
    file->WriteUInt(elementHash_ >> 32);

    file->WriteUInt(parameters_.Size());
    for (HashMap<StringHash, ShaderParameter>::ConstIterator i = parameters_.Begin(); i != parameters_.End(); ++i)
    {
        file->WriteString(i->second_.name_);
        file->WriteUByte((unsigned char)i->second_.buffer_);
        file->WriteUInt(i->second_.offset_);
        file->WriteUInt(i->second_.size_);
    }

    unsigned usedTextureUnits = 0;
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        if (useTextureUnits_[i])
            ++usedTextureUnits;
    }
    file->WriteUInt(usedTextureUnits);
    for (unsigned i = 0; i < MAX_TEXTURE_UNITS; ++i)
    {
        if (useTextureUnits_[i])
        {
            file->WriteString(graphics_->GetTextureUnitName((TextureUnit)i));
            file->WriteUByte((unsigned char)i);
        }
    }

    file->WriteUInt(byteCode_.Size());
    if (byteCode_.Size())
        file->Write(&byteCode_[0], byteCode_.Size());
}

void ShaderVariation::CalculateConstantBufferSizes()
{
    for (unsigned i = 0; i < MAX_SHADER_PARAMETER_GROUPS; ++i)
        constantBufferSizes_[i] = 0;

    for (HashMap<StringHash, ShaderParameter>::ConstIterator i = parameters_.Begin(); i != parameters_.End(); ++i)
    {
        if (i->second_.buffer_ < MAX_SHADER_PARAMETER_GROUPS)
        {
            unsigned oldSize = constantBufferSizes_[i->second_.buffer_];
            unsigned paramEnd = i->second_.offset_ + i->second_.size_;
            if (paramEnd > oldSize)
                constantBufferSizes_[i->second_.buffer_] = paramEnd;
        }
    }
}

}
