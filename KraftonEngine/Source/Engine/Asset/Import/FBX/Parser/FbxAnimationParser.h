/**
 * FBX 애니메이션 스택을 엔진 애니메이션 클립으로 변환하는 파서를 선언한다.
 *
 * 스켈레톤별 본 트랙을 시간 축으로 샘플링하고, UAnimSequence가 저장할 수 있는 FAnimationClip 형태로
 * 정리한다. 메시 임포트 결과와 같은 본 인덱스 체계를 사용해야 런타임 포즈 계산이 올바르게 동작한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Animation/Core/AnimationTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"

namespace fbxsdk
{
    class FbxScene;
}

/**
 * FBX animation stack을 스켈레톤별 FAnimationClip으로 샘플링하는 파서이다.
 */
class FFbxAnimationParser final
{
public:
    explicit FFbxAnimationParser(const FFbxImportMeta& InImportMeta) : ImportMeta(InImportMeta) {}

    void ParseSkeletonAnimations(fbxsdk::FbxScene* Scene,
                                 const FFbxSkeletonMeta& SkeletonMeta,
                                 const TArray<FBoneInfo>& SkeletonBones,
                                 TArray<FAnimationClip>& OutAnimationClips,
                                 float SampleRate = 30.0f) const;

private:
    const FFbxImportMeta& ImportMeta;
};
