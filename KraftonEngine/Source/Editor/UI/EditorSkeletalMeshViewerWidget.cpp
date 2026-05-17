#include "Editor/UI/EditorSkeletalMeshViewerWidget.h"

#include "Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.h"
#include "Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h"
#include "Editor/Settings/EditorSettings.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "ImGui/imgui.h"

FEditorSkeletalMeshViewerWidget::~FEditorSkeletalMeshViewerWidget()
{
	Tabs.clear();
}

FSkeletalEditorTab* FEditorSkeletalMeshViewerWidget::FindTabBySource(const FString& Path) const
{
	for (const auto& Tab : Tabs)
	{
		if (Tab && Tab->GetSourcePath() == Path)
		{
			return Tab.get();
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
		return;
	}

	if (ActiveTabIndex >= static_cast<int32>(Tabs.size()))
	{
		ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	}
}

bool FEditorSkeletalMeshViewerWidget::OpenFbxAsset(const FString& FbxPath)
{
	if (FSkeletalEditorTab* Existing = FindTabBySource(FbxPath))
	{
		RequestedFocusTabId = Existing->GetTabId();
		for (int32 Index = 0; Index < static_cast<int32>(Tabs.size()); ++Index)
		{
			if (Tabs[Index].get() == Existing)
			{
				ActiveTabIndex = Index;
				break;
			}
		}
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

	if (!NewTab->OpenFbxAsset(FbxPath))
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
		RequestedFocusTabId = Existing->GetTabId();
		for (int32 Index = 0; Index < static_cast<int32>(Tabs.size()); ++Index)
		{
			if (Tabs[Index].get() == Existing)
			{
				ActiveTabIndex = Index;
				break;
			}
		}
		return true;
	}

	auto NewTab = std::make_unique<FAnimSequenceEditorTab>(EditorEngine, NextTabId++);
	FAnimSequenceEditorTab* RawTab = NewTab.get();
	NewTab->SetOnSwitchToSkeletalMesh(
		[this, RawTab]()
		{
			const FString& Path = RawTab->GetFbxPath();
			if (!Path.empty())
			{
				OpenFbxAsset(Path);
			}
		});
	NewTab->SetOnSwitchToAnimSequence(nullptr);

	if (!NewTab->OpenAnimSequenceAsset(AssetPath))
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
		RequestedFocusTabId = Existing->GetTabId();
		for (int32 Index = 0; Index < static_cast<int32>(Tabs.size()); ++Index)
		{
			if (Tabs[Index].get() == Existing)
			{
				ActiveTabIndex = Index;
				break;
			}
		}
		return true;
	}

	auto NewTab = std::make_unique<FAnimSequenceEditorTab>(EditorEngine, NextTabId++);
	FAnimSequenceEditorTab* RawTab = NewTab.get();
	NewTab->SetOnSwitchToSkeletalMesh(
		[this, RawTab]()
		{
			const FString& Path = RawTab->GetFbxPath();
			if (!Path.empty())
			{
				OpenFbxAsset(Path);
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
	const int32 FocusToApplyThisFrame = RequestedFocusTabId;

	if (ImGui::BeginTabBar("##SkeletalEditorTabBar", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
	{
		for (int32 Index = 0; Index < static_cast<int32>(Tabs.size()); ++Index)
		{
			FSkeletalEditorTab* Tab = Tabs[Index].get();
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
			const FString Label = Tab->GetTabLabel();
			if (ImGui::BeginTabItem(Label.c_str(), &bTabOpen, TabFlags))
			{
				ActiveTabIndex = Index;
				Tab->RenderTabContent(DeltaTime);
				ImGui::EndTabItem();
			}

			if (!bTabOpen)
			{
				TabIndexToClose = Index;
			}
		}
		ImGui::EndTabBar();
	}

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
