/**
 * 애니메이션 에셋과 스켈레톤이 공유하는 직렬화 데이터 구조를 정의한다.
 *
 * 파일과 UObject 에셋을 직접 저장하기 위한 최소 데이터만 포함한다. FBX에서 읽은 본 계층과
 * 애니메이션 샘플 데이터는 USkeleton, UAnimDataModel, UAnimSequence가 소유하고, 메시 에셋은
 * 애니메이션 클립을 직접 알지 않는다.
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Vector.h"
#include "Object/FName.h"
#include "Serialization/Archive.h"
#include "Serialization/ArchiveMath.h"

/**
 * 초당 프레임 수를 분자/분모로 표현하는 간단한 프레임레이트 타입이다.
 */
struct FFrameRate
{
    int32 Numerator = 30;
    int32 Denominator = 1;

    float AsDecimal() const
    {
        return Denominator != 0 ? static_cast<float>(Numerator) / static_cast<float>(Denominator) : 0.0f;
    }

    friend FArchive &operator<<(FArchive &Ar, FFrameRate &Rate)
    {
        Ar << Rate.Numerator;
        Ar << Rate.Denominator;
        return Ar;
    }
};

/**
 * 스켈레톤을 구성하는 단일 본의 이름, 부모 관계, 바인드 포즈 정보를 보관한다.
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
        Ar << Bone.LocalBindPose;
        Ar << Bone.InverseBindPose;
        return Ar;
    }
};

/**
 * 위치, 회전, 스케일을 분리해 보관하는 원시 애니메이션 트랙이다.
 */
struct FRawAnimSequenceTrack
{
    TArray<FVector> PosKeys;
    TArray<FQuat>   RotKeys;
    TArray<FVector> ScaleKeys;

    friend FArchive &operator<<(FArchive &Ar, FRawAnimSequenceTrack &Track)
    {
        Ar << Track.PosKeys;
        Ar << Track.RotKeys;
        Ar << Track.ScaleKeys;
        return Ar;
    }
};

/**
 * 본 이름과 원시 트랙을 연결하는 이름 기반 애니메이션 데이터 표현이다.
 */
struct FBoneAnimationTrack
{
    FName                 Name;
    FRawAnimSequenceTrack InternalTrack;

    friend FArchive &operator<<(FArchive &Ar, FBoneAnimationTrack &Track)
    {
        Ar << Track.Name;
        Ar << Track.InternalTrack;
        return Ar;
    }
};

/**
 * 애니메이션 커브 데이터 이름과 값이 들어갈 자리를 위한 구조체
 *
 * TODO: 추후 확장 구현
 */
struct FAnimationCurveData
{
    friend FArchive &operator<<(FArchive &Ar, FAnimationCurveData &CurveData)
    {
        (void)Ar;
        (void)CurveData;
        return Ar;
    }
};
