#include "Editor/UI/EditorSkeletalMeshViewerWidget.h"

#include "Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.h"
#include "Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h"
#include "Editor/Settings/EditorSettings.h"
#include "Asset/Import/MeshManager.h"
#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"

#include <algorithm>
#include <cctype>

namespace
{
	FString NormalizeEditorTabPath(FString Path)
	{
		std::replace(Path.begin(), Path.end(), '\\', '/');
		std::transform(Path.begin(), Path.end(), Path.begin(),
			[](unsigned char Ch)
			{
				return static_cast<char>(std::tolower(Ch));
			});
		return Path;
	}

	bool ResolveAnimSequencePreviewContext(
		const FString& AssetPath,
		USkeletalMesh*& OutPreviewMesh,
		UAnimSequence*& OutSequence)
	{
		OutPreviewMesh = nullptr;
		OutSequence = FMeshManager::ResolveAnimSequenceReference(AssetPath);
		if (!OutSequence)
		{
			return false;
		}

		if (UFBXSceneAsset* SceneAsset = OutSequence->GetTypedOuter<UFBXSceneAsset>())
		{
			OutPreviewMesh = FMeshManager::FindSkeletalMeshForAnimSequence(SceneAsset, OutSequence);
		}
		if (!OutPreviewMesh)
		{
			OutPreviewMesh = FMeshManager::FindPreviewMeshForAnimSequence(OutSequence, AssetPath);
		}

		return OutSequence != nullptr;
	}

	FString MakeTabLabelWithIconSpace(const FString& Label)
	{
		constexpr const char* IconTextPadding = "     ";
		const size_t IdMarkerPos = Label.find("###");
		if (IdMarkerPos == FString::npos)
		{
			return FString(IconTextPadding) + Label;
		}

		return FString(IconTextPadding) + Label.substr(0, IdMarkerPos) + Label.substr(IdMarkerPos);
	}

	void DrawTabKindIcon(const FSkeletalEditorTab* Tab)
	{
		if (!Tab)
		{
			return;
		}

		ImTextureID IconTexture = GetSkeletalEditorModeBarIconTexture(
			GetSkeletalEditorModeBarIconForKind(Tab->GetKind()));
		if (!IconTexture)
		{
			return;
		}

		constexpr float IconSize = 14.0f;
		const ImVec2 TabMin = ImGui::GetItemRectMin();
		const ImVec2 TabMax = ImGui::GetItemRectMax();
		const ImVec2 IconMin(
			TabMin.x + 8.0f,
			TabMin.y + std::max(0.0f, (TabMax.y - TabMin.y - IconSize) * 0.5f));
		const ImVec2 IconMax(IconMin.x + IconSize, IconMin.y + IconSize);
		ImGui::GetWindowDrawList()->AddImage(IconTexture, IconMin, IconMax);
	}
}

FEditorSkeletalMeshViewerWidget::~FEditorSkeletalMeshViewerWidget()
{
	Tabs.clear();
}

FSkeletalEditorTab* FEditorSkeletalMeshViewerWidget::FindTabBySource(const FString& Path) const
{
	const FString NormalizedPath = NormalizeEditorTabPath(Path);
	for (const auto& Tab : Tabs)
	{
		if (Tab && NormalizeEditorTabPath(Tab->GetSourcePath()) == NormalizedPath)
		{
			return Tab.get();
		} 
	}
	return nullptr;
}

FSkeletalMeshEditorTab* FEditorSkeletalMeshViewerWidget::FindSkeletalMeshTabByMesh(USkeletalMesh* Mesh) const
{
	if (!Mesh)
	{
		return nullptr;
	}

	for (const auto& Tab : Tabs)
	{
		if (!Tab || Tab->GetKind() != ESkeletalEditorTabKind::SkeletalMesh)
		{
			continue;
		}

		auto* MeshTab = static_cast<FSkeletalMeshEditorTab*>(Tab.get());
		if (MeshTab->GetCurrentPreviewMesh() == Mesh)
		{
			return MeshTab;
		}
	}

	return nullptr;
}

