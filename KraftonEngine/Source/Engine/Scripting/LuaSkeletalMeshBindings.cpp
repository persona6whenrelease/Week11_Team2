#include "LuaBindings.h"
#include "SolInclude.h"

#include "LuaBindingHelper.h"
#include "LuaHandles.h"
#include "LuaPropertyBridge.h"
#include "LuaWorldLibrary.h"

#include "Asset/Animation/Core/AnimInstance.h"
#include "Asset/Animation/Core/AnimGraph.h"
#include "Asset/Animation/Core/AnimGraph_StateMachine.h"
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

	FName ReadNameField(const sol::table& Table, const char* PrimaryKey, const char* SecondaryKey = nullptr)
	{
		sol::optional<FString> Primary = Table[PrimaryKey];
		if (Primary && !Primary->empty())
		{
			return FName(Primary.value());
		}

		if (SecondaryKey)
		{
			sol::optional<FString> Secondary = Table[SecondaryKey];
			if (Secondary && !Secondary->empty())
			{
				return FName(Secondary.value());
			}
		}

		return FName();
	}

	bool ReadBoolField(const sol::table& Table, const char* Key, bool DefaultValue)
	{
		sol::optional<bool> Value = Table[Key];
		return Value.value_or(DefaultValue);
	}

	float ReadFloatField(const sol::table& Table, const char* Key, float DefaultValue)
	{
		sol::optional<float> Value = Table[Key];
		return Value.value_or(DefaultValue);
	}

	int32 FindStateIndexByName(const TArray<FAnimState>& States, const FName& StateName)
	{
		if (!StateName.IsValid())
		{
			return -1;
		}

		for (int32 Index = 0; Index < static_cast<int32>(States.size()); ++Index)
		{
			if (States[Index].Name == StateName)
			{
				return Index;
			}
		}

		return -1;
	}

	UAnimSequence* ResolveSequenceFromObject(const sol::object& Value)
	{
		if (!Value.valid())
		{
			return nullptr;
		}

		if (Value.is<FLuaAnimSequenceHandle>())
		{
			return Value.as<FLuaAnimSequenceHandle>().Resolve();
		}

		if (Value.is<FString>())
		{
			return FLuaWorldLibrary::LoadAnimSequence(Value.as<FString>());
		}

		if (Value.is<std::string>())
		{
			return FLuaWorldLibrary::LoadAnimSequence(Value.as<std::string>());
		}

		return nullptr;
	}

	std::unique_ptr<FAnimGraphNode_Base> BuildSequenceNode(USkeleton* Skeleton, UAnimSequence* Sequence)
	{
		if (!Skeleton || !Sequence)
		{
			return nullptr;
		}

		auto Node = std::make_unique<FAnimGraphNode_SequencePlayer>();
		Node->SetSequence(Skeleton, Sequence);
		return Node;
	}

	std::unique_ptr<FAnimGraphNode_StateMachine> BuildStateMachineNodeFromLua(
		USkeleton* Skeleton,
		const sol::table& Config,
		FString& OutError);

	bool ParseTransitionCondition(const sol::table& ConditionTable, FAnimTransitionCondition& OutCondition)
	{
		sol::optional<FString> KindValue = ConditionTable["kind"];
		if (!KindValue)
		{
			sol::optional<FString> TypeValue = ConditionTable["type"];
			if (TypeValue)
			{
				KindValue = TypeValue.value();
			}
		}
		if (!KindValue)
		{
			return false;
		}

		FString Kind = KindValue.value();
		std::transform(
			Kind.begin(),
			Kind.end(),
			Kind.begin(),
			[](unsigned char C)
			{
				return static_cast<char>(std::tolower(C));
			}
		);

		if (Kind == "bool" || Kind == "boolean" || Kind == "boolvariable")
		{
			OutCondition.Kind = EAnimTransitionConditionKind::BoolVariable;
			OutCondition.VarName = ReadNameField(ConditionTable, "name", "var");
			OutCondition.bExpectedValue = ReadBoolField(ConditionTable, "value", true);
			return OutCondition.VarName.IsValid();
		}

		if (Kind == "time" || Kind == "timeelapsed")
		{
			OutCondition.Kind = EAnimTransitionConditionKind::TimeElapsed;
			OutCondition.TimeThreshold = ReadFloatField(ConditionTable, "value", 0.0f);
			sol::optional<float> Time = ConditionTable["time"];
			if (Time)
			{
				OutCondition.TimeThreshold = Time.value();
			}
			return true;
		}

		return false;
	}

	std::unique_ptr<FAnimGraphNode_StateMachine> BuildStateMachineNodeFromLua(
		USkeleton* Skeleton,
		const sol::table& Config,
		FString& OutError)
	{
		if (!Skeleton)
		{
			OutError = "missing skeleton";
			return nullptr;
		}

		sol::optional<sol::table> MaybeStates = Config["states"];
		if (!MaybeStates)
		{
			OutError = "config.states is required";
			return nullptr;
		}

		auto Root = std::make_unique<FAnimGraphNode_StateMachine>();
		for (const auto& Entry : MaybeStates.value())
		{
			if (Entry.second.get_type() != sol::type::table)
			{
				continue;
			}

			sol::table StateTable = Entry.second.as<sol::table>();
			FAnimState State;
			State.Name = ReadNameField(StateTable, "name");
			if (!State.Name.IsValid())
			{
				OutError = "every state needs a name";
				return nullptr;
			}

			sol::optional<sol::object> SequenceObject = StateTable["sequence"];
			if (!SequenceObject)
			{
				SequenceObject = StateTable["anim"];
			}

			UAnimSequence* Sequence = nullptr;
			if (SequenceObject)
			{
				Sequence = ResolveSequenceFromObject(SequenceObject.value());
				if (Sequence)
				{
					State.Sub = BuildSequenceNode(Skeleton, Sequence);
				}
			}

			if (!State.Sub)
			{
				sol::optional<sol::table> MaybeSubmachine = StateTable["submachine"];
				if (MaybeSubmachine)
				{
					State.Sub = BuildStateMachineNodeFromLua(Skeleton, MaybeSubmachine.value(), OutError);
				}
			}

			if (!State.Sub)
			{
				if (OutError.empty())
				{
					OutError = "state " + State.Name.ToString() + " is missing a valid sequence or submachine";
				}
				return nullptr;
			}

			State.bLooping = ReadBoolField(StateTable, "looping", true);
			State.bResetTimeOnEnter = ReadBoolField(StateTable, "reset_time_on_enter", true);
			sol::optional<float> ExplicitLength = StateTable["length"];
			State.SubLengthHint = ExplicitLength.value_or(Sequence ? Sequence->GetPlayLength() : 0.0f);
			Root->States.push_back(std::move(State));
		}

		if (Root->States.empty())
		{
			OutError = "at least one state is required";
			return nullptr;
		}

		sol::optional<FString> InitialStateName = Config["initial_state"];
		if (InitialStateName)
		{
			const int32 InitialIndex = FindStateIndexByName(Root->States, FName(InitialStateName.value()));
			if (InitialIndex < 0)
			{
				OutError = "unknown initial_state = " + InitialStateName.value();
				return nullptr;
			}
			Root->InitialStateIndex = InitialIndex;
		}

		sol::optional<sol::table> MaybeTransitions = Config["transitions"];
		if (MaybeTransitions)
		{
			for (const auto& Entry : MaybeTransitions.value())
			{
				if (Entry.second.get_type() != sol::type::table)
				{
					continue;
				}

				sol::table TransitionTable = Entry.second.as<sol::table>();
				FAnimTransition Transition;
				Transition.FromStateIndex = FindStateIndexByName(
					Root->States,
					ReadNameField(TransitionTable, "from")
				);
				Transition.ToStateIndex = FindStateIndexByName(
					Root->States,
					ReadNameField(TransitionTable, "to")
				);
				Transition.BlendDuration = ReadFloatField(
					TransitionTable,
					"blend_duration",
					Transition.BlendDuration
				);

				if (Transition.FromStateIndex < 0 || Transition.ToStateIndex < 0)
				{
					OutError = "transition has unknown from/to state";
					return nullptr;
				}

				sol::optional<sol::table> MaybeConditions = TransitionTable["conditions"];
				if (!MaybeConditions)
				{
					OutError = "every transition needs conditions";
					return nullptr;
				}

				for (const auto& ConditionEntry : MaybeConditions.value())
				{
					if (ConditionEntry.second.get_type() != sol::type::table)
					{
						continue;
					}

					FAnimTransitionCondition Condition;
					if (!ParseTransitionCondition(ConditionEntry.second.as<sol::table>(), Condition))
					{
						OutError = "unsupported condition in transition";
						return nullptr;
					}
					Transition.Conditions.push_back(Condition);
				}

				if (Transition.Conditions.empty())
				{
					OutError = "transition conditions cannot be empty";
					return nullptr;
				}

				Root->Transitions.push_back(std::move(Transition));
			}
		}

		return Root;
	}

	bool BuildStateMachineFromLua(
		USkeletalMeshComponent* Component,
		UAnimStateMachineInstance* StateMachine,
		const sol::table& Config)
	{
		if (!Component || !StateMachine)
		{
			return false;
		}

		USkeleton* Skeleton = StateMachine->GetSkeleton();
		if (!Skeleton)
		{
			UE_LOG("[Lua] State machine graph setup failed: missing skeleton.");
			return false;
		}

		FString Error;
		std::unique_ptr<FAnimGraphNode_StateMachine> Root = BuildStateMachineNodeFromLua(Skeleton, Config, Error);
		if (!Root)
		{
			UE_LOG("[Lua] State machine graph setup failed: %s", Error.c_str());
			return false;
		}

		StateMachine->SetStateMachineGraph(std::move(Root));
		return true;
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

		"SetupStateMachineGraph",
		[](const FLuaSkeletalMeshComponentHandle& Self, sol::table Config)
		{
			USkeletalMeshComponent* Component = Self.Resolve();
			if (!Component)
			{
				UE_LOG("[Lua] Invalid SkeletalMeshComponent.SetupStateMachineGraph Call.");
				return false;
			}

			Component->SetAnimationMode(EAnimationMode::AnimationStateMachine);
			UAnimStateMachineInstance* StateMachine = Cast<UAnimStateMachineInstance>(Component->GetAnimInstance());
			if (!StateMachine)
			{
				UE_LOG("[Lua] Failed to create animation state machine instance.");
				return false;
			}

			if (!BuildStateMachineFromLua(Component, StateMachine, Config))
			{
				return false;
			}

			// State machine graphs are expected to tick immediately after setup.
			Component->Play(true);
			Component->RefreshAnimationPose();
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
