/**
 * 애니메이션 에셋과 스켈레톤이 공유하는 직렬화 데이터 구조를 정의한다.
 *
 * 이 파일의 타입들은 런타임 UObject보다 낮은 레벨의 순수 데이터이다. FBX 임포터가 읽어 온 본 계층,
 * 바인드 포즈, 애니메이션 샘플, 시퀀스 트랙을 엔진 내부 포맷으로 옮길 때 사용한다. 각 구조는
 * FArchive 직렬화를 직접 제공하므로, .uasset 저장 형식과 런타임 로드 형식이 이 정의에 맞춰진다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Serialization/Archive.h"

/**
 * 스켈레톤을 구성하는 단일 본의 이름, 부모 관계, 바인드 포즈 정보를 저장한다.
 */
struct FBoneInfo
{
    FString Name;
    int32   ParentIndex = -1;
    FMatrix LocalBindPose = FMatrix::Identity;
    FMatrix InverseBindPose = FMatrix::Identity;

    friend FArchive& operator<<(FArchive& Ar, FBoneInfo& Bone)
    {
        Ar << Bone.Name;
        Ar << Bone.ParentIndex;
        Ar.Serialize(&Bone.LocalBindPose, sizeof(FMatrix));
        Ar.Serialize(&Bone.InverseBindPose, sizeof(FMatrix));
        return Ar;
    }
};

/**
 * 특정 프레임에서 한 본이 가져야 할 로컬 변환 행렬을 저장한다.
 */
struct FBoneAnimSample
{
    FMatrix LocalMatrix = FMatrix::Identity;
};

/**
 * 하나의 본에 대한 프레임별 애니메이션 샘플 배열을 저장한다.
 */
struct FBoneAnimTrack
{
    int32 BoneIndex = -1;
    TArray<FBoneAnimSample> Samples;

    friend FArchive& operator<<(FArchive& Ar, FBoneAnimTrack& Track)
    {
        Ar << Track.BoneIndex;
        uint32 SampleCount = static_cast<uint32>(Track.Samples.size());
        Ar << SampleCount;
        if (Ar.IsLoading())
        {
            Track.Samples.resize(SampleCount);
        }
        if (SampleCount > 0)
        {
            Ar.Serialize(Track.Samples.data(), SampleCount * sizeof(FBoneAnimSample));
        }
        return Ar;
    }
};

/**
 * 위치, 회전, 스케일 키를 분리해 보관하는 원시 애니메이션 트랙이다.
 */
struct FRawAnimSequenceTrack
{
    TArray<FVector> PosKeys;
    TArray<FQuat>   RotKeys;
    TArray<FVector> ScaleKeys;

    friend FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrack& Track)
    {
        Ar << Track.PosKeys;
        Ar << Track.RotKeys;
        Ar << Track.ScaleKeys;
        return Ar;
    }
};

/**
 * 본 이름과 원시 트랙을 연결해 이름 기반 애니메이션 데이터를 표현한다.
 */
struct FBoneAnimationTrack
{
    FName Name;
    FRawAnimSequenceTrack InternalTrack;

    friend FArchive& operator<<(FArchive& Ar, FBoneAnimationTrack& Track)
    {
        Ar << Track.Name;
        Ar << Track.InternalTrack;
        return Ar;
    }
};

/**
 * 하나의 애니메이션 클립 전체 길이, 프레임 수, 본별 트랙을 묶어 저장한다.
 */
struct FAnimationClip
{
    FString Name;
    float Duration = 0.0f;
    float FrameRate = 30.0f;
    int32 FrameCount = 0;
    TArray<FBoneAnimTrack> Tracks;

    friend FArchive& operator<<(FArchive& Ar, FAnimationClip& Clip)
    {
        Ar << Clip.Name;
        Ar << Clip.Duration;
        Ar << Clip.FrameRate;
        Ar << Clip.FrameCount;
        Ar << Clip.Tracks;
        return Ar;
    }
};
