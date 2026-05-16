/**
 * 에셋 시스템에서 구분하는 파일 타입 열거형을 정의한다.
 *
 * 임포트 결과물은 StaticMesh, SkeletalMesh, Skeleton, AnimSequence, Material, Texture처럼 서로 다른
 * 저장 포맷을 가진다. 이 열거형은 공통 헤더, 로그, 에셋 매니저 분기에서 타입을 명확하게 식별하기 위한
 * 작은 기준 테이블이다.
 */

#pragma once

#include "Core/CoreTypes.h"

/**
 * 저장된 에셋 파일이 어떤 본문 포맷을 가지는지 구분하는 열거형이다.
 */
enum class EAssetType : uint32
{
    Unknown      = 0,
    StaticMesh   = 1,
    SkeletalMesh = 2,
    Skeleton     = 3,
    AnimSequence = 4,
    Material     = 5,
    Texture2D    = 6,
    FbxScene     = 7,
};

inline const char* LexToString(EAssetType Type)
{
    switch (Type)
    {
    case EAssetType::StaticMesh:   return "StaticMesh";
    case EAssetType::SkeletalMesh: return "SkeletalMesh";
    case EAssetType::Skeleton:     return "Skeleton";
    case EAssetType::AnimSequence: return "AnimSequence";
    case EAssetType::Material:     return "Material";
    case EAssetType::Texture2D:    return "Texture2D";
    case EAssetType::FbxScene:     return "FbxScene";
    default:                       return "Unknown";
    }
}
