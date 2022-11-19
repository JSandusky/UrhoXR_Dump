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

#include "../Graphics/Graphics.h"
#include "../Graphics/GraphicsImpl.h"
#include "../Graphics/ShaderPrecache.h"
#include "../Graphics/ShaderVariation.h"
#include "../IO/File.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"

#include "../DebugNew.h"

namespace Urho3D
{

ShaderPrecache::ShaderPrecache(Context* context, const String& fileName) :
    Object(context),
    fileName_(fileName),
    xmlFile_(context)
{
    if (GetSubsystem<FileSystem>()->FileExists(fileName))
    {
        // If file exists, read the already listed combinations
        File source(context_, fileName);
        xmlFile_.Load(source);

        XMLElement shader = xmlFile_.GetRoot().GetChild("shader");
        while (shader)
        {
            String oldCombination = shader.GetAttribute("vs") + " " + shader.GetAttribute("vsdefines") + " " +
                                    shader.GetAttribute("ps") + " " + shader.GetAttribute("psdefines");
#if !defined(GL_ES_VERSION_2_0) && !defined(URHO3D_D3D9)
			if (shader.HasAttribute("gs"))
				oldCombination += shader.GetAttribute("gs") + " " + shader.GetAttribute("gsdefines");
			if (shader.HasAttribute("hs"))
				oldCombination += shader.GetAttribute("hs") + " " + shader.GetAttribute("hsdefines");
			if (shader.HasAttribute("ds"))
				oldCombination += shader.GetAttribute("ds") + " " + shader.GetAttribute("dsdefines");
#endif
            usedCombinations_.Insert(oldCombination);

            shader = shader.GetNext("shader");
        }
    }

    // If no file yet or loading failed, create the root element now
    if (!xmlFile_.GetRoot())
        xmlFile_.CreateRoot("shaders");

    URHO3D_LOGINFO("Begin dumping shaders to " + fileName_);
}

ShaderPrecache::~ShaderPrecache()
{
    URHO3D_LOGINFO("End dumping shaders");

    if (usedCombinations_.Empty())
        return;

    File dest(context_, fileName_, FILE_WRITE);
    xmlFile_.Save(dest);
}

void ShaderPrecache::StoreShaders(ShaderVariation* vs, ShaderVariation* ps, ShaderVariation* gs, ShaderVariation* hs, ShaderVariation* ds)
{
    if (!vs || !ps)
        return;

    // Check for duplicate using pointers first (fast)
    ShaderCombination shaderCombo = { vs, ps, gs, hs, ds };
    if (usedPtrCombinations_.Contains(shaderCombo))
        return;
    usedPtrCombinations_.Insert(shaderCombo);

    String vsName = vs->GetName();
    String psName = ps->GetName();
    const String& vsDefines = vs->GetDefines();
    const String& psDefines = ps->GetDefines();

    // Check for duplicate using strings (needed for combinations loaded from existing file)
    String newCombination = vsName + " " + vsDefines + " " + psName + " " + psDefines;


#if !defined(GL_ES_VERSION_2_0) && !defined(URHO3D_D3D9)
    if (gs)
        newCombination += gs->GetName() + " " + gs->GetDefines();
    if (hs)
        newCombination += hs->GetName() + " " + hs->GetDefines();
    if (ds)
        newCombination += ds->GetName() + " " + ds->GetDefines();
#endif

    if (usedCombinations_.Contains(newCombination))
        return;
    usedCombinations_.Insert(newCombination);

    XMLElement shaderElem = xmlFile_.GetRoot().CreateChild("shader");
    shaderElem.SetAttribute("vs", vsName);
    shaderElem.SetAttribute("vsdefines", vsDefines);
    shaderElem.SetAttribute("ps", psName);
    shaderElem.SetAttribute("psdefines", psDefines);

#if !defined(GL_ES_VERSION_2_0) && !defined(URHO3D_D3D9)
    if (gs)
    {
        shaderElem.SetAttribute("gs", gs->GetName());
        shaderElem.SetAttribute("gsdefines", gs->GetDefines());
    }
    if (hs)
    {
        shaderElem.SetAttribute("hs", hs->GetName());
        shaderElem.SetAttribute("hsdefines", hs->GetDefines());
    }
    if (ds)
    {
        shaderElem.SetAttribute("ds", ds->GetName());
        shaderElem.SetAttribute("dsdefines", ds->GetDefines());
    }
#endif
}

void ShaderPrecache::LoadShaders(Graphics* graphics, Deserializer& source)
{
    URHO3D_LOGDEBUG("Begin precaching shaders");

    XMLFile xmlFile(graphics->GetContext());
    xmlFile.Load(source);

    XMLElement shader = xmlFile.GetRoot().GetChild("shader");
    while (shader)
    {
        String vsDefines = shader.GetAttribute("vsdefines");
#if !defined(GL_ES_VERSION_2_0) && !defined(URHO3D_D3D9)
        String gsDefines = shader.GetAttribute("gsdefines");
        String hsDefines = shader.GetAttribute("hsdefines");
        String dsDefines = shader.GetAttribute("dsdefines");
#endif
        String psDefines = shader.GetAttribute("psdefines");

        // Check for illegal variations on OpenGL ES and skip them
#ifdef GL_ES_VERSION_2_0
        if (
#ifndef __EMSCRIPTEN__
            vsDefines.Contains("INSTANCED") ||
#endif
            (psDefines.Contains("POINTLIGHT") && psDefines.Contains("SHADOW")))
        {
            shader = shader.GetNext("shader");
            continue;
        }
#endif

#if !defined(GL_ES_VERSION_2_0) && !defined(URHO3D_D3D9)
        ShaderVariation* vs = graphics->GetShader(VS, shader.GetAttribute("vs"), vsDefines);
        ShaderVariation* hs = graphics->GetShader(HS, shader.GetAttribute("hs"), hsDefines);
        ShaderVariation* ds = graphics->GetShader(DS, shader.GetAttribute("ds"), dsDefines);
        ShaderVariation* gs = graphics->GetShader(GS, shader.GetAttribute("gs"), gsDefines);
        ShaderVariation* ps = graphics->GetShader(PS, shader.GetAttribute("ps"), psDefines);
        // Set the shaders active to actually compile them
        graphics->SetShaders(vs, ps, gs, hs, ds);
#else
        ShaderVariation* vs = graphics->GetShader(VS, shader.GetAttribute("vs"), vsDefines);
        ShaderVariation* ps = graphics->GetShader(PS, shader.GetAttribute("ps"), psDefines);
        // Set the shaders active to actually compile them
        graphics->SetShaders(vs, ps, nullptr, nullptr, nullptr);
#endif

        shader = shader.GetNext("shader");
    }

    URHO3D_LOGDEBUG("End precaching shaders");
}

}
