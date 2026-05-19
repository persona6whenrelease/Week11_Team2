/**
 * 스테이트 머신 그래프를 보유하는 UAnimInstance 파생.
 *
 * - SetStateMachineGraph로 외부가 graph를 코드로 구성해 주입한다(소유권 이전).
 * - 평가 시 base UAnimInstance::EvaluateGraph가 AnimGraphPtr->Evaluate 호출 — 별도 우회 없음.
 * - BoolVariables는 본 클래스에 보관. base GetBoolVariable 훅을 override해 노드가 조회.
 */

#pragma once

#include "Asset/Animation/Core/AnimGraph_StateMachine.h"
#include "Asset/Animation/Core/AnimInstance.h"
#include "Core/CoreTypes.h"
#include "Object/FName.h"

#include <memory>

class UAnimStateMachineInstance : public UAnimInstance
{
  public:
    DECLARE_CLASS(UAnimStateMachineInstance, UAnimInstance)

    UAnimStateMachineInstance();
    ~UAnimStateMachineInstance() override = default;

    /**
     * 외부(에디터·테스트 픽스처·Lua)가 graph를 코드로 구성해 주입한다.
     * - 각 FAnimState의 Sub가 FAnimGraphNode_SequencePlayer면 SubLengthHint 자동 도출(명시 안 된 경우만).
     * - 이후 base AnimGraphPtr의 root로 set — 소유권 이전.
     */
    void SetStateMachineGraph(std::unique_ptr<FAnimGraphNode_StateMachine> InRoot);

    void SetBoolVariable(const FName &Name, bool Value) { BoolVariables[Name] = Value; }
    bool GetBoolVariable(const FName &Name, bool Default = false) const override;
    const std::unordered_map<FName, bool, FName::Hash> &GetBoolVariables() const { return BoolVariables; }

  private:
    std::unordered_map<FName, bool, FName::Hash> BoolVariables;
};
