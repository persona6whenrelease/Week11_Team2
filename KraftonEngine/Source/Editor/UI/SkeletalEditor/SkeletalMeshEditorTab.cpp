#include "Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.h"
#include "Editor/UI/SkeletalEditor/SkeletonTreeUtil.h"

#include "Component/SkeletalMeshComponent.h"
#include "Editor/Viewport/SkeletalMeshViewerViewportClient.h"
#include "Editor/UI/EditorFileUtils.h"
#include "Asset/Import/MeshManager.h"
#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"
#include "Platform/Paths.h"
#include "Render/Pipeline/Renderer.h"
#include "Runtime/Engine.h"
#include "WICTextureLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

namespace
{
	enum class EAnimToolIcon : int32
	{
		GoToFront = 0,
		StepBackwards,
		Backwards,
		Play,
		Pause,
		StepForward,
		GoToEnd,
		Loop,
		Recording,
		Count
	};

	const wchar_t* GetAnimToolIconFileName(EAnimToolIcon Icon, bool bOn)
	{
		switch (Icon)
		{
		case EAnimToolIcon::GoToFront:     return bOn ? L"Go_To_Front_24x.png"     : L"Go_To_Front_24x_OFF.png";
		case EAnimToolIcon::StepBackwards: return bOn ? L"Step_Backwards_24x.png"  : L"Step_Backwards_24x_OFF.png";
		case EAnimToolIcon::Backwards:     return bOn ? L"Backwards_24x.png"       : L"Backwards_24x_OFF.png";
		case EAnimToolIcon::Play:          return bOn ? L"Play_24x.png"            : L"Play_24x_OFF.png";
		case EAnimToolIcon::Pause:         return bOn ? L"Pause_24x.png"           : L"Pause_24x_OFF.png";
		case EAnimToolIcon::StepForward:   return bOn ? L"Step_Forward_24x.png"    : L"Step_Forward_24x_OFF.png";
		case EAnimToolIcon::GoToEnd:       return bOn ? L"Go_To_End_24x.png"       : L"Go_To_End_24x_OFF.png";
		case EAnimToolIcon::Loop:          return bOn ? L"Loop_24x.png"            : L"Loop_Toggle_24x.png";
		case EAnimToolIcon::Recording:     return bOn ? L"Recording_24x.png"       : L"Record_24x_OFF.png";
		default: return L"";
		}
	}

	struct FAnimIconSRVs
	{
		ID3D11ShaderResourceView* On = nullptr;
		ID3D11ShaderResourceView* Off = nullptr;
	};

	FAnimIconSRVs* GetAnimIconTable()
	{
		static FAnimIconSRVs Icons[static_cast<int32>(EAnimToolIcon::Count)] = {};
		return Icons;
	}

	bool bAnimIconsLoaded = false;

	void EnsureAnimIconsLoaded()
	{
		if (bAnimIconsLoaded || !GEngine)
		{
			return;
		}

		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		if (!Device)
		{
			return;
		}

		FAnimIconSRVs* Icons = GetAnimIconTable();
		const std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/UEIcons/");
		for (int32 i = 0; i < static_cast<int32>(EAnimToolIcon::Count); ++i)
		{
			const auto LoadOne = [&](bool bOn) -> ID3D11ShaderResourceView*
			{
				ID3D11ShaderResourceView* SRV = nullptr;
				const std::wstring FilePath = IconDir + GetAnimToolIconFileName(static_cast<EAnimToolIcon>(i), bOn);
				DirectX::CreateWICTextureFromFile(Device, FilePath.c_str(), nullptr, &SRV);
				return SRV;
			};
			Icons[i].On = LoadOne(true);
			Icons[i].Off = LoadOne(false);
		}
		bAnimIconsLoaded = true;
	}

	bool DrawAnimIconButton(const char* Id, EAnimToolIcon Icon, bool bActive, const char* FallbackLabel)
	{
		constexpr float IconSize = 24.0f;
		const FAnimIconSRVs& SRVs = GetAnimIconTable()[static_cast<int32>(Icon)];
		ID3D11ShaderResourceView* SRV = bActive ? SRVs.On : SRVs.Off;
		if (!SRV)
		{
			SRV = SRVs.On ? SRVs.On : SRVs.Off;
		}
		if (!SRV)
		{
			return ImGui::Button(FallbackLabel);
		}
		return ImGui::ImageButton(Id, reinterpret_cast<ImTextureID>(SRV), ImVec2(IconSize, IconSize));
	}

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

}

