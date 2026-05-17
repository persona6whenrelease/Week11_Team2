/**
 * 애니메이션 시퀀스 에셋 객체를 선언한다.
 *
 * UAnimSequence는 스켈레톤 참조 경로와 실제 애니메이션 클립 데이터를 함께 보관한다. FBX에서 추출한
 * 키프레임 샘플은 FAnimationClip으로 들어오고, 런타임에서는 AnimInstance가 이 객체를 읽어 현재 포즈를
 * 샘플링한다. Notify 목록도 같은 에셋에 포함되어 특정 재생 시간에 이벤트를 발생시키는 기준이 된다.
 */

#pragma once

#include "Asset/Animation/Core/AnimationTypes.h"
#include "Asset/Animation/Notify/AnimNotify.h"
#include "Asset/AssetFileHeader.h"
#include "Object/Object.h"

/**
 * 스켈레톤 참조와 애니메이션 클립, Notify를 함께 저장하는 애니메이션 에셋이다.
 *
 * 임포터가 만든 FAnimationClip은 이 객체를 통해 .uasset으로 저장된다. 런타임에서는 AnimInstance가
 * 시퀀스를 참조해 현재 시간의 로컬 본 포즈를 계산한다.
 */
class UAnimSequence : public UObject
{
public:
    DECLARE_CLASS(UAnimSequence, UObject)

    static constexpr uint32 AssetVersion = 1;

    /**
     * 에셋 헤더 검증과 본문 데이터 저장/로드를 함께 처리한다.
     */
    void Serialize(FArchive& Ar);

    void SetAnimationClip(FAnimationClip&& InClip) { AnimationClip = std::move(InClip); }
    const FAnimationClip& GetAnimationClip() const { return AnimationClip; }
    FAnimationClip& GetMutableAnimationClip() { return AnimationClip; }

    void SetSkeletonAssetPath(const FString& InPath) { SkeletonAssetPath = InPath; }
    const FString& GetSkeletonAssetPath() const { return SkeletonAssetPath; }

    void AddNotify(const FAnimNotifyEvent& Notify) { Notifies.push_back(Notify); }
    const TArray<FAnimNotifyEvent>& GetNotifies() const { return Notifies; }
    TArray<FAnimNotifyEvent>& GetMutableNotifies() { return Notifies; }

    float GetPlayLength() const { return AnimationClip.Duration; }
    bool IsValidSequence() const { return AnimationClip.Duration > 0.0f && !AnimationClip.Tracks.empty(); }

private:
    FString SkeletonAssetPath;
    FAnimationClip AnimationClip;
    TArray<FAnimNotifyEvent> Notifies;
};
