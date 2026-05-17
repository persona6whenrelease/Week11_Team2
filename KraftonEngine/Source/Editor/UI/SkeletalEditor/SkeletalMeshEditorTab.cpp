#include "Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.h"
#include "Editor/UI/SkeletalEditor/SkeletonTreeUtil.h"

#include "Component/SkeletalMeshComponent.h"
#include "Editor/Viewport/SkeletalMeshViewerViewportClient.h"
#include "Asset/Import/MeshManager.h"
#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{
	FVector GetRotationEulerNoScale(const FMatrix& Matrix)
	{
		FMatrix RotationMatrix = Matrix;
		const FVector Scale = Matrix.GetScale();
		if (std::abs(Scale.X) > 0.0001f)
		{
			RotationMatrix.M[0][0] /= Scale.X;
			RotationMatrix.M[0][1] /= Scale.X;
			RotationMatrix.M[0][2] /= Scale.X;
		}
		if (std::abs(Scale.Y) > 0.0001f)
		{
			RotationMatrix.M[1][0] /= Scale.Y;
			RotationMatrix.M[1][1] /= Scale.Y;
			RotationMatrix.M[1][2] /= Scale.Y;
		}
		if (std::abs(Scale.Z) > 0.0001f)
		{
			RotationMatrix.M[2][0] /= Scale.Z;
			RotationMatrix.M[2][1] /= Scale.Z;
			RotationMatrix.M[2][2] /= Scale.Z;
		}
		RotationMatrix.SetLocation(FVector(0.0f, 0.0f, 0.0f));
		return RotationMatrix.GetEuler();
	}

	FMatrix MakeLocalPoseMatrix(const FVector& Location, const FVector& Rotation, const FVector& Scale)
	{
		return FMatrix::MakeScaleMatrix(Scale) *
			FMatrix::MakeRotationEuler(Rotation) *
			FMatrix::MakeTranslationMatrix(Location);
	}

	TArray<UAnimSequence*> GetAnimSequencesForMesh(UFBXSceneAsset* SceneAsset, USkeletalMesh* PreviewMesh)
	{
		TArray<UAnimSequence*> Result;
		const FSkeletalMesh* MeshAsset = PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr;
		if (!SceneAsset || !MeshAsset)
		{
			return Result;
		}

		for (UAnimSequence* Sequence : SceneAsset->GetAnimSequences())
		{
			if (Sequence && Sequence->GetSkeletonAssetPath() == MeshAsset->SkeletonAssetPath)
			{
				Result.push_back(Sequence);
			}
		}
		return Result;
	}

	FString MakeAnimSequencePath(const FString& FbxPath, int32 SequenceIndex, UAnimSequence* Sequence)
	{
		FString Name = Sequence ? Sequence->GetSequenceName() : FString();
		if (Name.empty())
		{
			Name = "Sequence" + std::to_string(SequenceIndex);
		}
		return FbxPath + "#Anim_" + std::to_string(SequenceIndex) + "_" + Name;
	}

	UAnimSequence* FindAnimSequenceForMeshClip(UFBXSceneAsset* SceneAsset, USkeletalMesh* PreviewMesh, int32 ClipIndex, const FString& FbxPath, FString* OutPath)
	{
		if (!SceneAsset || !PreviewMesh || ClipIndex < 0)
		{
			return nullptr;
		}

		TArray<UAnimSequence*> AnimSequences = GetAnimSequencesForMesh(SceneAsset, PreviewMesh);
		if (ClipIndex >= static_cast<int32>(AnimSequences.size()))
		{
			return nullptr;
		}

		UAnimSequence* Sequence = AnimSequences[ClipIndex];
		if (OutPath)
		{
			*OutPath = MakeAnimSequencePath(FbxPath, ClipIndex, Sequence);
		}
		return Sequence;
	}

}

FSkeletalMeshEditorTab::FSkeletalMeshEditorTab(UEditorEngine* InEditorEngine, int32 InTabId)
	: FSkeletalEditorTab(InEditorEngine, InTabId)
{
}

USkeletalMesh* FSkeletalMeshEditorTab::GetActivePreviewMesh() const
{
	return GetSelectedSkeletalMesh();
}

void FSkeletalMeshEditorTab::RenderLeftPanel()
{
	RenderResourcePanel();
	ImGui::Separator();
	RenderBonePanel();
}

void FSkeletalMeshEditorTab::RenderRightPanel()
{
	RenderTransformPanel();
}