FSkeletalMeshEditorTab::FSkeletalMeshEditorTab(UEditorEngine* InEditorEngine, int32 InTabId)
	: FSkeletalEditorTab(InEditorEngine, InTabId)
{
}

USkeletalMesh* FSkeletalMeshEditorTab::GetActivePreviewMesh() const
{
	return GetSelectedSkeletalMesh();
}

void FSkeletalMeshEditorTab::RenderTabContent(float DeltaTime)
{
	ImGui::PushID(GetTabId());

	RenderTabModeBar();

	if (ImGui::BeginTable(
		"##SkeletalMeshViewerLayout",
		3,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 260.0f);
		ImGui::TableSetupColumn("Center", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 280.0f);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		RenderLeftPanel();

		ImGui::TableSetColumnIndex(1);
		RenderCenterPanel(DeltaTime);

		ImGui::TableSetColumnIndex(2);
		RenderRightPanel();

		ImGui::EndTable();
	}

	ImGui::PopID();
}

void FSkeletalMeshEditorTab::RenderCenterPanel(float DeltaTime)
{
	RenderPreviewAnimationSelector();
	ImGui::Separator();

	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	constexpr float SplitterThickness = 5.0f;
	constexpr float MinViewportHeight = 160.0f;
	constexpr float MinTimelineHeight = 64.0f;
	const float MaxTimelineHeight = std::max(MinTimelineHeight, Avail.y - MinViewportHeight - SplitterThickness);

	TimelinePanelHeight = std::clamp(TimelinePanelHeight, MinTimelineHeight, MaxTimelineHeight);
	const float ViewportHeight = std::max(MinViewportHeight, Avail.y - TimelinePanelHeight - SplitterThickness);

	if (ImGui::BeginChild("##SkeletalMeshViewportArea", ImVec2(0.0f, ViewportHeight), false))
	{
		RenderViewportPanel(DeltaTime);
	}
	ImGui::EndChild();

	ImGui::InvisibleButton("##SkeletalMeshTimelineSplitter", ImVec2(-1.0f, SplitterThickness));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	if (ImGui::IsItemActive())
	{
		TimelinePanelHeight -= ImGui::GetIO().MouseDelta.y;
	}
	{
		const ImVec2 SpMin = ImGui::GetItemRectMin();
		const ImVec2 SpMax = ImGui::GetItemRectMax();
		const ImU32 SpColor = ImGui::IsItemActive() ? IM_COL32(100, 150, 200, 255)
			: (ImGui::IsItemHovered() ? IM_COL32(110, 110, 115, 255) : IM_COL32(60, 60, 65, 255));
		ImGui::GetWindowDrawList()->AddRectFilled(SpMin, SpMax, SpColor);
	}

	if (ImGui::BeginChild("##SkeletalMeshTimelineArea", ImVec2(0.0f, 0.0f), true))
	{
		RenderPreviewAnimationTimeline();
	}
	ImGui::EndChild();
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
	PreviewSkeletalMesh = nullptr;
	CurrentSceneAsset = FMeshManager::LoadFbxScene(FbxPath);
	CurrentSequenceIndex = -1;
	SelectedResourceIndex = -1;
	SelectedBoneIndex = -1;
	bLoadedFromMeshAsset = false;
	AssetAnimSequencePaths.clear();
	AssetAnimSequences.clear();

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
	if (PreviewScene.PreviewMeshComponent)
	{
		PreviewScene.PreviewMeshComponent->SetAnimation(nullptr);
	}

	StatusMessage = "FBX loaded";
	return true;
}

bool FSkeletalMeshEditorTab::OpenSkeletalMeshAsset(const FString& AssetPath)
{
	SetSourcePath(AssetPath);
	CurrentSequenceIndex = -1;
	SelectedResourceIndex = -1;
	SelectedBoneIndex = -1;
	bLoadedFromMeshAsset = true;
	AssetAnimSequencePaths.clear();
	AssetAnimSequences.clear();

	PreviewSkeletalMesh = FMeshManager::LoadSkeletalMeshFromFile(AssetPath);
	if (!PreviewSkeletalMesh)
	{
		CurrentSceneAsset = nullptr;
		CurrentFbxPath.clear();
		StatusMessage = "Failed to load SkeletalMesh asset";
		return false;
	}

	CurrentSceneAsset = FMeshManager::LoadFbxSceneForSkeletalMesh(PreviewSkeletalMesh);
	CurrentFbxPath.clear();
	if (const FSkeletalMesh* MeshAsset = PreviewSkeletalMesh->GetSkeletalMeshAsset())
	{
		if (MeshAsset->SkeletonAssetPath.find('#') != FString::npos)
		{
			CurrentFbxPath = FMeshManager::GetFbxSourcePathFromSubAssetPath(MeshAsset->SkeletonAssetPath);
		}
	}

	PreviewScene.SetPreviewMesh(PreviewSkeletalMesh);
	if (PreviewScene.PreviewMeshComponent)
	{
		PreviewScene.PreviewMeshComponent->SetAnimation(nullptr);
	}
	FMeshManager::FindCompatibleAnimSequenceAssetsForSkeletalMesh(
		PreviewSkeletalMesh,
		AssetPath,
		AssetAnimSequencePaths,
		AssetAnimSequences);

	StatusMessage = CurrentSceneAsset
		? "SkeletalMesh asset loaded"
		: "SkeletalMesh asset loaded without source FBX scene";
	return true;
}

