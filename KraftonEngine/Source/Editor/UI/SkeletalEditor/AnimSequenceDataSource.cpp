#include "Editor/UI/SkeletalEditor/AnimSequenceDataSource.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Notify/AnimNotify.h"

namespace
{
	const TArray<FBoneAnimationTrack> EmptyTracks;

	const UAnimDataModel* GetDataModel(const UAnimSequence* Sequence)
	{
		return Sequence ? Sequence->GetDataModel() : nullptr;
	}
}

FUAnimSequenceDataSource::FUAnimSequenceDataSource(UAnimSequence* InSequence)
	: Sequence(InSequence)
{
	RebuildNotifyCache();
}

FString FUAnimSequenceDataSource::GetName() const
{
	if (!Sequence) return "<no anim sequence>";
	return Sequence->GetSequenceName().empty() ? Sequence->GetFName().ToString() : Sequence->GetSequenceName();
}

float FUAnimSequenceDataSource::GetDuration() const
{
	return Sequence ? Sequence->GetPlayLength() : 0.0f;
}

float FUAnimSequenceDataSource::GetFrameRate() const
{
	const UAnimDataModel* Model = GetDataModel(Sequence);
	return Model ? Model->GetFrameRate().AsDecimal() : 0.0f;
}

int32 FUAnimSequenceDataSource::GetFrameCount() const
{
	const UAnimDataModel* Model = GetDataModel(Sequence);
	return Model ? Model->GetNumberOfFrames() : 0;
}

const TArray<FBoneAnimationTrack>& FUAnimSequenceDataSource::GetTracks() const
{
	const UAnimDataModel* Model = GetDataModel(Sequence);
	return Model ? Model->GetBoneAnimationTracks() : EmptyTracks;
}

int32 FUAnimSequenceDataSource::AddNotify(const FAnimNotifyEntry& Notify)
{
	if (!Sequence) return -1;

	FAnimNotifyEvent Event;
	Event.NotifyName = FName(Notify.Name);
	Event.TriggerTime = Notify.TriggerTime;
	Event.Duration = Notify.Duration;
	Sequence->AddNotify(Event);

	CachedNotifyEntries.push_back(Notify);
	return static_cast<int32>(CachedNotifyEntries.size()) - 1;
}

bool FUAnimSequenceDataSource::RemoveNotify(int32 Index)
{
	if (!Sequence || Index < 0 || Index >= static_cast<int32>(CachedNotifyEntries.size()))
	{
		return false;
	}

	TArray<FAnimNotifyEvent>& AssetNotifies = Sequence->GetMutableNotifies();
	if (Index >= static_cast<int32>(AssetNotifies.size()))
	{
		return false;
	}

	AssetNotifies.erase(AssetNotifies.begin() + Index);
	CachedNotifyEntries.erase(CachedNotifyEntries.begin() + Index);
	return true;
}

bool FUAnimSequenceDataSource::UpdateNotify(int32 Index, const FAnimNotifyEntry& Notify)
{
	if (!Sequence || Index < 0 || Index >= static_cast<int32>(CachedNotifyEntries.size()))
	{
		return false;
	}

	TArray<FAnimNotifyEvent>& AssetNotifies = Sequence->GetMutableNotifies();
	if (Index >= static_cast<int32>(AssetNotifies.size()))
	{
		return false;
	}

	CachedNotifyEntries[Index] = Notify;
	WriteNotifyToAsset(Index, Notify);
	return true;
}

void FUAnimSequenceDataSource::RebuildNotifyCache()
{
	CachedNotifyEntries.clear();
	if (!Sequence) return;

	for (const FAnimNotifyEvent& Notify : Sequence->GetNotifies())
	{
		FAnimNotifyEntry Entry;
		Entry.Name = Notify.NotifyName.ToString();
		Entry.TriggerTime = Notify.TriggerTime;
		Entry.Duration = Notify.Duration;
		CachedNotifyEntries.push_back(Entry);
	}
}

void FUAnimSequenceDataSource::WriteNotifyToAsset(int32 Index, const FAnimNotifyEntry& Notify)
{
	TArray<FAnimNotifyEvent>& AssetNotifies = Sequence->GetMutableNotifies();
	FAnimNotifyEvent& Event = AssetNotifies[Index];
	Event.NotifyName = FName(Notify.Name);
	Event.TriggerTime = Notify.TriggerTime;
	Event.Duration = Notify.Duration;
}
