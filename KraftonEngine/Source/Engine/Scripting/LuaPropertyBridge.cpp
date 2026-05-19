#include "LuaPropertyBridge.h"

#include "Core/Log.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Object/FName.h"

#include <algorithm>
#include <cctype>

namespace
{
	FString NormalizeLuaPropertyToken(FString Name)
	{
		Name.erase(
			std::remove_if(
				Name.begin(),
				Name.end(),
				[](unsigned char C)
				{
					return std::isspace(C) || C == '_' || C == '-';
				}
			),
			Name.end()
		);

		std::transform(
			Name.begin(),
			Name.end(),
			Name.begin(),
			[](unsigned char C)
			{
				return static_cast<char>(std::tolower(C));
			}
		);

		return Name;
	}

	bool IsBlockedLuaPropertyName(const FString& PropertyName)
	{
		const FString NormalizedName = NormalizeLuaPropertyToken(PropertyName);

		static const char* BlockedNames[] =
		{
			"owner",
			"outer",
			"class",
			"rootcomponent",
			"parent",
			"attachparent",
			"world",
			"uuid"
		};

		for (const char* BlockedName : BlockedNames)
		{
			if (NormalizedName == BlockedName)
			{
				return true;
			}
		}

		return false;
	}

	bool IsLuaPathProperty(EPropertyType Type)
	{
		return Type == EPropertyType::ObjectRef ||
			Type == EPropertyType::MaterialSlot;
	}

	bool IsSafeLuaAssetPath(const FString& Path)
	{
		if (Path.empty() || Path == "None")
		{
			return true;
		}

		if (Path[0] == '/' || Path[0] == '\\')
		{
			return false;
		}

		if (Path.find(':') != FString::npos)
		{
			return false;
		}

		if (Path.find("..") != FString::npos)
		{
			return false;
		}

		return true;
	}

	bool HasDescriptorRange(const FPropertyDescriptor& Desc)
	{
		return Desc.Min < Desc.Max;
	}

	float ClampFloatForDescriptor(const FPropertyDescriptor& Desc, float Value)
	{
		if (!HasDescriptorRange(Desc))
		{
			return Value;
		}

		return std::clamp(Value, Desc.Min, Desc.Max);
	}

	int32 ClampIntForDescriptor(const FPropertyDescriptor& Desc, int32 Value)
	{
		if (!HasDescriptorRange(Desc))
		{
			return Value;
		}

		const float ClampedValue = std::clamp(static_cast<float>(Value), Desc.Min, Desc.Max);
		return static_cast<int32>(ClampedValue);
	}

	bool BuildArrayElementDescriptor(const FPropertyDescriptor& ArrayProp, size_t ElementIndex, FPropertyDescriptor& OutElementProp)
	{
		if (!ArrayProp.GetArrayElementGetter() || !ArrayProp.ValuePtr)
		{
			return false;
		}

		OutElementProp = ArrayProp;
		OutElementProp.TypeDesc = ArrayProp.GetElementType();
		OutElementProp.Name = "[" + std::to_string(ElementIndex) + "]";
		OutElementProp.ValuePtr = ArrayProp.GetArrayElementGetter()(ArrayProp.ValuePtr, ElementIndex);
		return OutElementProp.ValuePtr != nullptr;
	}

	void CollectReflectedStructProperties(const UClass* StructType, void* StructValue, TArray<FPropertyDescriptor>& OutProps)
	{
		if (!StructType || !StructValue)
		{
			return;
		}

		TArray<const UClass*> Chain;
		for (const UClass* C = StructType; C; C = C->GetSuperClass())
		{
			Chain.push_back(C);
		}

		for (int32 Index = static_cast<int32>(Chain.size()) - 1; Index >= 0; --Index)
		{
			for (const FPropertyDescriptor& Desc : Chain[Index]->GetOwnProperties())
			{
				FPropertyDescriptor Inst = Desc;
				Inst.ValuePtr = reinterpret_cast<char*>(StructValue) + reinterpret_cast<size_t>(Desc.ValuePtr);
				OutProps.push_back(Inst);
			}
		}
	}

