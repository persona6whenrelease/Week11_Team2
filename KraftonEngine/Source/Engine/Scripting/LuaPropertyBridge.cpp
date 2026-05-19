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
		return Type == EPropertyType::StaticMeshRef ||
			Type == EPropertyType::SkeletalMeshRef ||
			Type == EPropertyType::SceneComponentRef ||
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
		if (!ArrayProp.ArrayElementGetter || !ArrayProp.ValuePtr)
		{
			return false;
		}

		OutElementProp = ArrayProp;
		OutElementProp.Type = ArrayProp.InnerType;
		OutElementProp.Name = "[" + std::to_string(ElementIndex) + "]";
		OutElementProp.ValuePtr = ArrayProp.ArrayElementGetter(ArrayProp.ValuePtr, ElementIndex);
		OutElementProp.ArraySizeGetter = nullptr;
		OutElementProp.ArrayResizeFunc = nullptr;
		OutElementProp.ArrayElementGetter = nullptr;
		OutElementProp.ArrayElementConstGetter = nullptr;
		return OutElementProp.ValuePtr != nullptr;
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
	switch (Desc.Type)
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
	case EPropertyType::SceneComponentRef:
	case EPropertyType::StaticMeshRef:
	case EPropertyType::SkeletalMeshRef:
	case EPropertyType::MaterialSlot:
		return "string";

	case EPropertyType::Enum:
		return "int";

	case EPropertyType::Array:
		switch (Desc.InnerType)
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
			return "string[]";
		default:
			return "unknown[]";
		}

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

		if (Desc.Type == EPropertyType::Enum && Desc.EnumNames && Desc.EnumCount > 0)
		{
			sol::table EnumNames = Lua.create_table();

			for (uint32 EnumIndex = 0; EnumIndex < Desc.EnumCount; ++EnumIndex)
			{
				EnumNames[EnumIndex + 1] = Desc.EnumNames[EnumIndex];
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

	switch (Desc->Type)
	{
	case EPropertyType::Bool:
		return sol::make_object(Lua, *static_cast<bool*>(Desc->ValuePtr));

	case EPropertyType::ByteBool:
		return sol::make_object(Lua, *static_cast<uint8*>(Desc->ValuePtr) != 0);

	case EPropertyType::Int:
		return sol::make_object(Lua, *static_cast<int32*>(Desc->ValuePtr));

	case EPropertyType::Float:
		return sol::make_object(Lua, *static_cast<float*>(Desc->ValuePtr));

	case EPropertyType::Vec3:
		return sol::make_object(Lua, *static_cast<FVector*>(Desc->ValuePtr));

	case EPropertyType::Vec4:
	case EPropertyType::Color4:
		return sol::make_object(Lua, *static_cast<FVector4*>(Desc->ValuePtr));

	case EPropertyType::Rotator:
		return sol::make_object(Lua, *static_cast<FRotator*>(Desc->ValuePtr));

	case EPropertyType::String:
	case EPropertyType::StaticMeshRef:
	case EPropertyType::SkeletalMeshRef:
	case EPropertyType::SceneComponentRef:
		return sol::make_object(Lua, *static_cast<FString*>(Desc->ValuePtr));

	case EPropertyType::Name:
		return sol::make_object(Lua, static_cast<FName*>(Desc->ValuePtr)->ToString());

	case EPropertyType::MaterialSlot:
		return sol::make_object(Lua, static_cast<FMaterialSlot*>(Desc->ValuePtr)->Path);

	case EPropertyType::Enum:
		return sol::make_object(Lua, *reinterpret_cast<int32*>(Desc->ValuePtr));

	case EPropertyType::Array:
	{
		if (!Desc->ArraySizeGetter)
		{
			return sol::nil;
		}

		sol::table Table = Lua.create_table();
		const size_t Count = Desc->ArraySizeGetter(Desc->ValuePtr);
		for (size_t Index = 0; Index < Count; ++Index)
		{
			FPropertyDescriptor ElementDesc;
			if (!BuildArrayElementDescriptor(*Desc, Index, ElementDesc))
			{
				continue;
			}
			switch (ElementDesc.Type)
			{
			case EPropertyType::Bool:
				Table[static_cast<int>(Index + 1)] = *static_cast<bool*>(ElementDesc.ValuePtr);
				break;
			case EPropertyType::ByteBool:
				Table[static_cast<int>(Index + 1)] = (*static_cast<uint8*>(ElementDesc.ValuePtr) != 0);
				break;
			case EPropertyType::Int:
				Table[static_cast<int>(Index + 1)] = *static_cast<int32*>(ElementDesc.ValuePtr);
				break;
			case EPropertyType::Float:
				Table[static_cast<int>(Index + 1)] = *static_cast<float*>(ElementDesc.ValuePtr);
				break;
			case EPropertyType::Vec3:
				Table[static_cast<int>(Index + 1)] = *static_cast<FVector*>(ElementDesc.ValuePtr);
				break;
			case EPropertyType::Vec4:
			case EPropertyType::Color4:
				Table[static_cast<int>(Index + 1)] = *static_cast<FVector4*>(ElementDesc.ValuePtr);
				break;
			case EPropertyType::Rotator:
				Table[static_cast<int>(Index + 1)] = *static_cast<FRotator*>(ElementDesc.ValuePtr);
				break;
			case EPropertyType::String:
				Table[static_cast<int>(Index + 1)] = *static_cast<FString*>(ElementDesc.ValuePtr);
				break;
			case EPropertyType::Name:
				Table[static_cast<int>(Index + 1)] = static_cast<FName*>(ElementDesc.ValuePtr)->ToString();
				break;
			default:
				Table[static_cast<int>(Index + 1)] = sol::nil;
				break;
			}
		}
		return sol::make_object(Lua, Table);
	}

	default:
		return sol::nil;
	}
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
		switch (Desc->Type)
		{
		case EPropertyType::Bool:
			*static_cast<bool*>(Desc->ValuePtr) = Value.as<bool>();
			break;

		case EPropertyType::ByteBool:
			*static_cast<uint8*>(Desc->ValuePtr) = Value.as<bool>() ? 1 : 0;
			break;

		case EPropertyType::Int:
		{
			const int32 NewValue = ClampIntForDescriptor(*Desc, Value.as<int32>());
			*static_cast<int32*>(Desc->ValuePtr) = NewValue;
			break;
		}

		case EPropertyType::Float:
		{
			const float NewValue = ClampFloatForDescriptor(*Desc, Value.as<float>());
			*static_cast<float*>(Desc->ValuePtr) = NewValue;
			break;
		}

		case EPropertyType::Vec3:
			*static_cast<FVector*>(Desc->ValuePtr) = Value.as<FVector>();
			break;

		case EPropertyType::Vec4:
		case EPropertyType::Color4:
			*static_cast<FVector4*>(Desc->ValuePtr) = Value.as<FVector4>();
			break;

		case EPropertyType::Rotator:
			*static_cast<FRotator*>(Desc->ValuePtr) = Value.as<FRotator>();
			break;

		case EPropertyType::String:
		{
			*static_cast<FString*>(Desc->ValuePtr) = Value.as<FString>();
			break;
		}

		case EPropertyType::StaticMeshRef:
		case EPropertyType::SkeletalMeshRef:
		case EPropertyType::SceneComponentRef:
		{
			const FString NewValue = Value.as<FString>();
			if (IsLuaPathProperty(Desc->Type) && !IsSafeLuaAssetPath(NewValue))
			{
				UE_LOG("[LuaSecurity] SetProperty blocked: unsafe asset/reference path. property = %s, value = %s", Desc->Name.c_str(), NewValue.c_str());
				return false;
			}

			*static_cast<FString*>(Desc->ValuePtr) = NewValue;
			break;
		}

		case EPropertyType::Name:
			*static_cast<FName*>(Desc->ValuePtr) = FName(Value.as<FString>());
			break;

		case EPropertyType::MaterialSlot:
		{
			const FString NewPath = Value.as<FString>();
			if (!IsSafeLuaAssetPath(NewPath))
			{
				UE_LOG("[LuaSecurity] SetProperty blocked: unsafe material path. property = %s, value = %s", Desc->Name.c_str(), NewPath.c_str());
				return false;
			}

			static_cast<FMaterialSlot*>(Desc->ValuePtr)->Path = NewPath;
			break;
		}

		case EPropertyType::Enum:
		{
			const int32 NewValue = Value.as<int32>();

			if (Desc->EnumCount > 0 && (NewValue < 0 || NewValue >= static_cast<int32>(Desc->EnumCount)))
			{
				UE_LOG("[LuaSecurity] SetProperty blocked: enum value out of range. property = %s, value = %d", Desc->Name.c_str(), NewValue);
				return false;
			}

			*reinterpret_cast<int32*>(Desc->ValuePtr) = NewValue;
			break;
		}

		case EPropertyType::Array:
		{
			const std::size_t MaxLuaArraySize = 1024;
			if (!Desc->ArrayResizeFunc)
			{
				return false;
			}

			sol::table Table = Value.as<sol::table>();
			const std::size_t Count = Table.size();
			if (Count > MaxLuaArraySize)
			{
				UE_LOG("[LuaSecurity] SetProperty blocked: array too large. property = %s, count = %zu", Desc->Name.c_str(), Count);
				return false;
			}

			Desc->ArrayResizeFunc(Desc->ValuePtr, Count);
			for (std::size_t Index = 1; Index <= Count; ++Index)
			{
				sol::object Item = Table[static_cast<int>(Index)];
				if (!Item.valid() || Item.get_type() == sol::type::nil)
				{
					UE_LOG("[Lua] SetProperty failed: array contains nil. property = %s, index = %zu", Desc->Name.c_str(), Index);
					return false;
				}

				FPropertyDescriptor ElementDesc;
				if (!BuildArrayElementDescriptor(*Desc, Index - 1, ElementDesc))
				{
					return false;
				}

				switch (ElementDesc.Type)
				{
				case EPropertyType::Bool:
					*static_cast<bool*>(ElementDesc.ValuePtr) = Item.as<bool>();
					break;
				case EPropertyType::ByteBool:
					*static_cast<uint8*>(ElementDesc.ValuePtr) = Item.as<bool>() ? 1 : 0;
					break;
				case EPropertyType::Int:
					*static_cast<int32*>(ElementDesc.ValuePtr) = ClampIntForDescriptor(ElementDesc, Item.as<int32>());
					break;
				case EPropertyType::Float:
					*static_cast<float*>(ElementDesc.ValuePtr) = ClampFloatForDescriptor(ElementDesc, Item.as<float>());
					break;
				case EPropertyType::Vec3:
					*static_cast<FVector*>(ElementDesc.ValuePtr) = Item.as<FVector>();
					break;
				case EPropertyType::Vec4:
				case EPropertyType::Color4:
					*static_cast<FVector4*>(ElementDesc.ValuePtr) = Item.as<FVector4>();
					break;
				case EPropertyType::Rotator:
					*static_cast<FRotator*>(ElementDesc.ValuePtr) = Item.as<FRotator>();
					break;
				case EPropertyType::String:
					*static_cast<FString*>(ElementDesc.ValuePtr) = Item.as<FString>();
					break;
				case EPropertyType::Name:
					*static_cast<FName*>(ElementDesc.ValuePtr) = FName(Item.as<FString>());
					break;
				default:
					UE_LOG("[Lua] SetProperty failed: unsupported array element type = %s", Desc->Name.c_str());
					return false;
				}
			}
			break;
		}

		default:
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