FString FSkeletalMeshEditorTab::GetTabLabel() const
{
	FString Base;
	if (PreviewSkeletalMesh)
	{
		const FSkeletalMesh* Asset = PreviewSkeletalMesh->GetSkeletalMeshAsset();
		if (Asset && !Asset->PathFileName.empty())
		{
			Base = ExtractFileStem(Asset->PathFileName);
		}
	}
	if (Base.empty())
	{
		Base = GetSourcePath().empty() ? FString("SkeletalMesh") : ExtractFileStem(GetSourcePath());
	}
	return Base + "###SkelEditorTab" + std::to_string(GetTabId());
}

bool FSkeletalMeshEditorTab::OpenFbxAsset(const FString& FbxPath)
{
	SetSourcePath(FbxPath);
	CurrentFbxPath = FbxPath;
	CurrentSceneAsset = FMeshManager::LoadFbxScene(FbxPath);
	SelectedResourceIndex = -1;
	SelectedAnimSequenceIndex = 0;
	SelectedBoneIndex = -1;

	if (!CurrentSceneAsset)
	{
		StatusMessage = "Failed to load FBX scene";
		return false;
	}

	const TArray<USkeletalMesh*>& SkeletalMeshes = CurrentSceneAsset->GetSkeletalMeshes();
	if (SkeletalMeshes.empty())
	{
		StatusMessage = "FBX loaded, but no SkeletalMesh was found";
		return false;
	}

	SelectedResourceIndex = 0;
	PreviewSkeletalMesh = GetSelectedSkeletalMesh();
	PreviewScene.SetPreviewMesh(PreviewSkeletalMesh);

	StatusMessage = "FBX loaded";
	return true;
}

void FSkeletalMeshEditorTab::RenderResourcePanel()
{
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;

	if (ImGui::BeginChild("##SkeletalMeshResources", ImVec2(0.0f, 120.0f), false))
	{
		ImGui::TextUnformatted("Resources");
		ImGui::Separator();

		if (!CurrentSceneAsset)
		{
			ImGui::TextDisabled("%s", StatusMessage.c_str());
		}
		else
		{
			const TArray<USkeletalMesh*>& SkeletalMeshes = CurrentSceneAsset->GetSkeletalMeshes();
			if (SkeletalMeshes.empty())
			{
				ImGui::TextDisabled("No SkeletalMesh in this FBX");
			}

			for (int32 MeshIndex = 0; MeshIndex < static_cast<int32>(SkeletalMeshes.size()); ++MeshIndex)
			{
				const USkeletalMesh* Mesh = SkeletalMeshes[MeshIndex];
				const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
				FString Label = "SkeletalMesh " + std::to_string(MeshIndex);
				if (MeshAsset && !MeshAsset->PathFileName.empty())
				{
					Label = MeshAsset->PathFileName;
				}

				const bool bSelected = SelectedResourceIndex == MeshIndex;
				if (ImGui::Selectable(Label.c_str(), bSelected))
				{
					SelectedResourceIndex = MeshIndex;
					SelectedAnimSequenceIndex = 0;
					SelectedBoneIndex = -1;

					if (PreviewViewportClient)
					{
						PreviewViewportClient->GetBoneSelectionManager().ClearSelection();
					}

					PreviewSkeletalMesh = GetSelectedSkeletalMesh();
					PreviewScene.SetPreviewMesh(PreviewSkeletalMesh);
				}
			}
		}
	}
	ImGui::EndChild();
}

void FSkeletalMeshEditorTab::RenderBonePanel()
{
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;

	if (ImGui::BeginChild("##SkeletalMeshBoneHierarchy", ImVec2(0.0f, 0.0f), false))
	{
		RenderAnimationPlaybackPanel();

		ImGui::TextUnformatted("Bone Hierarchy");
		ImGui::Separator();

		if (PreviewViewportClient)
		{
			SelectedBoneIndex = PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone();
		}

		USkeletalMesh* SelectedMesh = GetSelectedSkeletalMesh();
		const FSkeletalMesh* MeshAsset = SelectedMesh ? SelectedMesh->GetSkeletalMeshAsset() : nullptr;
		if (!MeshAsset)
		{
			ImGui::TextDisabled("No SkeletalMesh selected");
		}
		else if (MeshAsset->Bones.empty())
		{
			ImGui::TextDisabled("No bones found");
		}
		else
		{
			const int32 PrevSelectedBoneIndex = SelectedBoneIndex;
			const int32 DoubleClicked = SkeletonTreeUtil::RenderSkeletonTree(
				MeshAsset->Bones,
				SelectedBoneIndex,
				bScrollToSelectedBone,
				RequestSetOpenBoneIndex,
				bRequestSetOpenValue);

			if (DoubleClicked >= 0 && PreviewViewportClient)
			{
				PreviewViewportClient->FocusBone(PreviewMeshComponent, DoubleClicked);
			}

			if (PrevSelectedBoneIndex != SelectedBoneIndex && PreviewViewportClient)
			{
				PreviewViewportClient->GetBoneSelectionManager().SelectBone(SelectedBoneIndex);
			}
		}
	}
	ImGui::EndChild();
}