FAnimSequenceEditorTab* FEditorSkeletalMeshViewerWidget::FindAnimSequenceTabByMesh(USkeletalMesh* Mesh) const
{
	if (!Mesh)
	{
		return nullptr;
	}

	for (const auto& Tab : Tabs)
	{
		if (!Tab || Tab->GetKind() != ESkeletalEditorTabKind::AnimSequence)
		{
			continue;
		}

		auto* AnimTab = static_cast<FAnimSequenceEditorTab*>(Tab.get());
		if (AnimTab->GetCurrentPreviewMesh() == Mesh)
		{
			return AnimTab;
		}
	}

	return nullptr;
}

FSkeletalEditorTab* FEditorSkeletalMeshViewerWidget::GetActiveTab() const
{
	if (ActiveTabIndex < 0 || ActiveTabIndex >= static_cast<int32>(Tabs.size()))
	{
		return nullptr;
	}
	return Tabs[ActiveTabIndex].get();
}

void FEditorSkeletalMeshViewerWidget::FocusTab(FSkeletalEditorTab* Tab)
{
	if (!Tab)
	{
		return;
	}

	RequestedFocusTabId = Tab->GetTabId();
	for (int32 i = 0; i < static_cast<int32>(Tabs.size()); ++i)
	{
		if (Tabs[i].get() == Tab)
		{
			ActiveTabIndex = i;
			break;
		}
	}
}

void FEditorSkeletalMeshViewerWidget::RequestCloseTab(int32 Index)
{
	if (Index < 0 || Index >= static_cast<int32>(Tabs.size()))
	{
		return;
	}
	Tabs.erase(Tabs.begin() + Index);
	if (Tabs.empty())
	{
		ActiveTabIndex = -1;
	}
	else if (ActiveTabIndex >= static_cast<int32>(Tabs.size()))
	{
		ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	}
}

bool FEditorSkeletalMeshViewerWidget::OpenFbxAsset(const FString& FbxPath)
{
	if (FSkeletalEditorTab* Existing = FindTabBySource(FbxPath))
	{
		FocusTab(Existing);
		return true;
	}

	auto NewTab = std::make_unique<FSkeletalMeshEditorTab>(EditorEngine, NextTabId++);
	FSkeletalMeshEditorTab* RawTab = NewTab.get();
	NewTab->SetOpenAnimEditorCallback(
		[this](const FString& Path, USkeletalMesh* Mesh, UAnimSequence* Sequence)
		{
			OpenAnimSequenceAsset(Path, Mesh, Sequence);
		});
	// Mode bar: 이미 SkeletalMesh 모드라 좌측 버튼은 그대로 활성 표시(no-op),
	// 우측은 현재 선택된 클립으로 AnimSequence 탭 열기.
	NewTab->SetOnSwitchToSkeletalMesh(nullptr);
	NewTab->SetOnSwitchToAnimSequence(
		[this, RawTab]()
		{
			USkeletalMesh* Mesh = RawTab->GetCurrentPreviewMesh();
			FString AnimSequencePath;
			UAnimSequence* Sequence = RawTab->GetCurrentAnimSequence(&AnimSequencePath);
			if (Mesh && Sequence && !AnimSequencePath.empty())
			{
				OpenAnimSequenceAsset(AnimSequencePath, Mesh, Sequence);
			}
		});
	const bool bOk = NewTab->OpenFbxAsset(FbxPath);
	if (!bOk)
	{
		return false;
	}

	if (FSkeletalMeshEditorTab* ExistingMeshTab = FindSkeletalMeshTabByMesh(NewTab->GetCurrentPreviewMesh()))
	{
		FocusTab(ExistingMeshTab);
		return true;
	}

	RequestedFocusTabId = NewTab->GetTabId();
	Tabs.emplace_back(std::move(NewTab));
	ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	return true;
}

