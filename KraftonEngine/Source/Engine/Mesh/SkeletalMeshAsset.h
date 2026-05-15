/**
 * 스켈레탈 메시 에셋의 직렬화 가능한 순수 데이터 구조를 정의한다.
 *
 * 정점의 bone id/weight, 본 계층, inverse bind pose, 애니메이션 트랙과 클립 정보를 포함한다.
 * 이 파일의 타입들은 UObject 수명이나 GPU 리소스에 의존하지 않으며, FBX 임포터가 만든 결과를
 * .uasset 또는 바이너리 데이터로 저장하고 다시 로드하기 위한 에셋 포맷의 중심이 된다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"
#include "Mesh/MeshCommonTypes.h"
#include "Serialization/Archive.h"

#include <algorithm>

/**
 * 스키닝에 필요한 본 영향 정보를 포함한 정점 형식이다.
 *
 * 일반 메시 정점 속성에 최대 4개의 bone id와 weight를 추가해 CPU/GPU skinning 단계에서 본 행렬을
 * 적용할 수 있게 한다. FBX 임포터는 여러 cluster weight를 이 형식으로 정규화한다.
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
 * 스켈레톤의 단일 본 정보를 표현한다.
 *
 * 본 이름, 부모 인덱스, bind pose 기준 행렬을 담아 계층 구조와 inverse bind pose 계산의 기준이
 * 된다.
 */
struct FBoneInfo
{
    FString Name;
    int32   ParentIndex = -1;
    FMatrix LocalBindPose = FMatrix::Identity;
    FMatrix InverseBindPose = FMatrix::Identity;

    friend FArchive &operator<<(FArchive &Ar, FBoneInfo &Bone)
    {
        Ar << Bone.Name;
        Ar << Bone.ParentIndex;
        Ar.Serialize(&Bone.LocalBindPose, sizeof(FMatrix));
        Ar.Serialize(&Bone.InverseBindPose, sizeof(FMatrix));
        return Ar;
    }
};

/**
 * 특정 시간의 본 로컬 transform 샘플이다.
 *
 * 애니메이션 커브를 런타임에서 다루기 쉬운 형태로 변환한 결과이며, 트랙 보간의 입력 데이터로
 * 사용된다.
 */
struct FBoneAnimSample
{
    FMatrix LocalMatrix = FMatrix::Identity;
};

/**
 * 하나의 본에 대한 시간 순서 애니메이션 샘플 목록이다.
 *
 * AnimationClip은 본마다 이 트랙을 가지고 있으며, 런타임은 현재 시간에 해당하는 두 샘플을 찾아
 * 보간한 뒤 최종 포즈를 계산한다.
 */
struct FBoneAnimTrack
{
    int32                   BoneIndex = -1;
    TArray<FBoneAnimSample> Samples;

    friend FArchive &operator<<(FArchive &Ar, FBoneAnimTrack &Track)
    {
        Ar << Track.BoneIndex;

        uint32 SampleCount = static_cast<uint32>(Track.Samples.size());
        Ar << SampleCount;
        if (Ar.IsLoading())
            Track.Samples.resize(SampleCount);
        if (SampleCount > 0)
        {
            Ar.Serialize(Track.Samples.data(), SampleCount * sizeof(FBoneAnimSample));
        }
        return Ar;
    }
};

/**
 * 하나의 재생 가능한 애니메이션 단위이다.
 *
 * Walk, Idle 같은 클립 이름, 길이, FPS, 본별 트랙을 함께 가진다. 원본 FBX의 AnimationStack이
 * 엔진에서 사용할 수 있는 형태로 정리된 결과이다.
 */
struct FAnimationClip
{
    FString                Name;
    float                  Duration = 0.0f;
    float                  FrameRate = 30.0f;
    int32                  FrameCount = 0;
    TArray<FBoneAnimTrack> Tracks;

    friend FArchive &operator<<(FArchive &Ar, FAnimationClip &Clip)
    {
        Ar << Clip.Name;
        Ar << Clip.Duration;
        Ar << Clip.FrameRate;
        Ar << Clip.FrameCount;
        Ar << Clip.Tracks;
        return Ar;
    }
};

/**
 * 스켈레탈 메시 에셋의 저장 단위이다.
 *
 * 스키닝 정점/인덱스, 섹션, 머티리얼 슬롯, 본 계층, 애니메이션 클립을 모두 포함한다. UObject가 아닌
 * 순수 데이터이므로 직렬화와 임포트 결과 저장에 사용된다.
 */
struct FSkeletalMesh
{
    FString                 PathFileName;
    TArray<FSkeletalVertex> Vertices;
    TArray<uint32>          Indices;
    TArray<FMeshSection>    Sections;
    TArray<FBoneInfo>       Bones;
    TArray<FAnimationClip>  AnimationClips;

    FVector BoundsCenter = FVector(0, 0, 0);
    FVector BoundsExtent = FVector(0, 0, 0);
    bool    bBoundsValid = false;

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

    void Serialize(FArchive &Ar)
    {
        Ar << PathFileName;
        Ar << Vertices;
        Ar << Indices;
        Ar << Sections;
        Ar << Bones;
        Ar << AnimationClips;
    }
};
