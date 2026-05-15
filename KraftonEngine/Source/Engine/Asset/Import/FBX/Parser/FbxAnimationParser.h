/**
 * FBX 애니메이션을 엔진 데이터로 읽어오는 파서 인터페이스를 선언한다.
 *
 * 이 파서는 이미 수집된 FBX 메타 정보와 스켈레톤 정보를 기반으로 애니메이션 클립 배열을 생성한다.
 * 원본 FBX SDK 객체를 직접 노출하지 않고, 이후 단계가 사용할 수 있는 FAnimationClip 중심 데이터로
 * 변환하는 것이 목적이다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Asset/Import/FBX/Types/FBXImportMeta.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"

namespace fbxsdk
{
    class FbxScene;
}

/**
 * FBX 애니메이션 스택을 엔진 애니메이션 클립으로 변환하는 파서이다.
 *
 * 스켈레톤 메타 정보를 기준으로 노드별 transform curve를 찾아 본 트랙을 구성한다. FBX 시간 단위와
 * 엔진 시간 표현의 차이를 이 단계에서 흡수해 런타임 포즈 계산이 단순한 샘플 보간만 수행하도록
 * 만든다.
 */
class FFbxAnimationParser final
{
  public:
    explicit FFbxAnimationParser(const FFbxImportMeta &InImportMeta) : ImportMeta(InImportMeta) {}

    void ParseSkeletonAnimations(fbxsdk::FbxScene *Scene, const FFbxSkeletonMeta &SkeletonMeta,
                                 FSkeletalMesh &OutMesh, float SampleRate = 30.0f) const;

  private:
    const FFbxImportMeta &ImportMeta;
};
