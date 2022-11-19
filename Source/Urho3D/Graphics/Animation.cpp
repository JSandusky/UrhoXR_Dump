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

#include "../Container/Sort.h"
#include "../Core/Context.h"
#include "../Core/Profiler.h"
#include "../Graphics/Animation.h"
#include "../IO/Deserializer.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"
#include "../IO/Serializer.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/XMLFile.h"
#include "../Resource/JSONFile.h"

#include "../DebugNew.h"

namespace Urho3D
{

inline bool CompareTriggers(AnimationTriggerPoint& lhs, AnimationTriggerPoint& rhs)
{
    return lhs.time_ < rhs.time_;
}

inline bool CompareKeyFrames(AnimationKeyFrame& lhs, AnimationKeyFrame& rhs)
{
    return lhs.time_ < rhs.time_;
}

void AnimationTrack::SetKeyFrame(unsigned index, const AnimationKeyFrame& keyFrame)
{
    if (index < keyFrames_.Size())
    {
        keyFrames_[index] = keyFrame;
        Urho3D::Sort(keyFrames_.Begin(), keyFrames_.End(), CompareKeyFrames);
    }
    else if (index == keyFrames_.Size())
        AddKeyFrame(keyFrame);
}

void AnimationTrack::AddKeyFrame(const AnimationKeyFrame& keyFrame)
{
    bool needSort = keyFrames_.Size() ? keyFrames_.Back().time_ > keyFrame.time_ : false;
    keyFrames_.Push(keyFrame);
    if (needSort)
        Urho3D::Sort(keyFrames_.Begin(), keyFrames_.End(), CompareKeyFrames);
}

void AnimationTrack::InsertKeyFrame(unsigned index, const AnimationKeyFrame& keyFrame)
{
    keyFrames_.Insert(index, keyFrame);
    Urho3D::Sort(keyFrames_.Begin(), keyFrames_.End(), CompareKeyFrames);
}

void AnimationTrack::RemoveKeyFrame(unsigned index)
{
    keyFrames_.Erase(index);
}

void AnimationTrack::RemoveAllKeyFrames()
{
    keyFrames_.Clear();
}

AnimationKeyFrame* AnimationTrack::GetKeyFrame(unsigned index)
{
    return index < keyFrames_.Size() ? &keyFrames_[index] : nullptr;
}

void AnimationTrack::GetKeyFrameIndex(float time, unsigned& index) const
{
    if (time < 0.0f)
        time = 0.0f;

    if (index >= keyFrames_.Size())
        index = keyFrames_.Size() - 1;

    // Check for being too far ahead
    while (index && time < keyFrames_[index].time_)
        --index;

    // Check for being too far behind
    while (index < keyFrames_.Size() - 1 && time >= keyFrames_[index + 1].time_)
        ++index;
}

float MorphTrack::GetWeight(float time) const
{
	// no keys, then always 0
	if (keyFrames_.Empty())
		return 0.0f;

	// one key, then it's constant
	if (keyFrames_.Size() == 1)
		return keyFrames_[0].second_;

	if (time < 0.0f)
		time = 0.0f;

	float t = time;
	unsigned firstKey = 0;
	unsigned lastKey = 0;
	while (firstKey < keyFrames_.Size() && t > keyFrames_[firstKey].first_)
		firstKey += 1;

	while (lastKey < keyFrames_.Size() && t < keyFrames_[lastKey].first_)
		lastKey += 1;

	firstKey = Min(firstKey, keyFrames_.Size() - 1);
	lastKey = Min(lastKey, keyFrames_.Size() - 1);

	auto openKey = keyFrames_[firstKey];
	auto closeKey = keyFrames_[lastKey];

	float timeDiff = closeKey.first_ - openKey.first_;
	float fracTime = t - openKey.first_;
	float ratio = fracTime / timeDiff;

	return Lerp(openKey.second_, closeKey.second_, ratio);
}

bool AnimationPhase::IsInside(float time, float animLength) const 
{ 
	// wraps around the end?
	if (end_ < start_)
		return time <= end_ || time >= start_;
	return time >= start_ && time <= end_;
}

float AnimationPhase::GetFraction(float time, float animLength) const 
{ 
	float end = end_;
	if (end_ < start_ && time <= end_)
	{
		end += animLength; // knock it all the way to the end
		time += animLength; // shift our time as well
	}

	// because of the shift above, standard normalization will be fine
	return (time - start_) / (end - start_); 
}

float AnimationPhase::FractionToTime(float fraction, float animLength) const 
{ 
	float baseLineTime = start_ + GetLength(animLength) * fraction;
	// wrap around back the the tail
	return fmodf(baseLineTime, animLength);
}

bool AnimationPhase::FindPhase(const StringHash& tag, const PODVector<AnimationPhase>& phases, AnimationPhase* phase)
{
    for (auto p : phases)
    {
        if (p.nameHash_ == tag)
        {
            if (phase)
                *phase = p;
            return true;
        }
    }
    return false;
}

bool AnimationPhase::FindPhase(const String& name, const PODVector<AnimationPhase>& phases, AnimationPhase* phase)
{
    for (auto p : phases)
    {
        if (p.phaseName_ == name)
        {
            if (phase)
                *phase = p;
            return true;
        }
    }
    return false;
}

Animation::Animation(Context* context) :
    ResourceWithMetadata(context),
    length_(0.f)
{
}

Animation::~Animation()
{
}

void Animation::RegisterObject(Context* context)
{
    context->RegisterFactory<Animation>();
}

bool Animation::BeginLoad(Deserializer& source)
{
    unsigned memoryUse = sizeof(Animation);

    // Check ID
	auto fileID = source.ReadFileID();
    if (fileID != "UANI" && fileID != "UAN2")
    {
        URHO3D_LOGERROR(source.GetName() + " is not a valid animation file");
        return false;
    }
	const bool isVersion2 = fileID == "UAN2";

    // Read name and length
    animationName_ = source.ReadString();
    animationNameHash_ = animationName_;
    length_ = source.ReadFloat();
    tracks_.Clear();

    unsigned tracks = source.ReadUInt();
    memoryUse += tracks * sizeof(AnimationTrack);

    // Read tracks
    for (unsigned i = 0; i < tracks; ++i)
    {
        AnimationTrack* newTrack = CreateTrack(source.ReadString());
        newTrack->channelMask_ = source.ReadUByte();

        unsigned keyFrames = source.ReadUInt();
        newTrack->keyFrames_.Resize(keyFrames);
        memoryUse += keyFrames * sizeof(AnimationKeyFrame);

        // Read keyframes of the track
        for (unsigned j = 0; j < keyFrames; ++j)
        {
            AnimationKeyFrame& newKeyFrame = newTrack->keyFrames_[j];
            newKeyFrame.time_ = source.ReadFloat();
            if (newTrack->channelMask_ & CHANNEL_POSITION)
                newKeyFrame.position_ = source.ReadVector3();
            if (newTrack->channelMask_ & CHANNEL_ROTATION)
                newKeyFrame.rotation_ = source.ReadQuaternion();
            if (newTrack->channelMask_ & CHANNEL_SCALE)
                newKeyFrame.scale_ = source.ReadVector3();
        }
    }

	// Read morph tracks and phases if version 2.0
	if (isVersion2)
	{
		unsigned morphTrackCt = source.ReadUInt();
		for (unsigned i = 0; i < morphTrackCt; ++i)
		{
            MorphTrack track;
            track.morphTarget_ = source.ReadString();
			unsigned weightKeys = source.ReadUInt();
			for (unsigned w = 0; w < weightKeys; ++w)
			{
				float time = source.ReadFloat();
				float weight = source.ReadFloat();
                track.keyFrames_.Push(MakePair(time, weight));
			}
            if (!track.keyFrames_.Empty())
                morphTracks_.Push(track);
		}

		// phases can be written into binary file
        unsigned phaseCt = source.ReadUInt();
        for (unsigned i = 0; i < phaseCt; ++i)
        {
            String phaseName = source.ReadString();
            float phaseStart = source.ReadFloat();
            float phaseEnd = source.ReadFloat();
            phases_.Push({ phaseName, phaseName, phaseStart, phaseEnd });
        }
	}

    // Optionally read triggers from an XML file
    auto* cache = GetSubsystem<ResourceCache>();
    String xmlName = ReplaceExtension(GetName(), ".xml");

    SharedPtr<XMLFile> file(cache->GetTempResource<XMLFile>(xmlName, false));
    if (file)
    {
		XMLElement rootElem = file->GetRoot();
		for (XMLElement triggerElem = rootElem.GetChild("trigger"); triggerElem; triggerElem = triggerElem.GetNext("trigger"))
		{
			if (triggerElem.HasAttribute("normalizedtime"))
				AddTrigger(triggerElem.GetFloat("normalizedtime"), true, triggerElem.GetVariant());
			else if (triggerElem.HasAttribute("time"))
				AddTrigger(triggerElem.GetFloat("time"), false, triggerElem.GetVariant());
			else if (triggerElem.HasAttribute("key") && tracks_.Size() > 0)
			{
				unsigned keyIndex = triggerElem.GetUInt("key");
				if (auto key = GetTrack(0)->GetKeyFrame(keyIndex))
					AddTrigger(key->time_, false, triggerElem.GetVariant());
				else
				{
					const String reportName = GetName();
					URHO3D_LOGERRORF("Unable to find a key for trigger: %u in %s", keyIndex, reportName.CString());
				}
			}
		}

		for (XMLElement phaseElem = rootElem.GetChild("phase"); phaseElem; phaseElem = phaseElem.GetNext("phase"))
		{
			String phaseName = phaseElem.GetAttributeCString("name");
			float startTime = phaseElem.GetFloat("start");
			float endTime = phaseElem.GetFloat("end");
			phases_.Push({ phaseName, phaseName, startTime, endTime });
		}

        LoadMetadataFromXML(rootElem);

        memoryUse += triggers_.Size() * sizeof(AnimationTriggerPoint);
        SetMemoryUse(memoryUse);
        return true;
    }

    // Optionally read triggers from a JSON file
    String jsonName = ReplaceExtension(GetName(), ".json");

    SharedPtr<JSONFile> jsonFile(cache->GetTempResource<JSONFile>(jsonName, false));
    if (jsonFile)
    {
        const JSONValue& rootVal = jsonFile->GetRoot();
        const JSONArray& triggerArray = rootVal.Get("triggers").GetArray();

        for (unsigned i = 0; i < triggerArray.Size(); i++)
        {
            const JSONValue& triggerValue = triggerArray.At(i);
            JSONValue normalizedTimeValue = triggerValue.Get("normalizedTime");
            if (!normalizedTimeValue.IsNull())
                AddTrigger(normalizedTimeValue.GetFloat(), true, triggerValue.GetVariant());
            else
            {
                JSONValue timeVal = triggerValue.Get("time");
                if (!timeVal.IsNull())
                    AddTrigger(timeVal.GetFloat(), false, triggerValue.GetVariant());
            }
        }

        const JSONArray& metadataArray = rootVal.Get("metadata").GetArray();
        LoadMetadataFromJSON(metadataArray);

        memoryUse += triggers_.Size() * sizeof(AnimationTriggerPoint);
        SetMemoryUse(memoryUse);
        return true;
    }

    SetMemoryUse(memoryUse);
    return true;
}

bool Animation::Save(Serializer& dest) const
{
    // Write ID, name and length
    dest.WriteFileID("UANI");
    dest.WriteString(animationName_);
    dest.WriteFloat(length_);

    // Write tracks
    dest.WriteUInt(tracks_.Size());
    for (HashMap<StringHash, AnimationTrack>::ConstIterator i = tracks_.Begin(); i != tracks_.End(); ++i)
    {
        const AnimationTrack& track = i->second_;
        dest.WriteString(track.name_);
        dest.WriteUByte(track.channelMask_);
        dest.WriteUInt(track.keyFrames_.Size());

        // Write keyframes of the track
        for (unsigned j = 0; j < track.keyFrames_.Size(); ++j)
        {
            const AnimationKeyFrame& keyFrame = track.keyFrames_[j];
            dest.WriteFloat(keyFrame.time_);
            if (track.channelMask_ & CHANNEL_POSITION)
                dest.WriteVector3(keyFrame.position_);
            if (track.channelMask_ & CHANNEL_ROTATION)
                dest.WriteQuaternion(keyFrame.rotation_);
            if (track.channelMask_ & CHANNEL_SCALE)
                dest.WriteVector3(keyFrame.scale_);
        }
    }

    // If triggers have been defined, write an XML file for them
    if (!triggers_.Empty() || HasMetadata())
    {
        auto* destFile = dynamic_cast<File*>(&dest);
        if (destFile)
        {
            String xmlName = ReplaceExtension(destFile->GetName(), ".xml");

            SharedPtr<XMLFile> xml(new XMLFile(context_));
            XMLElement rootElem = xml->CreateRoot("animation");

            for (unsigned i = 0; i < triggers_.Size(); ++i)
            {
                XMLElement triggerElem = rootElem.CreateChild("trigger");
                triggerElem.SetFloat("time", triggers_[i].time_);
                triggerElem.SetVariant(triggers_[i].data_);
            }

            SaveMetadataToXML(rootElem);

            File xmlFile(context_, xmlName, FILE_WRITE);
            xml->Save(xmlFile);
        }
        else
            URHO3D_LOGWARNING("Can not save animation trigger data when not saving into a file");
    }

    return true;
}

void Animation::SetAnimationName(const String& name)
{
    animationName_ = name;
    animationNameHash_ = StringHash(name);
}

void Animation::SetLength(float length)
{
    length_ = Max(length, 0.0f);
}

AnimationTrack* Animation::CreateTrack(const String& name)
{
    /// \todo When tracks / keyframes are created dynamically, memory use is not updated
    StringHash nameHash(name);
    AnimationTrack* oldTrack = GetTrack(nameHash);
    if (oldTrack)
        return oldTrack;

    AnimationTrack& newTrack = tracks_[nameHash];
    newTrack.name_ = name;
    newTrack.nameHash_ = nameHash;
    return &newTrack;
}

bool Animation::RemoveTrack(const String& name)
{
    HashMap<StringHash, AnimationTrack>::Iterator i = tracks_.Find(StringHash(name));
    if (i != tracks_.End())
    {
        tracks_.Erase(i);
        return true;
    }
    else
        return false;
}

void Animation::RemoveAllTracks()
{
    tracks_.Clear();
}

void Animation::SetTrigger(unsigned index, const AnimationTriggerPoint& trigger)
{
    if (index == triggers_.Size())
        AddTrigger(trigger);
    else if (index < triggers_.Size())
    {
        triggers_[index] = trigger;
        Sort(triggers_.Begin(), triggers_.End(), CompareTriggers);
    }
}

void Animation::AddTrigger(const AnimationTriggerPoint& trigger)
{
    triggers_.Push(trigger);
    Sort(triggers_.Begin(), triggers_.End(), CompareTriggers);
}

void Animation::AddTrigger(float time, bool timeIsNormalized, const Variant& data)
{
    AnimationTriggerPoint newTrigger;
    newTrigger.time_ = timeIsNormalized ? time * length_ : time;
    newTrigger.data_ = data;
    triggers_.Push(newTrigger);

    Sort(triggers_.Begin(), triggers_.End(), CompareTriggers);
}

void Animation::RemoveTrigger(unsigned index)
{
    if (index < triggers_.Size())
        triggers_.Erase(index);
}

void Animation::RemoveAllTriggers()
{
    triggers_.Clear();
}

void Animation::SetNumTriggers(unsigned num)
{
    triggers_.Resize(num);
}

SharedPtr<Animation> Animation::Clone(const String& cloneName) const
{
    SharedPtr<Animation> ret(new Animation(context_));

    ret->SetName(cloneName);
    ret->SetAnimationName(animationName_);
    ret->length_ = length_;
    ret->tracks_ = tracks_;
    ret->triggers_ = triggers_;
    ret->CopyMetadata(*this);
    ret->SetMemoryUse(GetMemoryUse());

    return ret;
}

AnimationTrack* Animation::GetTrack(unsigned index)
{
    if (index >= GetNumTracks())
        return nullptr;

    int j = 0;
    for(HashMap<StringHash, AnimationTrack>::Iterator i = tracks_.Begin(); i != tracks_.End(); ++i)
    {
        if (j == index)
            return &i->second_;

        ++j;
    }

    return nullptr;
}

AnimationTrack* Animation::GetTrack(const String& name)
{
    HashMap<StringHash, AnimationTrack>::Iterator i = tracks_.Find(StringHash(name));
    return i != tracks_.End() ? &i->second_ : nullptr;
}

AnimationTrack* Animation::GetTrack(StringHash nameHash)
{
    HashMap<StringHash, AnimationTrack>::Iterator i = tracks_.Find(nameHash);
    return i != tracks_.End() ? &i->second_ : nullptr;
}

AnimationTriggerPoint* Animation::GetTrigger(unsigned index)
{
    return index < triggers_.Size() ? &triggers_[index] : nullptr;
}

void Animation::GetActivePhases(float atTime, PODVector<AnimationPhase>& holder)
{
    for (unsigned i = 0; i < phases_.Size(); ++i)
    {
        if (phases_[i].IsInside(atTime, length_))
            holder.Push(phases_[i]);
    }
}

bool Animation::IsInPhase(const StringHash& phaseName, float atTime) const
{
    for (unsigned i = 0; i < phases_.Size(); ++i)
    {
        if (phases_[i].nameHash_ == phaseName && phases_[i].IsInside(atTime, length_))
            return true;
    }
    return false;
}

float Animation::GetTimeInPhase(const StringHash& phaseName, float atTime) const
{
	for (unsigned i = 0; i < phases_.Size(); ++i)
	{
		if (phases_[i].nameHash_ == phaseName && phases_[i].IsInside(atTime, length_))
			return phases_[i].GetFraction(atTime, length_);
	}
	return M_INFINITY;
}

const AnimationPhase* Animation::GetPhase(const StringHash& phaseName) const
{
	for (unsigned i = 0; i < phases_.Size(); ++i)
	{
		if (phases_[i].nameHash_ == phaseName)
			return &phases_[i];
	}
	return nullptr;
}

bool Animation::HasPhase(const StringHash& phaseName) const
{
	for (unsigned i = 0; i < phases_.Size(); ++i)
	{
		if (phases_[i].nameHash_ == phaseName)
			return true;
	}
	return false;
}

}
