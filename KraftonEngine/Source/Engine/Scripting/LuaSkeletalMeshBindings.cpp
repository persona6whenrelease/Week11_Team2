#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"
#include "LuaPropertyBridge.h"
#include "LuaWorldLibrary.h"

#include "Asset/Animation/Core/AnimInstance.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Animation/Core/AnimStateMachineInstance.h"
#include "Asset/Import/MeshManager.h"
#include "Component/SkeletalMeshComponent.h"
#include "Core/Log.h"

#include <algorithm>
#include <cctype>

namespace
{
	FLuaAnimSequenceHandle MakeAnimSequenceHandle(UAnimSequence* Sequence)
	{
		FLuaAnimSequenceHandle Handle;
		if (Sequence)
		{
			Handle.UUID = Sequence->GetUUID();
		}
		return Handle;
	}

	FString ToAnimationModeString(EAnimationMode Mode)
	{
		switch (Mode)
		{
		case EAnimationMode::AnimationStateMachine:
			return "StateMachine";
		case EAnimationMode::AnimationSingleNode:
		default:
			return "SingleNode";
		}
	}

	bool TryParseAnimationMode(const FString& Value, EAnimationMode& OutMode)
	{
		FString Normalized = Value;
		std::transform(
			Normalized.begin(),
			Normalized.end(),
			Normalized.begin(),
			[](unsigned char C)
			{
				return static_cast<char>(std::tolower(C));
			}
		);

		if (Normalized == "singlenode" || Normalized == "single")
		{
			OutMode = EAnimationMode::AnimationSingleNode;
			return true;
		}

		if (Normalized == "statemachine" || Normalized == "state")
		{
			OutMode = EAnimationMode::AnimationStateMachine;
			return true;
		}

		return false;
	}

	bool TryGetOwningSceneAssetPath(const USkeletalMeshComponent* Component, FString& OutScenePath)
	{
		OutScenePath.clear();

		const USkeletalMesh* SkeletalMesh = Component ? Component->GetSkeletalMesh() : nullptr;
		if (!SkeletalMesh)
		{
			return false;
		}

		const FString& MeshPath = SkeletalMesh->GetAssetPathFileName();
		const size_t MarkerIndex = MeshPath.find('#');
		if (MarkerIndex == FString::npos)
		{
			return false;
		}

		OutScenePath = MeshPath.substr(0, MarkerIndex);
		return !OutScenePath.empty();
	}

	UAnimSequence* FindAnimSequenceForComponent(USkeletalMeshComponent* Component, int32 SequenceIndex, FString* OutPath = nullptr)
	{
		if (OutPath)
		{
			OutPath->clear();
		}

		if (!Component || SequenceIndex < 0)
		{
			return nullptr;
		}

		FString ScenePath;
		if (!TryGetOwningSceneAssetPath(Component, ScenePath))
		{
			return nullptr;
		}

		UFBXSceneAsset* SceneAsset = FMeshManager::LoadFbxScene(ScenePath);
		if (!SceneAsset)
		{
			return nullptr;
		}

		return FMeshManager::FindAnimSequenceForSkeletalMesh(
			SceneAsset,
			Component->GetSkeletalMesh(),
			SequenceIndex,
			OutPath
		);
	}

	int32 GetAnimSequenceCountForComponent(USkeletalMeshComponent* Component)
	{
		if (!Component)
		{
			return 0;
		}

		FString ScenePath;
		if (!TryGetOwningSceneAssetPath(Component, ScenePath))
		{
			return 0;
		}

		UFBXSceneAsset* SceneAsset = FMeshManager::LoadFbxScene(ScenePath);
		return SceneAsset
			? FMeshManager::GetAnimSequenceCountForSkeletalMesh(SceneAsset, Component->GetSkeletalMesh())
			: 0;
	}
}

