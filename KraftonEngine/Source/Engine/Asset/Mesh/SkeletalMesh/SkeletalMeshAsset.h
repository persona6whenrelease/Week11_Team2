/**
 * 스켈레탈 메시 에셋의 직렬화 가능한 순수 데이터 구조를 정의한다.
 *
 * 정점은 위치/노말/UV/탄젠트뿐 아니라 최대 4개의 본 인덱스와 가중치를 포함한다. 이 데이터는 CPU skinning
 * 또는 향후 GPU skinning이 참조하는 기본 입력이며, 섹션과 머티리얼 슬롯을 함께 저장해 렌더링 단위도
 * 보존한다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Serialization/Archive.h"
#include "Asset/Animation/Core/AnimationTypes.h"

#include <algorithm>

/**
 * 스키닝에 필요한 위치, 노말, UV, 탄젠트, 본 인덱스, 가중치를 가진 정점 형식이다.
 */
struct FSkeletalVertex
{
    FVector  pos;
    FVector  normal;
    FVector2 tex;
    FVector4 tangent;
    uint32   BoneIDs[4] = {0, 0, 0, 0};
    float    BoneWeights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/**
 * 스켈레탈 메시의 정점, 인덱스, 섹션, 머티리얼 슬롯, 바운딩 정보를 저장하는 순수 데이터이다.
 */
struct FSkeletalMesh
{
    FString                 PathFileName;
    TArray<FSkeletalVertex> Vertices;
    TArray<uint32>          Indices;
    TArray<FMeshSection>    Sections;
    
    FString                 SkeletonAssetPath;
    // FBX 조립 과정에서만 사용하는 임시 본 배열이다. 저장 시에는 USkeleton으로 분리된다.
    TArray<FBoneInfo>       Bones;

    FVector BoundsCenter = FVector(0, 0, 0);
    FVector BoundsExtent = FVector(0, 0, 0);
    bool    bBoundsValid = false;

    /**
     * 정점 배열을 순회해 메시의 로컬 바운딩 박스 중심과 반경을 계산한다.
     */
    void CacheBounds()
    {
        bBoundsValid = false;
        if (Vertices.empty())
            return;

        FVector LocalMin = Vertices[0].pos;
        FVector LocalMax = Vertices[0].pos;
        for (const FSkeletalVertex &V : Vertices)
        {
            LocalMin.X = (std::min)(LocalMin.X, V.pos.X);
            LocalMin.Y = (std::min)(LocalMin.Y, V.pos.Y);
            LocalMin.Z = (std::min)(LocalMin.Z, V.pos.Z);
            LocalMax.X = (std::max)(LocalMax.X, V.pos.X);
            LocalMax.Y = (std::max)(LocalMax.Y, V.pos.Y);
            LocalMax.Z = (std::max)(LocalMax.Z, V.pos.Z);
        }

        BoundsCenter = (LocalMin + LocalMax) * 0.5f;
        BoundsExtent = (LocalMax - LocalMin) * 0.5f;
        bBoundsValid = true;
    }

    /**
     * 에셋 헤더 검증과 본문 데이터 저장/로드를 함께 처리한다.
     */
    void Serialize(FArchive &Ar)
    {
        Ar << PathFileName;
        Ar << Vertices;
        Ar << Indices;
        Ar << Sections;
        Ar << SkeletonAssetPath;
    }
};