	sol::object MakeLuaObjectForProperty(sol::state_view Lua, const FPropertyDescriptor& Desc)
	{
		switch (Desc.GetKind())
		{
		case EPropertyType::Bool:
			return sol::make_object(Lua, *static_cast<bool*>(Desc.ValuePtr));

		case EPropertyType::ByteBool:
			return sol::make_object(Lua, *static_cast<uint8*>(Desc.ValuePtr) != 0);

		case EPropertyType::Int:
			return sol::make_object(Lua, *static_cast<int32*>(Desc.ValuePtr));

		case EPropertyType::Float:
			return sol::make_object(Lua, *static_cast<float*>(Desc.ValuePtr));

		case EPropertyType::Vec3:
			return sol::make_object(Lua, *static_cast<FVector*>(Desc.ValuePtr));

		case EPropertyType::Vec4:
		case EPropertyType::Color4:
			return sol::make_object(Lua, *static_cast<FVector4*>(Desc.ValuePtr));

		case EPropertyType::Rotator:
			return sol::make_object(Lua, *static_cast<FRotator*>(Desc.ValuePtr));

		case EPropertyType::String:
		case EPropertyType::ObjectRef:
			return sol::make_object(Lua, *static_cast<FString*>(Desc.ValuePtr));

		case EPropertyType::Name:
			return sol::make_object(Lua, static_cast<FName*>(Desc.ValuePtr)->ToString());

		case EPropertyType::MaterialSlot:
			return sol::make_object(Lua, static_cast<FMaterialSlot*>(Desc.ValuePtr)->Path);

		case EPropertyType::Enum:
			return sol::make_object(Lua, *reinterpret_cast<int32*>(Desc.ValuePtr));

		case EPropertyType::Array:
		{
			if (!Desc.GetArraySizeGetter())
			{
				return sol::nil;
			}

			sol::table Table = Lua.create_table();
			const size_t Count = Desc.GetArraySizeGetter()(Desc.ValuePtr);
			for (size_t Index = 0; Index < Count; ++Index)
			{
				FPropertyDescriptor ElementDesc;
				if (!BuildArrayElementDescriptor(Desc, Index, ElementDesc))
				{
					continue;
				}
				Table[static_cast<int>(Index + 1)] = MakeLuaObjectForProperty(Lua, ElementDesc);
			}
			return sol::make_object(Lua, Table);
		}

		case EPropertyType::Struct:
		{
			sol::table Table = Lua.create_table();
			TArray<FPropertyDescriptor> ChildProps;
			CollectReflectedStructProperties(Desc.GetStructType(), Desc.ValuePtr, ChildProps);
			for (const FPropertyDescriptor& ChildProp : ChildProps)
			{
				Table[ChildProp.Name] = MakeLuaObjectForProperty(Lua, ChildProp);
			}
			return sol::make_object(Lua, Table);
		}

		default:
			return sol::nil;
		}
	}

