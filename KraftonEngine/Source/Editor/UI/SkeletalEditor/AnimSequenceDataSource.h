#pragma once

#include "Core/CoreTypes.h"

#include <cstdint>

class UAnimSequence;
struct FAnimationClip;
struct FBoneAnimationTrack;

// Notify 한 개의 임시 표현. UAnimSequence 본격 도입 전까지 viewer 내부에서만 사용한다.
// 추후 엔진의 정식 FAnimNotifyEvent로 교체된다.
struct FAnimNotifyEntry
{
	FString Name = "Notify";
	float   TriggerTime = 0.0f;
	float   Duration = 0.0f;            // 0 = 즉시(instant), >0 = 범위(state) notify
	uint32  ColorPacked = 0xFF80E080;   // ImGui IM_COL32 형식 (ABGR)
};

// AnimSequence Viewer가 timeline / track UI를 그리기 위해 필요한 최소 인터페이스.
// UAnimSequence asset이 들어오면 그 위의 어댑터(FUAnimSequenceDataSource)로 교체하면 된다.
class IAnimSequenceDataSource
{
public:
	virtual ~IAnimSequenceDataSource() = default;

	// 기본 정보
	virtual FString GetName() const = 0;
	virtual float   GetDuration() const = 0;
	virtual float   GetFrameRate() const = 0;
	virtual int32   GetFrameCount() const = 0;
	virtual const TArray<FBoneAnimationTrack>& GetTracks() const = 0;

	// Notify 편집 — 현재는 어댑터 메모리에만 보관(transient).
	// UAnimSequence 어댑터가 들어오면 실제 asset에 쓰기/저장.
	virtual const TArray<FAnimNotifyEntry>& GetNotifies() const = 0;
	virtual int32 AddNotify(const FAnimNotifyEntry& Notify) = 0;
	virtual bool  RemoveNotify(int32 Index) = 0;
	virtual bool  UpdateNotify(int32 Index, const FAnimNotifyEntry& Notify) = 0;
};

// 임시 어댑터: SkeletalMesh asset에 baked되어 있는 FAnimationClip 한 개를 AnimSequence처럼 노출.
// Notify는 어댑터 인스턴스 안에만 저장 — 탭을 닫으면 사라진다.
class FUAnimSequenceDataSource : public IAnimSequenceDataSource
{
public:
	explicit FUAnimSequenceDataSource(UAnimSequence* InSequence);

	FString GetName() const override;
	float   GetDuration() const override;
	float   GetFrameRate() const override;
	int32   GetFrameCount() const override;
	const TArray<FBoneAnimationTrack>& GetTracks() const override;

	const TArray<FAnimNotifyEntry>& GetNotifies() const override { return CachedNotifyEntries; }
	int32 AddNotify(const FAnimNotifyEntry& Notify) override;
	bool  RemoveNotify(int32 Index) override;
	bool  UpdateNotify(int32 Index, const FAnimNotifyEntry& Notify) override;

	UAnimSequence* GetSequence() const { return Sequence; }

private:
	void RebuildNotifyCache();
	void WriteNotifyToAsset(int32 Index, const FAnimNotifyEntry& Notify);

	UAnimSequence* Sequence = nullptr;
	TArray<FAnimNotifyEntry> CachedNotifyEntries;
};
