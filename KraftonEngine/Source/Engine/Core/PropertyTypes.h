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

// TSet 연산 함수 포인터
using FSetSizeGetter        = size_t (*)(const void*);
using FSetInsertFunc        = void   (*)(void*, const void*);
using FSetRemoveFunc        = void   (*)(void*, const void*);
using FSetClearFunc         = void   (*)(void*);
using FSetSnapshotFunc      = void   (*)(void*, TArray<void*>&);
using FSetConstSnapshotFunc = void   (*)(const void*, TArray<const void*>&);
using FSetElementSizeFunc   = size_t (*)();

// TMap 연산 함수 포인터
using FMapSizeGetter        = size_t (*)(const void*);
using FMapClearFunc         = void   (*)(void*);
using FMapSnapshotFunc      = void   (*)(void*, TArray<void*>&, TArray<void*>&);
using FMapConstSnapshotFunc = void   (*)(const void*, TArray<const void*>&, TArray<const void*>&);
using FMapInsertFunc        = void   (*)(void*, const void*, const void*);
using FMapRemoveFunc        = void   (*)(void*, const void*);
using FMapKeySizeFunc       = size_t (*)();
using FMapValueSizeFunc     = size_t (*)();

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
	Set,   // TSet<T> — 순서 없는 고유 원소 집합
	Map,   // TMap<K,V> — 키-값 쌍
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
	const FPropertyTypeDesc* ElementType = nullptr;  // Array/Set 원소 타입

	// Kind == Set / Map — 키·값 타입
	const FPropertyTypeDesc* KeyType   = nullptr;
	const FPropertyTypeDesc* ValueType = nullptr;

	// Kind == Set
	FSetSizeGetter        SetSizeGetter        = nullptr;
	FSetInsertFunc        SetInsertFunc        = nullptr;
	FSetRemoveFunc        SetRemoveFunc        = nullptr;
	FSetClearFunc         SetClearFunc         = nullptr;
	FSetSnapshotFunc      SetSnapshotFunc      = nullptr;
	FSetConstSnapshotFunc SetConstSnapshotFunc = nullptr;
	FSetElementSizeFunc   SetElementSizeFunc   = nullptr;

	// Kind == Map
	FMapSizeGetter        MapSizeGetter        = nullptr;
	FMapClearFunc         MapClearFunc         = nullptr;
	FMapSnapshotFunc      MapSnapshotFunc      = nullptr;
	FMapConstSnapshotFunc MapConstSnapshotFunc = nullptr;
	FMapInsertFunc        MapInsertFunc        = nullptr;
	FMapRemoveFunc        MapRemoveFunc        = nullptr;
	FMapKeySizeFunc       MapKeySizeFunc       = nullptr;
	FMapValueSizeFunc     MapValueSizeFunc     = nullptr;
};

// 프로퍼티 비트 플래그 (FPROPERTY 스펙시파이어로 제어)
enum EPropertyFlags : uint32
{
	EPF_None      = 0,
	EPF_ReadOnly  = 1 << 0,  // 에디터에 표시하되 편집 불가
	EPF_Hidden    = 1 << 1,  // 에디터에 숨김 (직렬화는 유지)
	EPF_Transient = 1 << 2,  // 직렬화 제외 (에디터 표시는 유지)
};

// 에디터에서만 필요한 정적 메타데이터.
struct FEditorPropertyMetadata
{
	const char* Category = "Default";
	const char* Tooltip = "";
};

// 클래스당 한 번 존재하는 정적 메타데이터 (string literal 기반, 복사 비용 없음)
struct FPropertyMetadata
{
	const char*              Name        = "";
	size_t                   Offset      = 0;
	const FPropertyTypeDesc* TypeDesc    = nullptr;
	uint32                   Flags       = EPF_None;
	float                    Min         = 0.0f;
	float                    Max         = 0.0f;
	float                    Speed       = 0.1f;
#if WITH_EDITOR
	const FEditorPropertyMetadata* Editor = nullptr;
#endif
};

// 인스턴스 접근용 경량 핸들 — ValuePtr 하나와 정적 메타데이터 포인터만 보유
struct FPropertyDescriptor
{
	void*                    ValuePtr          = nullptr;
	const FPropertyMetadata* Meta              = nullptr;
	// Meta가 없는 합성 디스크립터(배열 원소, Map 키·값 등)를 위한 fallback
	const FPropertyTypeDesc* SyntheticTypeDesc = nullptr;
	std::string              DynamicName;       // 배열 원소 "[N]" 등 동적 이름