void FSkeletalMeshEditorTab::RenderResourcePanel()
{
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;

	if (ImGui::BeginChild("##SkeletalMeshResources", ImVec2(0.0f, 160.0f), false))
	{
		ImGui::TextUnformatted("Resources");
		ImGui::Separator();

		if (bLoadedFromMeshAsset)
		{
			const FSkeletalMesh* MeshAsset = PreviewSkeletalMesh ? PreviewSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
			FString Label = MeshAsset && !MeshAsset->PathFileName.empty()
				? MeshAsset->PathFileName
				: (GetSourcePath().empty() ? FString("SkeletalMesh") : ExtractFileName(GetSourcePath()));

			if (ImGui::Selectable(Label.c_str(), true))
			{
				PreviewScene.SetPreviewMesh(PreviewSkeletalMesh);
				if (PreviewScene.PreviewMeshComponent)
				{
					PreviewScene.PreviewMeshComponent->SetAnimation(nullptr);
				}
			}
		}
		else if (!CurrentSceneAsset)
		{
			const FString Label = (PreviewSkeletalMesh && PreviewSkeletalMesh->GetSkeletalMeshAsset() &&
				!PreviewSkeletalMesh->GetSkeletalMeshAsset()->PathFileName.empty())
				? PreviewSkeletalMesh->GetSkeletalMeshAsset()->PathFileName
				: FString("SkeletalMesh Asset");
			ImGui::Selectable(Label.c_str(), true);
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
					CurrentSequenceIndex = -1;
					SelectedBoneIndex = -1;

					if (PreviewViewportClient)
					{
						PreviewViewportClient->GetBoneSelectionManager().ClearSelection();
					}

					PreviewSkeletalMesh = GetSelectedSkeletalMesh();
					PreviewScene.SetPreviewMesh(PreviewSkeletalMesh);
					if (PreviewScene.PreviewMeshComponent)
					{
						PreviewScene.PreviewMeshComponent->SetAnimation(nullptr);
					}
				}
			}
		}

		ImGui::Separator();
		USkeletalMesh* SelectedMesh = GetSelectedSkeletalMesh();
		const bool bCanSave = SelectedMesh != nullptr;
		if (!bCanSave) ImGui::BeginDisabled();
		if (ImGui::Button("Save Skeletal Mesh As..."))
		{
			FString DefaultStem;
			const FSkeletalMesh* MeshAsset = SelectedMesh ? SelectedMesh->GetSkeletalMeshAsset() : nullptr;
			if (MeshAsset && !MeshAsset->PathFileName.empty())
			{
				DefaultStem = ExtractFileStem(MeshAsset->PathFileName);
			}
			if (DefaultStem.empty())
			{
				DefaultStem = ExtractFileStem(GetSourcePath());
			}
			if (DefaultStem.empty())
			{
				DefaultStem = "SkeletalMesh";
			}

			const std::wstring DefaultFileNameW = FPaths::ToWide(DefaultStem) + L".asset";
			const std::wstring InitialDirW = FPaths::Combine(FPaths::RootDir(), L"Asset/Content/");

			FEditorFileDialogOptions Options;
			Options.Title             = L"Save Skeletal Mesh As";
			Options.Filter            = L"Asset Files (*.asset)\0*.asset\0All Files (*.*)\0*.*\0";
			Options.DefaultExtension  = L"asset";
			Options.DefaultFileName   = DefaultFileNameW.c_str();
			Options.InitialDirectory  = InitialDirW.c_str();
			Options.bFileMustExist    = false;
			Options.bPathMustExist    = true;
			Options.bPromptOverwrite  = true;
			Options.bReturnRelativeToProjectRoot = true;

			const FString SavePath = FEditorFileUtils::SaveFileDialog(Options);
			if (!SavePath.empty())
			{
				FMeshManager::SaveSkeletalMeshToFile(SelectedMesh, SavePath);
			}
		}
		if (!bCanSave) ImGui::EndDisabled();
	}
	ImGui::EndChild();
}