bool FEditorSkeletalMeshViewerWidget::OpenSkeletalMeshAsset(const FString& AssetPath)
{
	EAssetType AssetType = EAssetType::Unknown;
	if (!TryReadAssetType(AssetPath, AssetType))
	{
		return OpenFbxAsset(AssetPath);
	}

	if (AssetType == EAssetType::AnimSequence)
	{
		return OpenAnimSequenceAsset(AssetPath);
	}
	if (AssetType != EAssetType::SkeletalMesh)
	{
		return false;
	}

	if (FSkeletalEditorTab* Existing = FindTabBySource(AssetPath))
	{
		FocusTab(Existing);
		return true;
	}

	auto NewTab = std::make_unique<FSkeletalMeshEditorTab>(EditorEngine, NextTabId++);
	FSkeletalMeshEditorTab* RawTab = NewTab.get();
	NewTab->SetOpenAnimEditorCallback(
		[this](const FString& Path, USkeletalMesh* Mesh, UAnimSequence* Sequence)
		{
			OpenAnimSequenceAsset(Path, Mesh, Sequence);
		});
	NewTab->SetOnSwitchToSkeletalMesh(nullptr);
	NewTab->SetOnSwitchToAnimSequence(
		[this, RawTab]()
		{
			USkeletalMesh* Mesh = RawTab->GetCurrentPreviewMesh();
			FString AnimSequencePath;
			UAnimSequence* Sequence = RawTab->GetCurrentAnimSequence(&AnimSequencePath);
			if (Mesh && Sequence && !AnimSequencePath.empty())
			{
				OpenAnimSequenceAsset(AnimSequencePath, Mesh, Sequence);
			}
		});

	const bool bOk = NewTab->OpenSkeletalMeshAsset(AssetPath);
	if (!bOk)
	{
		return false;
	}

	RequestedFocusTabId = NewTab->GetTabId();
	Tabs.emplace_back(std::move(NewTab));
	ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	return true;
}

bool FEditorSkeletalMeshViewerWidget::OpenAnimSequenceAsset(const FString& AssetPath)
{
	if (FSkeletalEditorTab* Existing = FindTabBySource(AssetPath))
	{
		FocusTab(Existing);
		return true;
	}

	USkeletalMesh* ResolvedPreviewMesh = nullptr;
	UAnimSequence* ResolvedSequence = nullptr;
	if (ResolveAnimSequencePreviewContext(AssetPath, ResolvedPreviewMesh, ResolvedSequence))
	{
		if (FAnimSequenceEditorTab* ExistingAnimTab = FindAnimSequenceTabByMesh(ResolvedPreviewMesh))
		{
			if (ExistingAnimTab->OpenAnimSequenceAsset(AssetPath, ResolvedPreviewMesh, ResolvedSequence))
			{
				FocusTab(ExistingAnimTab);
				return true;
			}
		}
	}

	auto NewTab = std::make_unique<FAnimSequenceEditorTab>(EditorEngine, NextTabId++);
	FAnimSequenceEditorTab* RawTab = NewTab.get();
	NewTab->SetOnSwitchToSkeletalMesh(
		[this, RawTab]()
		{
			const FString& Path = RawTab->GetFbxPath();
			if (!Path.empty() && OpenFbxAsset(Path))
			{
				return;
			}

			const FString MeshAssetPath =
				FMeshManager::GetLoadedSkeletalMeshAssetPath(RawTab->GetCurrentPreviewMesh());
			if (!MeshAssetPath.empty())
			{
				OpenSkeletalMeshAsset(MeshAssetPath);
			}
		});
	NewTab->SetOnSwitchToAnimSequence(nullptr);
	const bool bOpened = (ResolvedPreviewMesh && ResolvedSequence)
		? NewTab->OpenAnimSequenceAsset(AssetPath, ResolvedPreviewMesh, ResolvedSequence)
		: NewTab->OpenAnimSequenceAsset(AssetPath);
	if (!bOpened)
	{
		return false;
	}
	RequestedFocusTabId = NewTab->GetTabId();
	Tabs.emplace_back(std::move(NewTab));
	ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	return true;
}

bool FEditorSkeletalMeshViewerWidget::OpenAnimSequenceAsset(const FString& AssetPath, USkeletalMesh* PreviewMesh, UAnimSequence* Sequence)
{
	if (!PreviewMesh || !Sequence)
	{
		return false;
	}

	if (FSkeletalEditorTab* Existing = FindTabBySource(AssetPath))
	{
		FocusTab(Existing);
		return true;
	}

	if (FAnimSequenceEditorTab* ExistingAnimTab = FindAnimSequenceTabByMesh(PreviewMesh))
	{
		if (ExistingAnimTab->OpenAnimSequenceAsset(AssetPath, PreviewMesh, Sequence))
		{
			FocusTab(ExistingAnimTab);
			return true;
		}
	}

	auto NewTab = std::make_unique<FAnimSequenceEditorTab>(EditorEngine, NextTabId++);
	FAnimSequenceEditorTab* RawTab = NewTab.get();
	NewTab->SetOnSwitchToSkeletalMesh(
		[this, RawTab]()
		{
			const FString& Path = RawTab->GetFbxPath();
			if (!Path.empty() && OpenFbxAsset(Path))
			{
				return;
			}

			const FString MeshAssetPath =
				FMeshManager::GetLoadedSkeletalMeshAssetPath(RawTab->GetCurrentPreviewMesh());
			if (!MeshAssetPath.empty())
			{
				OpenSkeletalMeshAsset(MeshAssetPath);
			}
		});
	NewTab->SetOnSwitchToAnimSequence(nullptr);
	if (!NewTab->OpenAnimSequenceAsset(AssetPath, PreviewMesh, Sequence))
	{
		return false;
	}

	RequestedFocusTabId = NewTab->GetTabId();
	Tabs.emplace_back(std::move(NewTab));
	ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	return true;
}

