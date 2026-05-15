#include "Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.h"

#include "Component/SkeletalMeshComponent.h"
#include "Editor/Viewport/SkeletalMeshViewerViewportClient.h"
#include "Mesh/MeshManager.h"
#include "Mesh/FBX/FBXSceneAsset.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletalMeshAsset.h"

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

	void RenderBoneTreeNode(const TArray<FBoneInfo>& Bones, int32 BoneIndex, int32& SelectedBoneIndex, int32& OutDoubleClickedBoneIndex, TArray<int32>& OutVisibleOrder, bool bScrollToSelected, int32 RequestSetOpenBoneIndex, bool bRequestSetOpenValue)
	{
		if (BoneIndex < 0 || BoneIndex >= static_cast<int32>(Bones.size()))
		{
			return;
		}

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow;
		if (SelectedBoneIndex == BoneIndex)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		bool bHasChild = false;
		for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Bones.size()); ++ChildIndex)
		{
			if (Bones[ChildIndex].ParentIndex == BoneIndex)
			{
				bHasChild = true;
				break;
			}
		}
		if (!bHasChild)
		{
			Flags |= ImGuiTreeNodeFlags_Leaf;
		}

		if (BoneIndex == RequestSetOpenBoneIndex)
		{
			ImGui::SetNextItemOpen(bRequestSetOpenValue);
		}

		const bool bOpen = ImGui::TreeNodeEx(
			reinterpret_cast<void*>(static_cast<intptr_t>(BoneIndex)),
			Flags,
			"%s",
			Bones[BoneIndex].Name.c_str());

		OutVisibleOrder.push_back(BoneIndex);
		if (bScrollToSelected && SelectedBoneIndex == BoneIndex)
		{
			ImGui::SetScrollHereY(0.5f);
		}

		if (ImGui::IsItemClicked())
		{
			SelectedBoneIndex = BoneIndex;
		}

		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			OutDoubleClickedBoneIndex = BoneIndex;
		}

		if (bOpen)
		{
			for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(Bones.size()); ++ChildIndex)
			{
				if (Bones[ChildIndex].ParentIndex == BoneIndex)
				{
					RenderBoneTreeNode(Bones, ChildIndex, SelectedBoneIndex, OutDoubleClickedBoneIndex, OutVisibleOrder, bScrollToSelected, RequestSetOpenBoneIndex, bRequestSetOpenValue);
				}
			}
			ImGui::TreePop();
		}
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
	CurrentSceneAsset = FMeshManager::LoadFbxScene(FbxPath);
	SelectedResourceIndex = -1;
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
			int32 PrevSelectedBoneIndex = SelectedBoneIndex;
			int32 DoubleClickedBoneIndex = -1;

			TArray<int32> VisibleOrder;
			VisibleOrder.reserve(MeshAsset->Bones.size());

			for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(MeshAsset->Bones.size()); ++BoneIndex)
			{
				if (MeshAsset->Bones[BoneIndex].ParentIndex < 0)
				{
					RenderBoneTreeNode(MeshAsset->Bones, BoneIndex, SelectedBoneIndex, DoubleClickedBoneIndex, VisibleOrder, bScrollToSelectedBone, RequestSetOpenBoneIndex, bRequestSetOpenValue);
				}
			}
			bScrollToSelectedBone = false;
			RequestSetOpenBoneIndex = -1;

			if (ImGui::IsWindowFocused() && !VisibleOrder.empty())
			{
				const bool bDown  = ImGui::IsKeyPressed(ImGuiKey_DownArrow,  true);
				const bool bUp    = ImGui::IsKeyPressed(ImGuiKey_UpArrow,    true);
				const bool bLeft  = ImGui::IsKeyPressed(ImGuiKey_LeftArrow,  true);
				const bool bRight = ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);

				int32 Cur = -1;
				if (bDown || bUp || bLeft || bRight)
				{
					for (int32 i = 0; i < static_cast<int32>(VisibleOrder.size()); ++i)
					{
						if (VisibleOrder[i] == SelectedBoneIndex)
						{
							Cur = i;
							break;
						}
					}
				}

				if (bDown || bUp)
				{
					int32 Next = Cur;
					if (Cur < 0)
					{
						Next = 0;
					}
					else if (bDown)
					{
						Next = std::min(Cur + 1, static_cast<int32>(VisibleOrder.size()) - 1);
					}
					else
					{
						Next = std::max(Cur - 1, 0);
					}

					if (Next != Cur)
					{
						SelectedBoneIndex     = VisibleOrder[Next];
						bScrollToSelectedBone = true;
					}
				}
				else if ((bLeft || bRight) && Cur >= 0)
				{
					const int32 SelBone = SelectedBoneIndex;

					int32 FirstChild = -1;
					for (int32 j = 0; j < static_cast<int32>(MeshAsset->Bones.size()); ++j)
					{
						if (MeshAsset->Bones[j].ParentIndex == SelBone)
						{
							FirstChild = j;
							break;
						}
					}
					const bool bHasChildren = (FirstChild >= 0);
					const bool bIsOpen      = bHasChildren
					                       && Cur + 1 < static_cast<int32>(VisibleOrder.size())
					                       && MeshAsset->Bones[VisibleOrder[Cur + 1]].ParentIndex == SelBone;

					if (bRight && bHasChildren)
					{
						if (!bIsOpen)
						{
							RequestSetOpenBoneIndex = SelBone;
							bRequestSetOpenValue    = true;
						}
						SelectedBoneIndex     = FirstChild;
						bScrollToSelectedBone = true;
					}
					else if (bLeft)
					{
						if (bHasChildren && bIsOpen)
						{
							RequestSetOpenBoneIndex = SelBone;
							bRequestSetOpenValue    = false;
						}
						else
						{
							const int32 Parent = MeshAsset->Bones[SelBone].ParentIndex;
							if (Parent >= 0)
							{
								SelectedBoneIndex     = Parent;
								bScrollToSelectedBone = true;
							}
						}
					}
				}
			}

			if (DoubleClickedBoneIndex != -1 && PreviewViewportClient)
			{
				PreviewViewportClient->FocusBone(PreviewMeshComponent, DoubleClickedBoneIndex);
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

	ImGui::TextUnformatted("Animation");
	ImGui::Separator();

	if (!Asset || Asset->AnimationClips.empty())
	{
		ImGui::TextDisabled("No baked animation clips.");
		ImGui::Separator();
		return;
	}

	const int32 ClipCount = static_cast<int32>(Asset->AnimationClips.size());
	int32 ClipIdx = std::clamp(PreviewMeshComponent->GetBakedAnimClipIndex(), 0, ClipCount - 1);
	const FAnimationClip& Clip = Asset->AnimationClips[ClipIdx];

	if (ImGui::BeginCombo("Clip", Clip.Name.c_str()))
	{
		for (int32 i = 0; i < ClipCount; ++i)
		{
			const bool bSelected = (i == ClipIdx);
			if (ImGui::Selectable(Asset->AnimationClips[i].Name.c_str(), bSelected))
			{
				PreviewMeshComponent->SetBakedAnimClipIndex(i);
				PreviewMeshComponent->SetBakedAnimTime(0.0f);
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

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

	if (Clip.Duration > 0.0f)
	{
		float Time = std::fmod(PreviewMeshComponent->GetBakedAnimTime(), Clip.Duration);
		if (Time < 0.0f) Time += Clip.Duration;
		if (ImGui::SliderFloat("Time (s)", &Time, 0.0f, Clip.Duration, "%.3f"))
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
