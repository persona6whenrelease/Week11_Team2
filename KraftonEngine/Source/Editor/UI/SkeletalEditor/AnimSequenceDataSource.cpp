#include "Editor/UI/SkeletalEditor/AnimSequenceDataSource.h"

#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Notify/AnimNotify.h"

namespace
{
const TArray<FBoneAnimationTrack> EmptyTracks;
}

FUAnimSequenceDataSource::FUAnimSequenceDataSource(UAnimSequence *InSequence) : Sequence(InSequence)
{
    RebuildNotifyCache();
}

FString FUAnimSequenceDataSource::GetName() const
{
    if (!Sequence)
        return "<no anim sequence>";

    const FString &SequenceName = Sequence->GetSequenceName();
    return SequenceName.empty() ? Sequence->GetFName().ToString() : SequenceName;
}

float FUAnimSequenceDataSource::GetDuration() const
{
    const UAnimDataModel *DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
    return DataModel ? DataModel->GetPlayLength() : 0.0f;
}

float FUAnimSequenceDataSource::GetFrameRate() const
{
    const UAnimDataModel *DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
    return DataModel ? DataModel->GetFrameRate().AsDecimal() : 0.0f;
}

int32 FUAnimSequenceDataSource::GetFrameCount() const
{
    const UAnimDataModel *DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
    return DataModel ? DataModel->GetNumberOfFrames() : 0;
}

const TArray<FBoneAnimationTrack> &FUAnimSequenceDataSource::GetTracks() const
{
    const UAnimDataModel *DataModel = Sequence ? Sequence->GetDataModel() : nullptr;
    return DataModel ? DataModel->GetBoneAnimationTracks() : EmptyTracks;
}

int32 FUAnimSequenceDataSource::AddNotify(const FAnimNotifyEntry &Notify)
{
    if (!Sequence)
        return -1;

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

    TArray<FAnimNotifyEvent> &AssetNotifies = Sequence->GetMutableNotifies();
    if (Index >= static_cast<int32>(AssetNotifies.size()))
    {
        return false;
    }

    AssetNotifies.erase(AssetNotifies.begin() + Index);
    CachedNotifyEntries.erase(CachedNotifyEntries.begin() + Index);
    return true;
}

bool FUAnimSequenceDataSource::UpdateNotify(int32 Index, const FAnimNotifyEntry &Notify)
{
    if (!Sequence || Index < 0 || Index >= static_cast<int32>(CachedNotifyEntries.size()))
    {
        return false;
    }

    TArray<FAnimNotifyEvent> &AssetNotifies = Sequence->GetMutableNotifies();
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
    if (!Sequence)
        return;

    for (const FAnimNotifyEvent &Notify : Sequence->GetNotifies())
    {
        FAnimNotifyEntry Entry;
        Entry.Name = Notify.NotifyName.ToString();
        Entry.TriggerTime = Notify.TriggerTime;
        Entry.Duration = Notify.Duration;
        CachedNotifyEntries.push_back(Entry);
    }
}

void FUAnimSequenceDataSource::WriteNotifyToAsset(int32 Index, const FAnimNotifyEntry &Notify)
{
    TArray<FAnimNotifyEvent> &AssetNotifies = Sequence->GetMutableNotifies();
    FAnimNotifyEvent &Event = AssetNotifies[Index];
    Event.NotifyName = FName(Notify.Name);
    Event.TriggerTime = Notify.TriggerTime;
    Event.Duration = Notify.Duration;
}
