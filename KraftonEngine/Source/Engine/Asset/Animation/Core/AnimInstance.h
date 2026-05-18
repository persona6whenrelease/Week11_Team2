/**
 * 애니메이션 인스턴스 기반 클래스. 한 프레임 평가의 진입점과 시간 상태를 보유한다.
 *
 * 시퀀스 선택/조합 같은 구체 정책은 파생(UAnimSingleNodeInstance 등)이 정한다. FK와 스키닝은
 * USkinnedMeshComponent가 담당하며, 본 클래스는 OutputLocalPose만 산출해 컴포넌트로 노출한다.
 */

#pragma once

#include "Asset/Animation/Core/AnimGraph.h"
#include "Asset/Animation/Notify/AnimNotify.h"
#include "Math/Matrix.h"
#include "Object/FName.h"
#include "Object/Object.h"

#include <memory>

class USkeleton;

class UAnimInstance : public UObject
{
  public:
    DECLARE_CLASS(UAnimInstance, UObject)

    UAnimInstance();
    ~UAnimInstance() override;

    /**
     * 스켈레톤을 결합하고 OutputLocalPose를 초기화한다.
     */
    virtual void InitializeAnimation(USkeleton *InSkeleton);

    /**
     * 시간 누적 + Notify 트리거 판정. 평가는 EvaluateGraph()에서 별도로 수행한다.
     */
    virtual void Update(float DeltaTime);

    /**
     * AnimGraph를 호출해 OutputLocalPose를 갱신한다.
     */
    virtual void EvaluateGraph();

    const TArray<FMatrix> &GetOutputLocalPose() const { return OutputLocalPose; }
    const TArray<FName>   &GetTriggeredNotifiesThisFrame() const { return TriggeredNotifiesThisFrame; }

    void  SetLooping(bool b)        { bLooping = b; }
    bool  IsLooping() const         { return bLooping; }
    void  SetPlaybackSpeed(float s) { PlaybackSpeed = s; }
    float GetPlaybackSpeed() const  { return PlaybackSpeed; }
    void  SetPaused(bool b)         { bPaused = b; }
    bool  IsPaused() const          { return bPaused; }
    void  ResetTime()               { PreviousTime = CurrentTime = 0.0f; }
    float GetCurrentTime() const    { return CurrentTime; }
    float GetPreviousTime() const   { return PreviousTime; }

    /**
     * 외부에서 임의 시점 평가를 요청할 때 시간만 강제로 세팅하기 위한 helper.
     * Update를 거치지 않으므로 loop wrap 처리는 호출자 책임이다.
     */
    void SetEvaluationTime(float InTime) { PreviousTime = CurrentTime = InTime; }

    USkeleton *GetSkeleton() const  { return Skeleton; }
    AnimGraph *GetAnimGraph() const { return AnimGraphPtr.get(); }

  protected:
    /**
     * 파생이 제공하는 현재 시퀀스 길이. 단일 시퀀스 / 스테이트머신마다 다르게 계산된다.
     * 베이스의 기본값은 0(재생할 시퀀스 없음). 엔진 RTTI/팩토리 등록과 호환되도록 abstract로 두지 않는다.
     */
    virtual float GetEffectivePlayLength() const { return 0.0f; }

    /**
     * 파생이 현재 활성 Notify 배열 포인터를 노출한다. 없으면 nullptr.
     */
    virtual const TArray<FAnimNotifyEvent> *GetActiveNotifies() const { return nullptr; }

    /**
     * Skeleton 본 수에 맞춰 OutputLocalPose를 bind pose로 초기화한다.
     */
    void FillBindPose();

    USkeleton                  *Skeleton = nullptr;     // ref, not owned
    std::unique_ptr<AnimGraph>  AnimGraphPtr;           // owned
    TArray<FMatrix>             OutputLocalPose;        // size = BoneCount
    TArray<FName>               TriggeredNotifiesThisFrame;

    float CurrentTime   = 0.0f;
    float PreviousTime  = 0.0f; // 구간 사이 event 처리할 때 필요.
    float PlaybackSpeed = 1.0f;
    bool  bLooping      = true;
    bool  bPaused       = true;
};
