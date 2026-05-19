/**
 * 임의 root 노드를 보유하는 UAnimInstance 파생.
 *
 * - SetRootGraph로 외부가 FAnimGraphNode_Base 베이스 타입의 root를 주입(소유권 이전).
 * - 평가 시 base UAnimInstance::EvaluateGraph가 AnimGraphPtr->Evaluate 호출 — 별도 우회 없음.
 * - StateMachine 한정 시그니처/SubLengthHint 도출 부수효과는 두지 않는다.
 */

#pragma once

#include "Asset/Animation/Core/AnimGraph.h"
#include "Asset/Animation/Core/AnimInstance.h"

#include <memory>

class UAnimGraphInstance : public UAnimInstance
{
  public:
    DECLARE_CLASS(UAnimGraphInstance, UAnimInstance)

    UAnimGraphInstance();
    ~UAnimGraphInstance() override = default;

    /**
     * 외부(에디터·테스트·콘솔 커맨드)가 graph를 코드로 구성해 주입한다.
     * - AnimGraphPtr가 없으면 lazy 생성 후 root set — 소유권 이전.
     * - root 노드 타입은 가리지 않는다(SequencePlayer/Blend2/BlendN/StateMachine 등 모두 허용).
     */
    void SetRootGraph(std::unique_ptr<FAnimGraphNode_Base> InRoot);
};
