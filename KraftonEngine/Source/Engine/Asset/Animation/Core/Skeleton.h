/**
 * 스켈레톤 에셋 객체를 선언한다.
 *
 * USkeleton은 본 이름, 부모 인덱스, 로컬 바인드 포즈, inverse bind pose를 포함한 본 배열을 소유한다.
 * 스켈레탈 메시와 애니메이션 시퀀스는 동일한 본 인덱스 체계를 기준으로 연결되므로, 이 객체는 임포트
 * 결과와 런타임 포즈 계산 사이의 기준 테이블 역할을 한다.
 */

#pragma once

#include "Asset/Animation/Core/AnimationTypes.h"
#include "Asset/AssetFileHeader.h"
#include "Object/Object.h"

/**
 * 본 배열을 에셋으로 저장하고 이름 기반 검색을 제공하는 스켈레톤 객체이다.
 *
 * 스켈레탈 메시와 애니메이션 시퀀스는 이 객체의 본 인덱스 체계를 공유한다. inverse bind pose는
 * 스키닝 행렬을 만들 때 기준 포즈를 제거하는 데 사용된다.
 */
class USkeleton : public UObject
{
public:
    DECLARE_CLASS(USkeleton, UObject)

    static constexpr uint32 AssetVersion = 1;

    /**
     * 에셋 헤더 검증과 본문 데이터 저장/로드를 함께 처리한다.
     */
    void Serialize(FArchive& Ar);

    void SetBones(TArray<FBoneInfo>&& InBones) { Bones = std::move(InBones); }
    const TArray<FBoneInfo>& GetBones() const { return Bones; }
    TArray<FBoneInfo>& GetMutableBones() { return Bones; }

    /**
     * 본 이름을 기준으로 스켈레톤 내부 인덱스를 찾는다.
     */
    int32 FindBoneIndexByName(const FString& BoneName) const;

private:
    TArray<FBoneInfo> Bones;
};
