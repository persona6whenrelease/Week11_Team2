#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "Serialization/Archive.h"

class UClass;

// 에디터에서 자동 위젯 매핑에 사용되는 프로퍼티 타입
enum class EPropertyType : uint8_t
{
	Bool,
	ByteBool, // uint8을 bool처럼 사용 (std::vector<bool> 회피용)
	Int,
	Float,
	Vec3,
	Vec4,
	Rotator,	// FRotator (Pitch, Yaw, Roll)
	String,
	Name,		  // FName — 문자열 풀 기반 이름 (리소스 키 등)
	SceneComponentRef, // Owner actor 내부 USceneComponent 참조
	Color4,	   // FVector4 RGBA — ImGui::ColorEdit4 위젯
	StaticMeshRef, // UStaticMesh* 에셋 레퍼런스 (드롭다운 선택)
	SkeletalMeshRef, // USkeletalMesh* 에셋 레퍼런스
	MaterialSlot,  // FMaterialSlot — 머티리얼 경로
	Enum,
	Array,
	Vec3Array,
	Struct,
	ActorRef,
};

// 머티리얼 슬롯: 경로를 하나의 단위로 관리
struct FMaterialSlot
{
	std::string Path;
};

inline FArchive& operator<<(FArchive& Ar, FMaterialSlot& Slot)
{
	Ar << Slot.Path;
	return Ar;
}

// 컴포넌트가 노출하는 편집 가능한 프로퍼티 디스크립터
struct FPropertyDescriptor
{
	using FArraySizeGetter = size_t (*)(const void*);
	using FArrayResizeFunc = void (*)(void*, size_t);
	using FArrayElementGetter = void* (*)(void*, size_t);
	using FArrayElementConstGetter = const void* (*)(const void*, size_t);

	std::string   Name;
	EPropertyType Type;
	void*         ValuePtr;

	// float 범위 힌트 (DragFloat 등에서 사용)
	float Min   = 0.0f;
	float Max   = 0.0f;
	float Speed = 0.1f;

	// Enum Metadata
	const char** EnumNames = nullptr;
	uint32		 EnumCount = 0;

	// Optional editor metadata. Existing property initializers can ignore these.
	std::string Category = "Default";
	std::string Tooltip;
	uint32 Flags = 0;

	// Reflected USTRUCT metadata for composite value properties.
	const UClass* StructType = nullptr;

	// Generic array metadata for TArray<primitive>-style reflected properties.
	EPropertyType InnerType = EPropertyType::Int;
	FArraySizeGetter ArraySizeGetter = nullptr;
	FArrayResizeFunc ArrayResizeFunc = nullptr;
	FArrayElementGetter ArrayElementGetter = nullptr;
	FArrayElementConstGetter ArrayElementConstGetter = nullptr;
};

template<typename ArrayT>
struct TArrayPropertyOps
{
	static size_t GetSize(const void* ArrayPtr)
	{
		return static_cast<const ArrayT*>(ArrayPtr)->size();
	}

	static void Resize(void* ArrayPtr, size_t NewSize)
	{
		static_cast<ArrayT*>(ArrayPtr)->resize(NewSize);
	}

	static void* GetElement(void* ArrayPtr, size_t Index)
	{
		return &(*static_cast<ArrayT*>(ArrayPtr))[Index];
	}

	static const void* GetConstElement(const void* ArrayPtr, size_t Index)
	{
		return &(*static_cast<const ArrayT*>(ArrayPtr))[Index];
	}
};