void FSkeletalMeshEditorTab::RenderAnimationPlaybackPanel()
{
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;

	const FSkeletalMesh* Asset = (PreviewSkeletalMesh && PreviewMeshComponent)
		? PreviewSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
	TArray<UAnimSequence*> AnimSequences = GetAnimSequencesForMesh(CurrentSceneAsset, PreviewSkeletalMesh);

	ImGui::TextUnformatted("Animation");
	ImGui::Separator();

	if (!Asset || AnimSequences.empty())
	{
		ImGui::TextDisabled("No animation sequences.");
		ImGui::Separator();
		return;
	}

	const int32 ClipCount = static_cast<int32>(AnimSequences.size());
	int32 ClipIdx = std::clamp(SelectedAnimSequenceIndex, 0, ClipCount - 1);
	SelectedAnimSequenceIndex = ClipIdx;
	FString CurrentAnimSequencePath;
	UAnimSequence* CurrentSequence = FindAnimSequenceForMeshClip(CurrentSceneAsset, PreviewSkeletalMesh, ClipIdx, CurrentFbxPath, &CurrentAnimSequencePath);
	if (CurrentSequence && PreviewMeshComponent->GetAnimation() != CurrentSequence)
	{
		PreviewMeshComponent->SetAnimation(CurrentSequence);
	}
	const float CurrentDuration = CurrentSequence ? CurrentSequence->GetPlayLength() : 0.0f;
	FString CurrentClipName = CurrentSequence ? CurrentSequence->GetSequenceName() : FString();
	if (CurrentClipName.empty())
	{
		CurrentClipName = "<no sequence>";
	}

	if (ImGui::BeginCombo("Clip", CurrentClipName.c_str()))
	{
		for (int32 i = 0; i < ClipCount; ++i)
		{
			FString AnimSequencePath;
			UAnimSequence* Sequence = FindAnimSequenceForMeshClip(CurrentSceneAsset, PreviewSkeletalMesh, i, CurrentFbxPath, &AnimSequencePath);
			FString SequenceName = Sequence ? Sequence->GetSequenceName() : FString();
			if (SequenceName.empty())
			{
				SequenceName = AnimSequencePath;
			}

			const bool bSelected = (i == ClipIdx);
			if (ImGui::Selectable(SequenceName.c_str(), bSelected))
			{
				SelectedAnimSequenceIndex = i;
				PreviewMeshComponent->SetAnimation(Sequence);
				PreviewMeshComponent->SetBakedAnimTime(0.0f);
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	// 현재 선택된 클립을 AnimSequence Editor 탭으로 점프 (임시 트리거 — UAnimSequence asset 도입 전까지)
	ImGui::SameLine();
	const bool bCanJump = OpenAnimEditorCallback && PreviewSkeletalMesh && CurrentSequence;
	if (!bCanJump) ImGui::BeginDisabled();
	if (ImGui::SmallButton("Edit in Anim Editor"))
	{
		if (CurrentSequence)
		{
			OpenAnimEditorCallback(CurrentAnimSequencePath, PreviewSkeletalMesh, CurrentSequence);
		}
	}
	if (!bCanJump) ImGui::EndDisabled();

	const bool bPaused = PreviewMeshComponent->IsBakedAnimPaused();
	if (ImGui::Button(bPaused ? "Play" : "Pause", ImVec2(70.0f, 0.0f)))
	{
		PreviewMeshComponent->SetBakedAnimPaused(!bPaused);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset", ImVec2(70.0f, 0.0f)))
	{
		PreviewMeshComponent->SetBakedAnimTime(0.0f);
	}

	if (CurrentDuration > 0.0f)
	{
		float Time = std::fmod(PreviewMeshComponent->GetBakedAnimTime(), CurrentDuration);
		if (Time < 0.0f) Time += CurrentDuration;
		if (ImGui::SliderFloat("Time (s)", &Time, 0.0f, CurrentDuration, "%.3f"))
		{
			PreviewMeshComponent->SetBakedAnimTime(Time);
		}
	}

	float Speed = PreviewMeshComponent->GetBakedAnimPlaybackSpeed();
	if (ImGui::SliderFloat("Speed", &Speed, 0.0f, 3.0f, "%.2fx"))
	{
		PreviewMeshComponent->SetBakedAnimPlaybackSpeed(Speed);
	}

	ImGui::Separator();
}

void FSkeletalMeshEditorTab::RenderTransformPanel()
{
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;

	if (ImGui::BeginChild("##SkeletalMeshDetails", ImVec2(0.0f, 0.0f), false))
	{
		ImGui::TextUnformatted("Transform");
		ImGui::Separator();

		if (PreviewViewportClient)
		{
			SelectedBoneIndex = PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone();
		}

		USkeletalMesh* SelectedMesh = GetSelectedSkeletalMesh();
		const FSkeletalMesh* MeshAsset = SelectedMesh ? SelectedMesh->GetSkeletalMeshAsset() : nullptr;
		if (!MeshAsset ||
			!PreviewMeshComponent ||
			SelectedBoneIndex < 0 ||
			SelectedBoneIndex >= static_cast<int32>(MeshAsset->Bones.size()))
		{
			ImGui::TextDisabled("No bone selected");
		}
		else
		{
			const TArray<FMatrix>& LocalPoses = PreviewMeshComponent->GetLocalBonePoseMatrices();
			if (SelectedBoneIndex >= static_cast<int32>(LocalPoses.size()))
			{
				ImGui::TextDisabled("No bone pose available");
				ImGui::EndChild();
				return;
			}

			const FBoneInfo& Bone = MeshAsset->Bones[SelectedBoneIndex];
			const FMatrix& LocalPose = LocalPoses[SelectedBoneIndex];
			const FVector LocationVector = LocalPose.GetLocation();
			const FVector RotationVector = GetRotationEulerNoScale(LocalPose);
			const FVector ScaleVector = LocalPose.GetScale();
			float Location[3] = { LocationVector.X, LocationVector.Y, LocationVector.Z };
			float Rotation[3] = { RotationVector.X, RotationVector.Y, RotationVector.Z };
			float Scale[3] = { ScaleVector.X, ScaleVector.Y, ScaleVector.Z };

			ImGui::TextUnformatted(Bone.Name.c_str());
			ImGui::Separator();
			bool bChanged = false;
			bChanged |= ImGui::DragFloat3("Location", Location, 0.1f, -100000.0f, 100000.0f, "%.3f");
			bChanged |= ImGui::DragFloat3("Rotation", Rotation, 0.1f, -360.0f, 360.0f, "%.3f");
			bChanged |= ImGui::DragFloat3("Scale", Scale, 0.01f, 0.001f, 100.0f, "%.3f");

			if (bChanged)
			{
				FVector NewLocation(Location[0], Location[1], Location[2]);
				FVector NewRotation(Rotation[0], Rotation[1], Rotation[2]);
				FVector NewScale(
					(std::max)(Scale[0], 0.001f),
					(std::max)(Scale[1], 0.001f),
					(std::max)(Scale[2], 0.001f));
				PreviewMeshComponent->SetBoneLocalPose(
					SelectedBoneIndex,
					MakeLocalPoseMatrix(NewLocation, NewRotation, NewScale));
			}
		}
	}
	ImGui::EndChild();
}

int32 FSkeletalMeshEditorTab::GetCurrentClipIndex() const
{
	const USkeletalMeshComponent* Comp = PreviewScene.PreviewMeshComponent;
	(void)Comp;
	return SelectedAnimSequenceIndex;
}

UAnimSequence* FSkeletalMeshEditorTab::GetCurrentAnimSequence(FString* OutPath) const
{
	return FindAnimSequenceForMeshClip(
		CurrentSceneAsset,
		PreviewSkeletalMesh,
		GetCurrentClipIndex(),
		CurrentFbxPath,
		OutPath);
}

USkeletalMesh* FSkeletalMeshEditorTab::GetSelectedSkeletalMesh() const
{
	if (!CurrentSceneAsset)
	{
		return nullptr;
	}

	const TArray<USkeletalMesh*>& SkeletalMeshes = CurrentSceneAsset->GetSkeletalMeshes();
	if (SelectedResourceIndex < 0 || SelectedResourceIndex >= static_cast<int32>(SkeletalMeshes.size()))
	{
		return nullptr;
	}

	return SkeletalMeshes[SelectedResourceIndex];
}