bool FEditorSkeletalMeshViewerWidget::WantsMouseCapture() const
{
	const FSkeletalEditorTab* Active = GetActiveTab();
	return Active && Active->WantsMouseCapture();
}

bool FEditorSkeletalMeshViewerWidget::WantsKeyboardCapture() const
{
	const FSkeletalEditorTab* Active = GetActiveTab();
	return Active && Active->WantsKeyboardCapture();
}

void FEditorSkeletalMeshViewerWidget::UpdateInput(float DeltaTime)
{
	if (FSkeletalEditorTab* Active = GetActiveTab())
	{
		Active->UpdateInput(DeltaTime);
	}
}

void FEditorSkeletalMeshViewerWidget::Render(float DeltaTime)
{
	FEditorSettings& Settings = FEditorSettings::Get();
	ImGuiWindowClass ViewerWindowClass;
	ViewerWindowClass.ParentViewportId = 0;
	ViewerWindowClass.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoTaskBarIcon;
	ImGui::SetNextWindowClass(&ViewerWindowClass);
	ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Skeletal Editor", &Settings.UI.bSkeletalMeshViewer, ImGuiWindowFlags_MenuBar))
	{
		ImGui::End();
		return;
	}

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Asset"))
		{
			ImGui::MenuItem("Open SkeletalMesh...", nullptr, false, false);
			ImGui::MenuItem("Open AnimSequence...", nullptr, false, false);
			ImGui::Separator();
			if (ImGui::MenuItem("Close Current Tab", nullptr, false, ActiveTabIndex >= 0))
			{
				RequestCloseTab(ActiveTabIndex);
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	if (Tabs.empty())
	{
		ImGui::TextDisabled("No asset opened. Double-click a SkeletalMesh / AnimSequence in ContentBrowser.");
		ImGui::End();
		return;
	}

	int32 TabIndexToClose = -1;

	// 이번 프레임에 적용할 포커스 요청을 미리 캡처.
	// 렌더 중 콜백이 RequestedFocusTabId를 새로 set할 수 있으므로 분리해야 한다.
	const int32 FocusToApplyThisFrame = RequestedFocusTabId;

	if (ImGui::BeginTabBar("##SkeletalEditorTabBar", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
	{
		for (int32 i = 0; i < static_cast<int32>(Tabs.size()); ++i)
		{
			FSkeletalEditorTab* Tab = Tabs[i].get();
			if (!Tab)
			{
				continue;
			}

			ImGuiTabItemFlags TabFlags = ImGuiTabItemFlags_None;
			if (FocusToApplyThisFrame >= 0 && FocusToApplyThisFrame == Tab->GetTabId())
			{
				TabFlags |= ImGuiTabItemFlags_SetSelected;
			}

			bool bTabOpen = true;
			const FString Label = MakeTabLabelWithIconSpace(Tab->GetTabLabel());
			const bool bSelected = ImGui::BeginTabItem(Label.c_str(), &bTabOpen, TabFlags);
			DrawTabKindIcon(Tab);
			if (bSelected)
			{
				ActiveTabIndex = i;
				Tab->RenderTabContent(DeltaTime);
				ImGui::EndTabItem();
			}

			if (!bTabOpen)
			{
				TabIndexToClose = i;
			}
		}
		ImGui::EndTabBar();
	}

	// 적용된 요청만 클리어. 렌더 중 콜백이 새로 set한 값은 다음 프레임을 위해 유지.
	if (RequestedFocusTabId == FocusToApplyThisFrame)
	{
		RequestedFocusTabId = -1;
	}

	if (TabIndexToClose >= 0)
	{
		RequestCloseTab(TabIndexToClose);
	}

	ImGui::End();
}