void RegisterSkeletalMeshComponentBinding(sol::state& Lua)
{
	Lua.new_usertype<FLuaAnimSequenceHandle>(
		"AnimSequence",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaAnimSequenceHandle),

		"Name",
		sol::property(
			[](const FLuaAnimSequenceHandle& Self)
			{
				UAnimSequence* Sequence = Self.Resolve();
				return Sequence ? Sequence->GetSequenceName() : FString();
			}
		),

		"PlayLength",
		sol::property(
			[](const FLuaAnimSequenceHandle& Self)
			{
				UAnimSequence* Sequence = Self.Resolve();
				return Sequence ? Sequence->GetPlayLength() : 0.0f;
			}
		),

		"SkeletonAssetPath",
		sol::property(
			[](const FLuaAnimSequenceHandle& Self)
			{
				UAnimSequence* Sequence = Self.Resolve();
				return Sequence ? Sequence->GetSkeletonAssetPath() : FString();
			}
		),

		"Valid",
		sol::property(
			[](const FLuaAnimSequenceHandle& Self)
			{
				UAnimSequence* Sequence = Self.Resolve();
				return Sequence ? Sequence->IsValidSequence() : false;
			}
		),

		"GetNotifies",
		[](const FLuaAnimSequenceHandle& Self, sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::table Result = LuaView.create_table();

			UAnimSequence* Sequence = Self.Resolve();
			if (!Sequence)
			{
				return Result;
			}

			int32 LuaIndex = 1;
			for (const FAnimNotifyEvent& Notify : Sequence->GetNotifies())
			{
				sol::table Entry = LuaView.create_table();
				Entry["Name"] = Notify.NotifyName.ToString();
				Entry["TriggerTime"] = Notify.TriggerTime;
				Entry["Duration"] = Notify.Duration;
				Result[LuaIndex++] = Entry;
			}

			return Result;
		}
	);

	Lua.new_usertype<FLuaSkeletalMeshComponentHandle>(
		"SkeletalMeshComponent",

		sol::no_constructor,

		LUA_HANDLE_COMMON(FLuaSkeletalMeshComponentHandle),

		"MeshPath",
		sol::property(
			[](const FLuaSkeletalMeshComponentHandle& Self)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				return Component ? Component->GetSkeletalMeshPath() : FString("None");
			}
		),

		"AnimationMode",
		sol::property(
			[](const FLuaSkeletalMeshComponentHandle& Self)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				return Component ? ToAnimationModeString(Component->GetAnimationMode()) : FString("SingleNode");
			},
			[](const FLuaSkeletalMeshComponentHandle& Self, const FString& Value)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SkeletalMeshComponent.AnimationMode Access.");
					return;
				}

				EAnimationMode Mode = EAnimationMode::AnimationSingleNode;
				if (!TryParseAnimationMode(Value, Mode))
				{
					UE_LOG("[Lua] Invalid SkeletalMeshComponent.AnimationMode Value = %s", Value.c_str());
					return;
				}

				Component->SetAnimationMode(Mode);
			}
		),

		"CurrentTime",
		sol::property(
			[](const FLuaSkeletalMeshComponentHandle& Self)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				return Component ? Component->GetBakedAnimTime() : 0.0f;
			},
			[](const FLuaSkeletalMeshComponentHandle& Self, float Value)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SkeletalMeshComponent.CurrentTime Access.");
					return;
				}
				Component->SetBakedAnimTime(Value);
			}
		),

		"PlaybackSpeed",
		sol::property(
			[](const FLuaSkeletalMeshComponentHandle& Self)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				return Component ? Component->GetBakedAnimPlaybackSpeed() : 1.0f;
			},
			[](const FLuaSkeletalMeshComponentHandle& Self, float Value)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SkeletalMeshComponent.PlaybackSpeed Access.");
					return;
				}
				Component->SetBakedAnimPlaybackSpeed(Value);
			}
		),

		"Paused",
		sol::property(
			[](const FLuaSkeletalMeshComponentHandle& Self)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				return Component ? Component->IsBakedAnimPaused() : true;
			},
			[](const FLuaSkeletalMeshComponentHandle& Self, bool Value)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				if (!Component)
				{
					UE_LOG("[Lua] Invalid SkeletalMeshComponent.Paused Access.");
					return;
				}
				Component->SetBakedAnimPaused(Value);
			}
		),

		"Animation",
		sol::property(
			[](const FLuaSkeletalMeshComponentHandle& Self)
			{
				USkeletalMeshComponent* Component = Self.Resolve();
				return MakeAnimSequenceHandle(Component ? Cast<UAnimSequence>(Component->GetAnimation()) : nullptr);
			}
		),

		"SetSkeletalMesh",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FString& SkeletalMeshPath)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.SetSkeletalMesh Call.");
				return false;
			}

			return FLuaWorldLibrary::SetSkeletalMesh(Component, SkeletalMeshPath);
		},

		"SetAnimation",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FLuaAnimSequenceHandle& SequenceHandle)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			UAnimSequence* Sequence = SequenceHandle.Resolve();
			if (!Component || !Sequence)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.SetAnimation Call.");
				return false;
			}

			Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Component->SetAnimation(Sequence);
			return true;
		},

		"PlayAnimation",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FLuaAnimSequenceHandle& SequenceHandle, sol::optional<bool> bLooping)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			UAnimSequence* Sequence = SequenceHandle.Resolve();
			if (!Component || !Sequence)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.PlayAnimation Call.");
				return false;
			}

			Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Component->PlayAnimation(Sequence, bLooping.value_or(true));
			return true;
		},

		"PlayAnimationPath",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FString& SequencePath, sol::optional<bool> bLooping)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.PlayAnimationPath Call.");
				return false;
			}

			UAnimSequence* Sequence = FLuaWorldLibrary::LoadAnimSequence(SequencePath);
			if (!Sequence)
			{
				UE_LOG("[Lua] Failed to load anim sequence = %s", SequencePath.c_str());
				return false;
			}

			Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Component->PlayAnimation(Sequence, bLooping.value_or(true));
			return true;
		},

		"Play",
		[](const FLuaSkeletalMeshComponentHandle& Self, sol::optional<bool> bLooping)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.Play Call.");
				return;
			}
			Component->Play(bLooping.value_or(true));
		},

		"Stop",
		[](const FLuaSkeletalMeshComponentHandle& Self)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.Stop Call.");
				return;
			}
			Component->Stop();
		},

		"RefreshAnimationPose",
		[](const FLuaSkeletalMeshComponentHandle& Self)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.RefreshAnimationPose Call.");
				return;
			}
			Component->RefreshAnimationPose();
		},

		"GetAnimSequenceCount",
		[](const FLuaSkeletalMeshComponentHandle& Self)
		{
			return GetAnimSequenceCountForComponent(Self.Resolve());
		},

		"GetAnimSequence",
		[](const FLuaSkeletalMeshComponentHandle& Self, int32 LuaIndex)
		{
			if (LuaIndex <= 0)
			{
				return FLuaAnimSequenceHandle();
			}
			return MakeAnimSequenceHandle(FindAnimSequenceForComponent(Self.Resolve(), LuaIndex - 1));
		},

		"GetAnimSequencePath",
		[](const FLuaSkeletalMeshComponentHandle& Self, int32 LuaIndex)
		{
			if (LuaIndex <= 0)
			{
				return FString();
			}

			FString OutPath;
			FindAnimSequenceForComponent(Self.Resolve(), LuaIndex - 1, &OutPath);
			return OutPath;
		},

		"GetTriggeredNotifies",
		[](const FLuaSkeletalMeshComponentHandle& Self, sol::this_state State)
		{
			sol::state_view LuaView(State);
			sol::table Result = LuaView.create_table();

			USkeletalMeshComponent* Component = Self.Resolve();
			UAnimInstance* AnimInstance = Component ? Component->GetAnimInstance() : nullptr;
			if (!AnimInstance)
			{
				return Result;
			}

			int32 LuaIndex = 1;
			for (const FName& NotifyName : AnimInstance->GetTriggeredNotifiesThisFrame())
			{
				Result[LuaIndex++] = NotifyName.ToString();
			}

			return Result;
		},

		"SetStateBool",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FString& Name, bool Value)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			UAnimStateMachineInstance* StateMachine = Component
				? Cast<UAnimStateMachineInstance>(Component->GetAnimInstance())
				: nullptr;
			if (!StateMachine)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.SetStateBool Call.");
				return false;
			}

			StateMachine->SetBoolVariable(FName(Name), Value);
			return true;
		},

		"GetStateBool",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FString& Name, sol::optional<bool> DefaultValue)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			UAnimStateMachineInstance* StateMachine = Component
				? Cast<UAnimStateMachineInstance>(Component->GetAnimInstance())
				: nullptr;
			if (!StateMachine)
			{
				return DefaultValue.value_or(false);
			}

			return StateMachine->GetBoolVariable(FName(Name), DefaultValue.value_or(false));
		},

		"ListProperties",
		[](const FLuaSkeletalMeshComponentHandle& Self, sol::this_state State)
		{
			return FLuaPropertyBridge::ListProperties(State, Self.Resolve());
		},

		"HasProperty",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FString& Name)
		{
			return FLuaPropertyBridge::HasProperty(Self.Resolve(), Name);
		},

		"GetProperty",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FString& Name, sol::this_state State)
		{
			return FLuaPropertyBridge::GetProperty(State, Self.Resolve(), Name);
		},

		"SetProperty",
		[](const FLuaSkeletalMeshComponentHandle& Self, const FString& Name, sol::object Value)
		{
			return FLuaPropertyBridge::SetProperty(Self.Resolve(), Name, Value);
		}
	);

	sol::table Animation = Lua.get_or("Animation", Lua.create_table());
	Lua["Animation"] = Animation;

	Animation.set_function("LoadSequence", [](const FString& SequencePath)
	{
		return MakeAnimSequenceHandle(FLuaWorldLibrary::LoadAnimSequence(SequencePath));
	});
}
