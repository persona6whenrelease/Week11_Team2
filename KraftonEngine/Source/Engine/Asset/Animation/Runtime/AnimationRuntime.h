/**
 * 애니메이션 클립을 포즈로 평가하는 정적 런타임 유틸리티를 선언한다.
 *
 * 이 타입은 UObject 상태를 갖지 않고, 입력된 클립/본/시간을 기반으로 로컬 본 행렬 배열을 계산한다.
 * AnimInstance나 프리뷰 뷰어가 같은 샘플링 규칙을 사용하도록 만드는 공통 계산 계층이다.
 */

#pragma once

#include "Asset/Animation/Core/AnimationTypes.h"

class UAnimSequence;

/**
 * 애니메이션 에셋 데이터를 현재 시간의 본 포즈로 평가하는 계산 전용 클래스이다.
 *
 * 상태를 직접 보관하지 않으므로 에디터 프리뷰와 실제 런타임 재생이 같은 샘플링 함수를 공유할 수 있다.
 */
class FAnimationRuntime
{
public:
    /**
     * 애니메이션 클립과 시간을 입력받아 본별 로컬 포즈 행렬 배열을 계산한다.
     */
    static bool SampleLocalPose(const UAnimSequence* Sequence, float TimeSeconds, const TArray<FBoneInfo>& Bones, TArray<FMatrix>& OutLocalPose);
    /**
     * 두 로컬 포즈 배열을 같은 본 인덱스 기준으로 선형 보간한다.
     */
    static bool BlendLocalPoses(const TArray<FMatrix>& PoseA, const TArray<FMatrix>& PoseB, float Alpha, TArray<FMatrix>& OutPose);
};
