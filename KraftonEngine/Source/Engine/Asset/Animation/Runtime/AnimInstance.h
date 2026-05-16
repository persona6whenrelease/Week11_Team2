/**
 * 스켈레탈 메시 컴포넌트에 애니메이션 재생 상태를 제공하는 인스턴스를 선언한다.
 *
 * AnimInstance는 에셋 데이터 그 자체가 아니라 재생 시간, 속도, 루프 여부 같은 런타임 상태를 가진다.
 * UAnimSingleNodeInstance는 단일 UAnimSequence를 직접 재생하는 가장 단순한 구현으로, 복잡한 상태 머신
 * 없이 에셋 뷰어와 기본 런타임 재생을 처리하는 데 사용된다.
 */

#pragma once

#include "Asset/Animation/Core/AnimSequence.h"

class USkeletalMeshComponent;

/**
 * 스켈레탈 메시 컴포넌트에 붙어 애니메이션 재생 상태를 관리하는 런타임 객체이다.
 */
class UAnimInstance : public UObject
{
public:
    DECLARE_CLASS(UAnimInstance, UObject)

    void InitializeAnimation(USkeletalMeshComponent* InOwnerComponent);
    /**
     * 프레임 시간만큼 재생 상태를 갱신하고 필요한 경우 컴포넌트 포즈를 새로 적용한다.
     */
    virtual void NativeUpdateAnimation(float DeltaSeconds);

protected:
    USkeletalMeshComponent* OwnerComponent = nullptr;
};

/**
 * 단일 애니메이션 시퀀스를 직접 재생하는 간단한 AnimInstance 구현이다.
 *
 * 상태 머신이나 블렌드 그래프 없이 하나의 클립만 시간에 따라 샘플링하므로 에셋 뷰어와 기본 재생 테스트에
 * 적합하다.
 */
class UAnimSingleNodeInstance : public UAnimInstance
{
public:
    DECLARE_CLASS(UAnimSingleNodeInstance, UAnimInstance)

    void SetAnimation(UAnimSequence* InSequence, bool bInLooping = true);
    void SetPlaying(bool bInPlaying) { bPlaying = bInPlaying; }
    void SetPlayRate(float InPlayRate) { PlayRate = InPlayRate; }
    float GetCurrentTime() const { return CurrentTime; }

    /**
     * 프레임 시간만큼 재생 상태를 갱신하고 필요한 경우 컴포넌트 포즈를 새로 적용한다.
     */
    void NativeUpdateAnimation(float DeltaSeconds) override;

private:
    UAnimSequence* Sequence = nullptr;
    bool bLooping = true;
    bool bPlaying = true;
    float PlayRate = 1.0f;
    float CurrentTime = 0.0f;
    float PreviousTime = 0.0f;
};
