#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "Serialization/Archive.h"

class UClass;

using FArraySizeGetter = size_t (*)(const void*);
using FArrayResizeFunc = void (*)(void*, size_t);
using FArrayElementGetter = void* (*)(void*, size_t);
using FArrayElementConstGetter = const void* (*)(const void*, size_t);

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
	Color4,	   // FVector4 RGBA — ImGui::ColorEdit4 위젯
	ObjectRef,  // UCLASS 기반 참조/에셋 경로. 세부 타입은 ObjectClass 메타로 구분
	MaterialSlot,  // FMaterialSlot — 머티리얼 경로
	Enum,
	Array,
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

struct FPropertyTypeDesc
{
	EPropertyType Kind = EPropertyType::Int;

	// Kind == Struct
	const UClass* StructType = nullptr;

	// Kind == Enum
	const char** EnumNames = nullptr;
	uint32 EnumCount = 0;

	// Kind == ObjectRef
	const UClass* ObjectClass = nullptr;

	// Kind == Array
	FArraySizeGetter ArraySizeGetter = nullptr;
	FArrayResizeFunc ArrayResizeFunc = nullptr;
	FArrayElementGetter ArrayElementGetter = nullptr;
	FArrayElementConstGetter ArrayElementConstGetter = nullptr;
	const FPropertyTypeDesc* ElementType = nullptr;

	// Reserved for future container support.
	const FPropertyTypeDesc* KeyType = nullptr;
	const FPropertyTypeDesc* ValueType = nullptr;
};

// 컴포넌트가 노출하는 편집 가능한 프로퍼티 디스크립터
struct FPropertyDescriptor
{

	std::string   Name;
	void*         ValuePtr;

	// float 범위 힌트 (DragFloat 등에서 사용)
	float Min   = 0.0f;
	float Max   = 0.0f;
	float Speed = 0.1f;

	// Optional editor metadata. Existing property initializers can ignore these.
	std::string Category = "Default";
	std::string Tooltip;
	uint32 Flags = 0;

	const FPropertyTypeDesc* TypeDesc = nullptr;

	EPropertyType GetKind() const
	{
		return TypeDesc ? TypeDesc->Kind : EPropertyType::Int;
	}

	const UClass* GetStructType() const
	{
		return TypeDesc ? TypeDesc->StructType : nullptr;
	}

	const char** GetEnumNames() const
	{
		return TypeDesc ? TypeDesc->EnumNames : nullptr;
	}

	uint32 GetEnumCount() const
	{
		return TypeDesc ? TypeDesc->EnumCount : 0;
	}

	const UClass* GetObjectClass() const
	{
		return TypeDesc ? TypeDesc->ObjectClass : nullptr;
	}

	const FPropertyTypeDesc* GetElementType() const
	{
		return TypeDesc ? TypeDesc->ElementType : nullptr;
	}

	FArraySizeGetter GetArraySizeGetter() const
	{
		return TypeDesc ? TypeDesc->ArraySizeGetter : nullptr;
	}

	FArrayResizeFunc GetArrayResizeFunc() const
	{
		return TypeDesc ? TypeDesc->ArrayResizeFunc : nullptr;
	}

	FArrayElementGetter GetArrayElementGetter() const
	{
		return TypeDesc ? TypeDesc->ArrayElementGetter : nullptr;
	}

	FArrayElementConstGetter GetArrayElementConstGetter() const
	{
		return TypeDesc ? TypeDesc->ArrayElementConstGetter : nullptr;
	}
};

inline const FPropertyTypeDesc* GetBuiltinPropertyType(EPropertyType Kind)
{
	switch (Kind)
	{
	case EPropertyType::Bool:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Bool };
		return &Type;
	}
	case EPropertyType::ByteBool:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::ByteBool };
		return &Type;
	}
	case EPropertyType::Int:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Int };
		return &Type;
	}
	case EPropertyType::Float:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Float };
		return &Type;
	}
	case EPropertyType::Vec3:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Vec3 };
		return &Type;
	}
	case EPropertyType::Vec4:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Vec4 };
		return &Type;
	}
	case EPropertyType::Rotator:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Rotator };
		return &Type;
	}
	case EPropertyType::String:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::String };
		return &Type;
	}
	case EPropertyType::Name:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Name };
		return &Type;
	}
	case EPropertyType::Color4:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Color4 };
		return &Type;
	}
	case EPropertyType::ObjectRef:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::ObjectRef };
		return &Type;
	}
	case EPropertyType::MaterialSlot:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::MaterialSlot };
		return &Type;
	}
	case EPropertyType::ActorRef:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::ActorRef };
		return &Type;
	}
	default:
	{
		static const FPropertyTypeDesc Type{ EPropertyType::Int };
		return &Type;
	}
	}
}

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