void FSkeletalMeshEditorTab::RenderBonePanel()
{
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;

	if (ImGui::BeginChild("##SkeletalMeshBoneHierarchy", ImVec2(0.0f, 0.0f), false))
	{
		ImGui::TextUnformatted("Bone Hierarchy");
		ImGui::Separator();

		if (PreviewViewportClient)
		{
			SelectedBoneIndex = PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone();
		}

		USkeletalMesh* SelectedMesh = GetSelectedSkeletalMesh();
		const USkeleton* Skeleton = SelectedMesh ? SelectedMesh->GetSkeleton() : nullptr;
		if (!SelectedMesh)
		{
			ImGui::TextDisabled("No SkeletalMesh selected");
		}
		else if (!Skeleton || Skeleton->GetBones().empty())
		{
			ImGui::TextDisabled("No bones found");
		}
		else
		{
			const TArray<FBoneInfo>& Bones = Skeleton->GetBones();
			
			const int32 PrevSelectedBoneIndex = SelectedBoneIndex;
			const int32 DoubleClicked = SkeletonTreeUtil::RenderSkeletonTree(
				Bones,
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

void FSkeletalMeshEditorTab::RenderPreviewAnimationSelector()
{
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;

	const FSkeletalMesh* Asset = (PreviewSkeletalMesh && PreviewMeshComponent)
		? PreviewSkeletalMesh->GetSkeletalMeshAsset() : nullptr;

	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Preview Animation");
	ImGui::SameLine();

	const bool bUseAssetAnimSequences = bLoadedFromMeshAsset;
	const int32 SequenceCount = bUseAssetAnimSequences
		? static_cast<int32>(AssetAnimSequences.size())
		: FMeshManager::GetAnimSequenceCountForSkeletalMesh(CurrentSceneAsset, PreviewSkeletalMesh);
	if (!Asset || SequenceCount <= 0)
	{
		ImGui::SetNextItemWidth(260.0f);
		ImGui::BeginDisabled();
		const char* NoSequenceLabel = "No compatible animation sequences";
		if (ImGui::BeginCombo("##PreviewAnimation", NoSequenceLabel))
		{
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();
		return;
	}

	int32 SequenceIndex = GetCurrentAnimSequenceIndex();
	if (SequenceIndex >= SequenceCount)
	{
		SequenceIndex = -1;
		CurrentSequenceIndex = -1;
	}
	FString CurrentAnimSequencePath;
	UAnimSequence* CurrentSequence = nullptr;
	if (SequenceIndex >= 0)
	{
		if (bUseAssetAnimSequences)
		{
			CurrentSequence = AssetAnimSequences[SequenceIndex];
			CurrentAnimSequencePath = AssetAnimSequencePaths[SequenceIndex];
		}
		else
		{
			CurrentSequence = FMeshManager::FindAnimSequenceForSkeletalMesh(
				CurrentSceneAsset, PreviewSkeletalMesh, SequenceIndex, &CurrentAnimSequencePath);
		}
	}
	if (CurrentSequence && PreviewMeshComponent->GetAnimation() != CurrentSequence)
	{
		PreviewMeshComponent->SetAnimation(CurrentSequence);
	}
	else if (!CurrentSequence && PreviewMeshComponent->GetAnimation())
	{
		PreviewMeshComponent->SetAnimation(nullptr);
		PreviewMeshComponent->SetBakedAnimTime(0.0f);
		PreviewMeshComponent->SetBakedAnimPaused(true);
	}
    const FString CurrentSequenceName =
        (CurrentSequence && !CurrentSequence->GetSequenceName().empty())
            ? CurrentSequence->GetSequenceName()
            : FString("None");
    const char* CurrentClipName = CurrentSequenceName.c_str();

	ImGui::SetNextItemWidth(260.0f);
	if (ImGui::BeginCombo("##PreviewAnimation", CurrentClipName))
	{
		const bool bNoneSelected = SequenceIndex < 0;
		if (ImGui::Selectable("None", bNoneSelected))
		{
			CurrentSequenceIndex = -1;
			PreviewMeshComponent->SetAnimation(nullptr);
			PreviewMeshComponent->SetBakedAnimTime(0.0f);
			PreviewMeshComponent->SetBakedAnimPaused(true);
		}
		if (bNoneSelected)
		{
			ImGui::SetItemDefaultFocus();
		}
		ImGui::Separator();

		for (int32 i = 0; i < SequenceCount; ++i)
		{
			FString AnimSequencePath;
			UAnimSequence* Sequence = nullptr;
			if (bUseAssetAnimSequences)
			{
				Sequence = AssetAnimSequences[i];
				AnimSequencePath = AssetAnimSequencePaths[i];
			}
			else
			{
				Sequence = FMeshManager::FindAnimSequenceForSkeletalMesh(
					CurrentSceneAsset, PreviewSkeletalMesh, i, &AnimSequencePath);
			}
            const FString SequenceName = (Sequence && !Sequence->GetSequenceName().empty())
                ? Sequence->GetSequenceName()
                : (!AnimSequencePath.empty()
                    ? AnimSequencePath
                    : FString("AnimSequence_") + std::to_string(i));

            const bool bSelected = (i == SequenceIndex);
            if (ImGui::Selectable(SequenceName.c_str(), bSelected))
            {
				CurrentSequenceIndex = i;
                PreviewMeshComponent->SetAnimation(Sequence);
				PreviewMeshComponent->SetBakedAnimTime(0.0f);
				PreviewMeshComponent->SetBakedAnimPaused(true);
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
}

void FSkeletalMeshEditorTab::RenderPreviewAnimationTimeline()
{
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;
	UAnimSequence* CurrentSequence = GetCurrentAnimSequence();
	const UAnimDataModel* DataModel = CurrentSequence ? CurrentSequence->GetDataModel() : nullptr;
	const float Duration = CurrentSequence ? CurrentSequence->GetPlayLength() : 0.0f;
	const float FrameRate = DataModel ? DataModel->GetFrameRate().AsDecimal() : 30.0f;
	const int32 FrameCount = DataModel
		? DataModel->GetNumberOfFrames()
		: ((FrameRate > 0.0f) ? static_cast<int32>(std::round(Duration * FrameRate)) : 0);

	if (!PreviewMeshComponent || !CurrentSequence || Duration <= 0.0f || FrameCount <= 0)
	{
		ImGui::TextDisabled("No preview animation timeline.");
		return;
	}

	EnsureAnimIconsLoaded();

	const bool bPaused = PreviewMeshComponent->IsBakedAnimPaused();
	const float FrameStep = (FrameRate > 0.0f) ? (1.0f / FrameRate) : 0.0f;
	float CurrentTime = std::clamp(PreviewMeshComponent->GetBakedAnimTime(), 0.0f, Duration);

	constexpr float RowHeight = 46.0f;
	constexpr float RulerHeight = 20.0f;
	const float TotalHeight = RowHeight;
	const ImVec2 AvailableSize = ImGui::GetContentRegionAvail();
	ImVec2 Origin = ImGui::GetCursorScreenPos();
	Origin.y += std::max(0.0f, (AvailableSize.y - RowHeight) * 0.5f);
	ImGui::SetCursorScreenPos(Origin);

	const float AvailableWidth = std::max(160.0f, AvailableSize.x);
	const float ControlsWidth = (AvailableWidth >= 560.0f) ? 290.0f : 238.0f;
	const float TotalWidth = std::max(80.0f, AvailableWidth - ControlsWidth);

	ImGui::InvisibleButton("##PreviewTimelineHit", ImVec2(TotalWidth, TotalHeight),
		ImGuiButtonFlags_MouseButtonLeft);
	const bool bHovered = ImGui::IsItemHovered();
	const bool bActive = ImGui::IsItemActive();
	const ImVec2 MousePos = ImGui::GetIO().MousePos;

	auto FrameToTime = [&](int32 Frame)
	{
		return std::clamp(static_cast<float>(Frame) / static_cast<float>(FrameCount) * Duration, 0.0f, Duration);
	};
	auto TimeToX = [&](float Time)
	{
		return Origin.x + std::clamp(Time / Duration, 0.0f, 1.0f) * TotalWidth;
	};
	auto XToFrame = [&](float ScreenX) -> int32
	{
		const float Normalized = std::clamp((ScreenX - Origin.x) / TotalWidth, 0.0f, 1.0f);
		return static_cast<int32>(std::round(Normalized * FrameCount));
	};

	if (bActive && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const int32 SnapFrame = XToFrame(MousePos.x);
		const float SnapTime = FrameToTime(SnapFrame);
		PreviewMeshComponent->SetBakedAnimTime(SnapTime);
		PreviewMeshComponent->SetBakedAnimPaused(true);
		CurrentTime = SnapTime;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 RectMin = Origin;
	const ImVec2 RectMax = ImVec2(Origin.x + TotalWidth, Origin.y + TotalHeight);
	const float TrackY = Origin.y + RulerHeight;

	DrawList->AddRectFilled(RectMin, RectMax, IM_COL32(28, 28, 32, 255));
	DrawList->AddRectFilled(RectMin, ImVec2(RectMax.x, TrackY), IM_COL32(46, 46, 52, 255));
	DrawList->AddRectFilled(ImVec2(Origin.x, TrackY), RectMax, IM_COL32(36, 36, 40, 255));

	int32 TickStride = 1;
	const float PixelsPerFrame = TotalWidth / static_cast<float>(FrameCount);
	if (PixelsPerFrame > 0.0f)
	{
		const float MinPixels = 48.0f;
		const float Needed = MinPixels / PixelsPerFrame;
		const int32 Candidates[] = { 1, 2, 5, 10, 15, 20, 30, 50, 100, 200, 500 };
		TickStride = Candidates[10];
		for (int32 Candidate : Candidates)
		{
			if (static_cast<float>(Candidate) >= Needed)
			{
				TickStride = Candidate;
				break;
			}
		}
	}

	for (int32 Frame = 0; Frame <= FrameCount; Frame += TickStride)
	{
		const float X = TimeToX(FrameToTime(Frame));
		DrawList->AddLine(ImVec2(X, TrackY - 6.0f), ImVec2(X, TrackY), IM_COL32(210, 210, 210, 255));
		DrawList->AddLine(ImVec2(X, TrackY), ImVec2(X, RectMax.y), IM_COL32(70, 70, 75, 75));

		char Label[16];
		std::snprintf(Label, sizeof(Label), "%d", Frame);
		DrawList->AddText(ImVec2(X + 2.0f, Origin.y + 4.0f), IM_COL32(210, 210, 210, 255), Label);
	}

	const float StartX = TimeToX(0.0f);
	const float EndX = TimeToX(Duration);
	const ImU32 StartColor = IM_COL32(60, 220, 90, 255);
	const ImU32 EndColor = IM_COL32(235, 65, 55, 255);
	DrawList->AddLine(ImVec2(StartX, Origin.y), ImVec2(StartX, RectMax.y), StartColor, 2.0f);
	DrawList->AddLine(ImVec2(EndX, Origin.y), ImVec2(EndX, RectMax.y), EndColor, 2.0f);

	char StartLabel[16];
	char EndLabel[16];
	std::snprintf(StartLabel, sizeof(StartLabel), "%d", 0);
	std::snprintf(EndLabel, sizeof(EndLabel), "%d", FrameCount);
	const ImVec2 EndLabelSize = ImGui::CalcTextSize(EndLabel);
	const float BoundaryLabelY = RectMax.y - ImGui::GetTextLineHeight() - 3.0f;
	DrawList->AddText(ImVec2(StartX + 4.0f, BoundaryLabelY), StartColor, StartLabel);
	DrawList->AddText(ImVec2(std::max(Origin.x, EndX - EndLabelSize.x - 4.0f), BoundaryLabelY), EndColor, EndLabel);

	if (bHovered)
	{
		const int32 HoverFrame = XToFrame(MousePos.x);
		const float HoverTime = FrameToTime(HoverFrame);
		const float HoverX = TimeToX(HoverTime);
		DrawList->AddLine(ImVec2(HoverX, TrackY), ImVec2(HoverX, RectMax.y), IM_COL32(255, 255, 255, 70));

		char HoverLabel[48];
		std::snprintf(HoverLabel, sizeof(HoverLabel), "f%d  %.3fs", HoverFrame, HoverTime);
		const ImVec2 HoverLabelSize = ImGui::CalcTextSize(HoverLabel);
		const float LabelX = std::min(HoverX + 6.0f, RectMax.x - HoverLabelSize.x - 4.0f);
		DrawList->AddRectFilled(
			ImVec2(LabelX - 2.0f, TrackY + 3.0f),
			ImVec2(LabelX + HoverLabelSize.x + 2.0f, TrackY + HoverLabelSize.y + 7.0f),
			IM_COL32(0, 0, 0, 180));
		DrawList->AddText(ImVec2(LabelX, TrackY + 4.0f),
			IM_COL32(255, 255, 255, 230), HoverLabel);
	}

	const float ScrubX = TimeToX(CurrentTime);
	const ImU32 ScrubColor = IM_COL32(255, 130, 30, 255);
	DrawList->AddLine(ImVec2(ScrubX, Origin.y), ImVec2(ScrubX, RectMax.y), ScrubColor, 2.0f);
	DrawList->AddTriangleFilled(
		ImVec2(ScrubX - 6.0f, Origin.y),
		ImVec2(ScrubX + 6.0f, Origin.y),
		ImVec2(ScrubX, Origin.y + 9.0f),
		ScrubColor);
	DrawList->AddRect(RectMin, RectMax, IM_COL32(70, 70, 75, 255));

	ImGui::SameLine(0.0f, 10.0f);
	ImVec2 ControlsPos = ImGui::GetCursorScreenPos();
	ControlsPos.y = Origin.y + (RowHeight - 24.0f) * 0.5f;
	ImGui::SetCursorScreenPos(ControlsPos);

	constexpr float ButtonSpacing = 1.0f;
	constexpr float GroupSpacing = 8.0f;
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));
	
	ImGui::SameLine(0.0f, ButtonSpacing);
	
	
	if (DrawAnimIconButton("##PreviewAnimGoToFront", EAnimToolIcon::GoToFront, true, "|<"))
	{
		PreviewMeshComponent->SetBakedAnimTime(0.0f);
		PreviewMeshComponent->SetBakedAnimPaused(true);
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	if (DrawAnimIconButton("##PreviewAnimStepBack", EAnimToolIcon::StepBackwards, true, "<|"))
	{
		if (FrameStep > 0.0f)
		{
			PreviewMeshComponent->SetBakedAnimTime(std::max(0.0f, CurrentTime - FrameStep));
			PreviewMeshComponent->SetBakedAnimPaused(true);
		}
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	const float CurrentSpeed = PreviewMeshComponent->GetBakedAnimPlaybackSpeed();
	const bool bReversePlaying = !bPaused && CurrentSpeed < 0.0f;
	if (DrawAnimIconButton("##PreviewAnimBackwards", bReversePlaying ? EAnimToolIcon::Pause : EAnimToolIcon::Backwards, true, bReversePlaying ? "||" : "<<"))
	{
		if (bReversePlaying)
		{
			PreviewMeshComponent->SetBakedAnimPaused(true);
		}
		else
		{
			PreviewMeshComponent->SetBakedAnimPlaybackSpeed(-std::max(0.01f, std::abs(CurrentSpeed)));
			PreviewMeshComponent->SetBakedAnimPaused(false);
		}
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	if (DrawAnimIconButton("##PreviewAnimRecording", EAnimToolIcon::Recording, bPreviewRecording, bPreviewRecording ? "Rec" : "RecOff"))
	{
		bPreviewRecording = !bPreviewRecording;
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	const bool bForwardPlaying = !bPaused && CurrentSpeed >= 0.0f;
	if (DrawAnimIconButton("##PreviewAnimPlayPause", bForwardPlaying ? EAnimToolIcon::Pause : EAnimToolIcon::Play, true, bForwardPlaying ? "Pause" : "Play"))
	{
		if (bForwardPlaying)
		{
			PreviewMeshComponent->SetBakedAnimPaused(true);
		}
		else
		{
			PreviewMeshComponent->SetBakedAnimPlaybackSpeed(std::max(0.01f, std::abs(CurrentSpeed)));
			PreviewMeshComponent->SetBakedAnimPaused(false);
		}
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	if (DrawAnimIconButton("##PreviewAnimStepForward", EAnimToolIcon::StepForward, true, "|>"))
	{
		if (FrameStep > 0.0f)
		{
			PreviewMeshComponent->SetBakedAnimTime(std::min(Duration, CurrentTime + FrameStep));
			PreviewMeshComponent->SetBakedAnimPaused(true);
		}
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	if (DrawAnimIconButton("##PreviewAnimGoToEnd", EAnimToolIcon::GoToEnd, true, ">|"))
	{
		PreviewMeshComponent->SetBakedAnimTime(Duration);
		PreviewMeshComponent->SetBakedAnimPaused(true);
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	if (DrawAnimIconButton("##PreviewAnimLoop", EAnimToolIcon::Loop, bPreviewLooping, bPreviewLooping ? "LoopOn" : "LoopOff"))
	{
		bPreviewLooping = !bPreviewLooping;
		PreviewMeshComponent->Play(bPreviewLooping);
		PreviewMeshComponent->SetBakedAnimPaused(true);
	}

	ImGui::PopStyleColor(3);

	ImGui::SameLine(0.0f, GroupSpacing);
	const float AbsSpeed = std::max(0.0f, std::abs(PreviewMeshComponent->GetBakedAnimPlaybackSpeed()));
	char SpeedLabel[16];
	std::snprintf(SpeedLabel, sizeof(SpeedLabel), "x%.1f", AbsSpeed);
	ImGui::SetNextItemWidth(64.0f);
	if (ImGui::BeginCombo("##PreviewAnimSpeed", SpeedLabel, ImGuiComboFlags_NoArrowButton))
	{
		const float SpeedOptions[] = { 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f };
		for (float Option : SpeedOptions)
		{
			char OptionLabel[16];
			std::snprintf(OptionLabel, sizeof(OptionLabel), "x%.2g", Option);
			const bool bSelected = std::abs(AbsSpeed - Option) < 0.001f;
			if (ImGui::Selectable(OptionLabel, bSelected))
			{
				const float Direction = PreviewMeshComponent->GetBakedAnimPlaybackSpeed() < 0.0f ? -1.0f : 1.0f;
				PreviewMeshComponent->SetBakedAnimPlaybackSpeed(Option * Direction);
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
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
			
			// === Override 마스크 컨트롤 ===
			ImGui::Separator();
			const bool bThisBoneOverridden = PreviewMeshComponent->IsBoneOverridden(SelectedBoneIndex);
			if (bThisBoneOverridden)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "* User Modified");
			}
			else
			{
				ImGui::TextDisabled("(Follows animation)");
			}

			if (ImGui::Button("Reset This Bone", ImVec2(140.0f, 0.0f)))
			{
				PreviewMeshComponent->ClearBoneOverride(SelectedBoneIndex);
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset All Bones", ImVec2(140.0f, 0.0f)))
			{
				PreviewMeshComponent->ClearAllBoneOverrides();
			}
		}
	}
	ImGui::EndChild();
}

int32 FSkeletalMeshEditorTab::GetCurrentAnimSequenceIndex() const
{
	const USkeletalMeshComponent* Comp = PreviewScene.PreviewMeshComponent;
	if (!Comp)
	{
		return CurrentSequenceIndex;
	}

	const UAnimSequence* CurrentSequence = Cast<UAnimSequence>(Comp->GetAnimation());
	if (CurrentSequence && bLoadedFromMeshAsset)
	{
		for (int32 SequenceIndex = 0; SequenceIndex < static_cast<int32>(AssetAnimSequences.size()); ++SequenceIndex)
		{
			if (AssetAnimSequences[SequenceIndex] == CurrentSequence)
			{
				return SequenceIndex;
			}
		}
	}
	if (CurrentSequence && CurrentSceneAsset && PreviewSkeletalMesh)
	{
		FString CurrentAnimSequencePath;
		for (int32 SequenceIndex = 0;; ++SequenceIndex)
		{
			UAnimSequence* Sequence = FMeshManager::FindAnimSequenceForSkeletalMesh(
				CurrentSceneAsset, PreviewSkeletalMesh, SequenceIndex, &CurrentAnimSequencePath);
			if (!Sequence)
			{
				break;
			}
			if (Sequence == CurrentSequence)
			{
				return SequenceIndex;
			}
		}
	}

	return CurrentSequenceIndex;
}

UAnimSequence* FSkeletalMeshEditorTab::GetCurrentAnimSequence(FString* OutPath) const
{
	const int32 SequenceIndex = GetCurrentAnimSequenceIndex();
	if (SequenceIndex < 0)
	{
		if (OutPath)
		{
			*OutPath = FString();
		}
		return nullptr;
	}

	if (bLoadedFromMeshAsset)
	{
		if (SequenceIndex >= static_cast<int32>(AssetAnimSequences.size()))
		{
			if (OutPath)
			{
				*OutPath = FString();
			}
			return nullptr;
		}
		if (OutPath)
		{
			*OutPath = AssetAnimSequencePaths[SequenceIndex];
		}
		return AssetAnimSequences[SequenceIndex];
	}

	return FMeshManager::FindAnimSequenceForSkeletalMesh(
		CurrentSceneAsset,
		PreviewSkeletalMesh,
		SequenceIndex,
		OutPath);
}

USkeletalMesh* FSkeletalMeshEditorTab::GetSelectedSkeletalMesh() const
{
	if (bLoadedFromMeshAsset)
	{
		return PreviewSkeletalMesh;
	}

	if (!CurrentSceneAsset)
	{
		return PreviewSkeletalMesh;
	}

	const TArray<USkeletalMesh*>& SkeletalMeshes = CurrentSceneAsset->GetSkeletalMeshes();
	if (SelectedResourceIndex < 0 || SelectedResourceIndex >= static_cast<int32>(SkeletalMeshes.size()))
	{
		return nullptr;
	}

	return SkeletalMeshes[SelectedResourceIndex];
}
