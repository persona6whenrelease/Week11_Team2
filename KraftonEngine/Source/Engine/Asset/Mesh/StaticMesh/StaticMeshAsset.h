/**
 * 정적 메시 에셋의 직렬화 가능한 순수 데이터 구조를 정의한다.
 *
 * 위치, 노말, 탄젠트, UV를 가진 정점 배열과 인덱스, 섹션, 머티리얼 슬롯을 저장한다. FBX/OBJ 임포터는
 * 각자의 원본 포맷을 이 구조로 변환하고, UStaticMesh는 이 데이터를 기반으로 GPU 리소스를 만든다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Vector.h"
#include "Engine/Object/Object.h"
#include "Asset/Mesh/Common/MeshCommonTypes.h"
#include "Render/Resource/Buffer.h"
#include "Serialization/Archive.h"
#include <memory>
#include <algorithm>

/**
 * 정적 메시 렌더링에 사용하는 위치, 노말, 색상, UV, 탄젠트 포함 정점 형식이다.
 */
struct FNormalVertex
{
    FVector  pos;
    FVector  normal;
    FVector4 color;
    FVector2 tex;
    FVector4 tangent;
};

/**
 * 정적 메시의 정점, 인덱스, 섹션, 머티리얼 슬롯, 바운딩 정보를 저장하는 순수 데이터이다.
 */
struct FStaticMesh
{
    FString               PathFileName;
    TArray<FNormalVertex> Vertices;
    TArray<uint32>        Indices;

    TArray<FStaticMeshSection> Sections;

    std::unique_ptr<FMeshBuffer> RenderBuffer;

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
        for (const FNormalVertex &V : Vertices)
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
    }
};