	// ---- 메타데이터 접근자 ----
	const char* GetName()     const { return DynamicName.empty() ? (Meta ? Meta->Name : "") : DynamicName.c_str(); }
	const char* GetCategory() const
	{
#if WITH_EDITOR
		return (Meta && Meta->Editor) ? Meta->Editor->Category : "Default";
#else
		return "Default";
#endif
	}

	const char* GetTooltip() const
	{
#if WITH_EDITOR
		return (Meta && Meta->Editor) ? Meta->Editor->Tooltip : "";
#else
		return "";
#endif
	}
	float       GetMin()      const { return Meta ? Meta->Min      : 0.0f; }
	float       GetMax()      const { return Meta ? Meta->Max      : 0.0f; }
	float       GetSpeed()    const { return Meta ? Meta->Speed    : 0.1f; }
	uint32      GetFlags()    const { return Meta ? Meta->Flags    : EPF_None; }

	const FPropertyTypeDesc* GetTypeDesc() const
	{
		return SyntheticTypeDesc ? SyntheticTypeDesc : (Meta ? Meta->TypeDesc : nullptr);
	}

	EPropertyType GetKind() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->Kind : EPropertyType::Int;
	}

	const UClass* GetStructType() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->StructType : nullptr;
	}

	const char** GetEnumNames() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->EnumNames : nullptr;
	}

	uint32 GetEnumCount() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->EnumCount : 0;
	}

	const UClass* GetObjectClass() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->ObjectClass : nullptr;
	}

	const FPropertyTypeDesc* GetElementType() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->ElementType : nullptr;
	}

	FArraySizeGetter GetArraySizeGetter() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->ArraySizeGetter : nullptr;
	}

	FArrayResizeFunc GetArrayResizeFunc() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->ArrayResizeFunc : nullptr;
	}

	FArrayElementGetter GetArrayElementGetter() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->ArrayElementGetter : nullptr;
	}

	FArrayElementConstGetter GetArrayElementConstGetter() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->ArrayElementConstGetter : nullptr;
	}

	const FPropertyTypeDesc* GetKeyType() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->KeyType : nullptr;
	}

	const FPropertyTypeDesc* GetValueType() const
	{
		const FPropertyTypeDesc* TD = GetTypeDesc();
		return TD ? TD->ValueType : nullptr;
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

template<typename SetT>
struct TSetPropertyOps
{
	using ElemT = typename SetT::value_type;

	static size_t GetSize(const void* P)
	{
		return static_cast<const SetT*>(P)->size();
	}

	static void Insert(void* P, const void* E)
	{
		static_cast<SetT*>(P)->insert(*static_cast<const ElemT*>(E));
	}

	static void Remove(void* P, const void* E)
	{
		static_cast<SetT*>(P)->erase(*static_cast<const ElemT*>(E));
	}

	static void Clear(void* P)
	{
		static_cast<SetT*>(P)->clear();
	}

	static void Snapshot(void* P, TArray<void*>& Out)
	{
		for (auto& e : *static_cast<SetT*>(P))
			Out.push_back(const_cast<void*>(static_cast<const void*>(&e)));
	}

	static void ConstSnapshot(const void* P, TArray<const void*>& Out)
	{
		for (const auto& e : *static_cast<const SetT*>(P))
			Out.push_back(&e);
	}

	static size_t ElementSize() { return sizeof(ElemT); }
};

template<typename MapT>
struct TMapPropertyOps
{
	using KeyT = typename MapT::key_type;
	using ValT = typename MapT::mapped_type;

	static size_t GetSize(const void* P)
	{
		return static_cast<const MapT*>(P)->size();
	}

	static void Clear(void* P)
	{
		static_cast<MapT*>(P)->clear();
	}

	static void Snapshot(void* P, TArray<void*>& Keys, TArray<void*>& Vals)
	{
		for (auto& [k, v] : *static_cast<MapT*>(P))
		{
			Keys.push_back(const_cast<void*>(static_cast<const void*>(&k)));
			Vals.push_back(&v);
		}
	}

	static void ConstSnapshot(const void* P, TArray<const void*>& Keys, TArray<const void*>& Vals)
	{
		for (const auto& [k, v] : *static_cast<const MapT*>(P))
		{
			Keys.push_back(&k);
			Vals.push_back(&v);
		}
	}

	static void Insert(void* P, const void* K, const void* V)
	{
		(*static_cast<MapT*>(P))[*static_cast<const KeyT*>(K)] = *static_cast<const ValT*>(V);
	}

	static void Remove(void* P, const void* K)
	{
		static_cast<MapT*>(P)->erase(*static_cast<const KeyT*>(K));
	}

	static size_t KeySize()   { return sizeof(KeyT); }
	static size_t ValueSize() { return sizeof(ValT); }
};