	bool AssignPropertyFromLuaObject(const FPropertyDescriptor& Desc, const sol::object& Value)
	{
		switch (Desc.GetKind())
		{
		case EPropertyType::Bool:
			*static_cast<bool*>(Desc.ValuePtr) = Value.as<bool>();
			return true;

		case EPropertyType::ByteBool:
			*static_cast<uint8*>(Desc.ValuePtr) = Value.as<bool>() ? 1 : 0;
			return true;

		case EPropertyType::Int:
			*static_cast<int32*>(Desc.ValuePtr) = ClampIntForDescriptor(Desc, Value.as<int32>());
			return true;

		case EPropertyType::Float:
			*static_cast<float*>(Desc.ValuePtr) = ClampFloatForDescriptor(Desc, Value.as<float>());
			return true;

		case EPropertyType::Vec3:
			*static_cast<FVector*>(Desc.ValuePtr) = Value.as<FVector>();
			return true;

		case EPropertyType::Vec4:
		case EPropertyType::Color4:
			*static_cast<FVector4*>(Desc.ValuePtr) = Value.as<FVector4>();
			return true;

		case EPropertyType::Rotator:
			*static_cast<FRotator*>(Desc.ValuePtr) = Value.as<FRotator>();
			return true;

		case EPropertyType::String:
			*static_cast<FString*>(Desc.ValuePtr) = Value.as<FString>();
			return true;

		case EPropertyType::ObjectRef:
		{
			const FString NewValue = Value.as<FString>();
			if (IsLuaPathProperty(Desc.GetKind()) && !IsSafeLuaAssetPath(NewValue))
			{
				UE_LOG("[LuaSecurity] SetProperty blocked: unsafe asset/reference path. property = %s, value = %s", Desc.Name.c_str(), NewValue.c_str());
				return false;
			}

			*static_cast<FString*>(Desc.ValuePtr) = NewValue;
			return true;
		}

		case EPropertyType::Name:
			*static_cast<FName*>(Desc.ValuePtr) = FName(Value.as<FString>());
			return true;

		case EPropertyType::MaterialSlot:
		{
			const FString NewPath = Value.as<FString>();
			if (!IsSafeLuaAssetPath(NewPath))
			{
				UE_LOG("[LuaSecurity] SetProperty blocked: unsafe material path. property = %s, value = %s", Desc.Name.c_str(), NewPath.c_str());
				return false;
			}

			static_cast<FMaterialSlot*>(Desc.ValuePtr)->Path = NewPath;
			return true;
		}

		case EPropertyType::Enum:
		{
			const int32 NewValue = Value.as<int32>();
			if (Desc.GetEnumCount() > 0 && (NewValue < 0 || NewValue >= static_cast<int32>(Desc.GetEnumCount())))
			{
				UE_LOG("[LuaSecurity] SetProperty blocked: enum value out of range. property = %s, value = %d", Desc.Name.c_str(), NewValue);
				return false;
			}

			*reinterpret_cast<int32*>(Desc.ValuePtr) = NewValue;
			return true;
		}

		case EPropertyType::Array:
		{
			const std::size_t MaxLuaArraySize = 1024;
			if (!Desc.GetArrayResizeFunc())
			{
				return false;
			}

			sol::table Table = Value.as<sol::table>();
			const std::size_t Count = Table.size();
			if (Count > MaxLuaArraySize)
			{
				UE_LOG("[LuaSecurity] SetProperty blocked: array too large. property = %s, count = %zu", Desc.Name.c_str(), Count);
				return false;
			}

			Desc.GetArrayResizeFunc()(Desc.ValuePtr, Count);
			for (std::size_t Index = 1; Index <= Count; ++Index)
			{
				sol::object Item = Table[static_cast<int>(Index)];
				if (!Item.valid() || Item.get_type() == sol::type::nil)
				{
					UE_LOG("[Lua] SetProperty failed: array contains nil. property = %s, index = %zu", Desc.Name.c_str(), Index);
					return false;
				}

				FPropertyDescriptor ElementDesc;
				if (!BuildArrayElementDescriptor(Desc, Index - 1, ElementDesc)
					|| !AssignPropertyFromLuaObject(ElementDesc, Item))
				{
					return false;
				}
			}
			return true;
		}

		case EPropertyType::Struct:
		{
			sol::table Table = Value.as<sol::table>();
			TArray<FPropertyDescriptor> ChildProps;
			CollectReflectedStructProperties(Desc.GetStructType(), Desc.ValuePtr, ChildProps);
			for (FPropertyDescriptor& ChildProp : ChildProps)
			{
				sol::optional<sol::object> MaybeItem = Table[ChildProp.Name];
				if (!MaybeItem.has_value())
				{
					continue;
				}

				sol::object Item = MaybeItem.value();
				if (!Item.valid() || Item.get_type() == sol::type::nil)
				{
					continue;
				}

				if (!AssignPropertyFromLuaObject(ChildProp, Item))
				{
					return false;
				}
			}
			return true;
		}

		default:
			return false;
		}
	}
}

FString FLuaPropertyBridge::NormalizePropertyName(FString Name)
{
	Name.erase(
		std::remove_if(
			Name.begin(),
			Name.end(),
			[](unsigned char C)
			{
				return std::isspace(C) || C == '_' || C == '-';
			}
		),
		Name.end()
	);

	std::transform(
		Name.begin(),
		Name.end(),
		Name.begin(),
		[](unsigned char C)
		{
			return static_cast<char>(std::tolower(C));
		}
	);

	return Name;
}

bool FLuaPropertyBridge::IsSamePropertyName(const FString& A, const FString& B)
{
	return NormalizePropertyName(A) == NormalizePropertyName(B);
}

bool FLuaPropertyBridge::TryGetDescriptors(UActorComponent* Component, TArray<FPropertyDescriptor>& OutProps)
{
	if (!Component)
	{
		return false;
	}

	Component->GetEditableProperties(OutProps);
	return true;
}

const FPropertyDescriptor* FLuaPropertyBridge::FindDescriptor(const TArray<FPropertyDescriptor>& Props, const FString& PropertyName)
{
	for (const FPropertyDescriptor& Desc : Props)
	{
		if (IsSamePropertyName(Desc.Name, PropertyName))
		{
			return &Desc;
		}
	}

	return nullptr;
}

