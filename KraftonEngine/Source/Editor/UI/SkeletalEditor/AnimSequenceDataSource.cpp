#include "Editor/UI/SkeletalEditor/AnimSequenceDataSource.h"

#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshAsset.h"

namespace
{
	const TArray<FBoneAnimTrack> EmptyTracks;
}

FFbxClipAnimDataSource::FFbxClipAnimDataSource(USkeletalMesh* InMesh, int32 InClipIndex)
	: Mesh(InMesh)
	, ClipIndex(InClipIndex)
{
}

const FAnimationClip* FFbxClipAnimDataSource::GetClip() const
{
	const FSkeletalMesh* Asset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset) return nullptr;
	if (ClipIndex < 0 || ClipIndex >= static_cast<int32>(Asset->AnimationClips.size())) return nullptr;
	return &Asset->AnimationClips[ClipIndex];
}

FString FFbxClipAnimDataSource::GetName() const
{
	const FAnimationClip* Clip = GetClip();
	return Clip ? Clip->Name : FString("<no clip>");
}

float FFbxClipAnimDataSource::GetDuration() const
{
	const FAnimationClip* Clip = GetClip();
	return Clip ? Clip->Duration : 0.0f;
}

float FFbxClipAnimDataSource::GetFrameRate() const
{
	const FAnimationClip* Clip = GetClip();
	return Clip ? Clip->FrameRate : 0.0f;
}

int32 FFbxClipAnimDataSource::GetFrameCount() const
{
	const FAnimationClip* Clip = GetClip();
	return Clip ? Clip->FrameCount : 0;
}

const TArray<FBoneAnimTrack>& FFbxClipAnimDataSource::GetTracks() const
{
	const FAnimationClip* Clip = GetClip();
	return Clip ? Clip->Tracks : EmptyTracks;
}

int32 FFbxClipAnimDataSource::AddNotify(const FAnimNotifyEntry& Notify)
{
	SessionNotifies.push_back(Notify);
	return static_cast<int32>(SessionNotifies.size()) - 1;
}

bool FFbxClipAnimDataSource::RemoveNotify(int32 Index)
{
	if (Index < 0 || Index >= static_cast<int32>(SessionNotifies.size())) return false;
	SessionNotifies.erase(SessionNotifies.begin() + Index);
	return true;
}

bool FFbxClipAnimDataSource::UpdateNotify(int32 Index, const FAnimNotifyEntry& Notify)
{
	if (Index < 0 || Index >= static_cast<int32>(SessionNotifies.size())) return false;
	SessionNotifies[Index] = Notify;
	return true;
}