const char* FLuaPropertyBridge::ToLuaTypeName(const FPropertyDescriptor& Desc)
{
	switch (Desc.GetKind())
	{
	case EPropertyType::Bool:
	case EPropertyType::ByteBool:
		return "bool";

	case EPropertyType::Int:
		return "int";

	case EPropertyType::Float:
		return "float";

	case EPropertyType::Vec3:
		return "FVector";

	case EPropertyType::Vec4:
	case EPropertyType::Color4:
		return "FVector4";

	case EPropertyType::Rotator:
		return "FRotator";

	case EPropertyType::String:
	case EPropertyType::Name:
	case EPropertyType::ObjectRef:
	case EPropertyType::MaterialSlot:
		return "string";

	case EPropertyType::Enum:
		return "int";

	case EPropertyType::Array:
		if (!Desc.GetElementType())
		{
			return "unknown[]";
		}
		switch (Desc.GetElementType()->Kind)
		{
		case EPropertyType::Bool:
		case EPropertyType::ByteBool:
			return "bool[]";
		case EPropertyType::Int:
			return "int[]";
		case EPropertyType::Float:
			return "float[]";
		case EPropertyType::Vec3:
			return "FVector[]";
		case EPropertyType::Vec4:
		case EPropertyType::Color4:
			return "FVector4[]";
		case EPropertyType::Rotator:
			return "FRotator[]";
		case EPropertyType::String:
		case EPropertyType::Name:
		case EPropertyType::ObjectRef:
			return "string[]";
		case EPropertyType::Struct:
			return "table[]";
		default:
			return "unknown[]";
		}

	case EPropertyType::Struct:
		return "table";

	default:
		return "unknown";
	}
}

sol::table FLuaPropertyBridge::ListProperties(sol::this_state State, UActorComponent* Component)
{
	sol::state_view Lua(State);
	sol::table Result = Lua.create_table();

	TArray<FPropertyDescriptor> Props;
	if (!TryGetDescriptors(Component, Props))
	{
		return Result;
	}

	int LuaIndex = 1;

	for (const FPropertyDescriptor& Desc : Props)
	{
		sol::table Item = Lua.create_table();

		Item["name"] = Desc.Name;
		Item["type"] = ToLuaTypeName(Desc);
		Item["min"] = Desc.Min;
		Item["max"] = Desc.Max;
		Item["speed"] = Desc.Speed;

		if (Desc.GetKind() == EPropertyType::Enum && Desc.GetEnumNames() && Desc.GetEnumCount() > 0)
		{
			sol::table EnumNames = Lua.create_table();

			for (uint32 EnumIndex = 0; EnumIndex < Desc.GetEnumCount(); ++EnumIndex)
			{
				EnumNames[EnumIndex + 1] = Desc.GetEnumNames()[EnumIndex];
			}

			Item["enumNames"] = EnumNames;
		}

		Result[LuaIndex++] = Item;
	}

	return Result;
}

bool FLuaPropertyBridge::HasProperty(UActorComponent* Component, const FString& PropertyName)
{
	TArray<FPropertyDescriptor> Props;
	if (!TryGetDescriptors(Component, Props))
	{
		return false;
	}

	return FindDescriptor(Props, PropertyName) != nullptr;
}

sol::object FLuaPropertyBridge::GetProperty(sol::this_state State, UActorComponent* Component, const FString& PropertyName)
{
	sol::state_view Lua(State);

	TArray<FPropertyDescriptor> Props;
	if (!TryGetDescriptors(Component, Props))
	{
		return sol::nil;
	}

	const FPropertyDescriptor* Desc = FindDescriptor(Props, PropertyName);

	if (!Desc || !Desc->ValuePtr)
	{
		return sol::nil;
	}

	return MakeLuaObjectForProperty(Lua, *Desc);
}

bool FLuaPropertyBridge::SetProperty(UActorComponent* Component, const FString& PropertyName, const sol::object& Value)
{
	if (!Component)
	{
		return false;
	}

	TArray<FPropertyDescriptor> Props;
	if (!TryGetDescriptors(Component, Props))
	{
		return false;
	}

	const FPropertyDescriptor* Desc = FindDescriptor(Props, PropertyName);

	if (!Desc || !Desc->ValuePtr)
	{
		UE_LOG("[Lua] SetProperty failed: property not found = %s", PropertyName.c_str());
		return false;
	}

	if (IsBlockedLuaPropertyName(Desc->Name))
	{
		UE_LOG("[LuaSecurity] SetProperty blocked: protected property = %s", Desc->Name.c_str());
		return false;
	}

	try
	{
		if (!AssignPropertyFromLuaObject(*Desc, Value))
		{
			UE_LOG("[Lua] SetProperty failed: unsupported property type = %s", PropertyName.c_str());
			return false;
		}
	}
	catch (const sol::error& Error)
	{
		UE_LOG("[Lua] SetProperty type error: %s", Error.what());
		return false;
	}

	Component->PostEditProperty(Desc->Name.c_str());
	return true;
}
