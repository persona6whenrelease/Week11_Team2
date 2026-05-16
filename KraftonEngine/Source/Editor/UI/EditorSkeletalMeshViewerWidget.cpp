#include "Editor/UI/EditorSkeletalMeshViewerWidget.h"

#include "Editor/UI/SkeletalEditor/SkeletalMeshEditorTab.h"
#include "Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h"
#include "Editor/Settings/EditorSettings.h"
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
		RequestedFocusTabId = Existing->GetTabId();
		for (int32 i = 0; i < static_cast<int32>(Tabs.size()); ++i)
		{
			if (Tabs[i].get() == Existing)
			{
				ActiveTabIndex = i;
				break;
			}
		}
		return true;
	}

	auto NewTab = std::make_unique<FSkeletalMeshEditorTab>(EditorEngine, NextTabId++);
	FSkeletalMeshEditorTab* RawTab = NewTab.get();
	NewTab->SetOpenAnimEditorCallback(
		[this](const FString& Path, USkeletalMesh* Mesh, int32 ClipIdx)
		{
			OpenAnimSequenceFromFbxClip(Path, Mesh, ClipIdx);
		});
	// Mode bar: 이미 SkeletalMesh 모드라 좌측 버튼은 그대로 활성 표시(no-op),
	// 우측은 현재 선택된 클립으로 AnimSequence 탭 열기.
	NewTab->SetOnSwitchToSkeletalMesh(nullptr);
	NewTab->SetOnSwitchToAnimSequence(
		[this, RawTab]()
		{
			USkeletalMesh* Mesh = RawTab->GetCurrentPreviewMesh();
			const FString& Path = RawTab->GetCurrentFbxPath();
			if (Mesh && !Path.empty())
			{
				const int32 ClipIdx = RawTab->GetCurrentClipIndex();
				OpenAnimSequenceFromFbxClip(Path, Mesh, ClipIdx);
			}
		});
	const bool bOk = NewTab->OpenFbxAsset(FbxPath);
	if (!bOk)
	{
		return false;
	}
	RequestedFocusTabId = NewTab->GetTabId();
	Tabs.emplace_back(std::move(NewTab));
	ActiveTabIndex = static_cast<int32>(Tabs.size()) - 1;
	return true;
}

bool FEditorSkeletalMeshViewerWidget::OpenAnimSequenceFromFbxClip(const FString& FbxPath, USkeletalMesh* PreviewMesh, int32 ClipIndex)
{
	if (!PreviewMesh)
	{
		return false;
	}

	// 같은 (FBX, clip) 조합이 이미 열려 있으면 그 탭으로 포커싱
	const FString SourceKey = FbxPath + "#clip" + std::to_string(ClipIndex);
	if (FSkeletalEditorTab* Existing = FindTabBySource(SourceKey))
	{
		RequestedFocusTabId = Existing->GetTabId();
		for (int32 i = 0; i < static_cast<int32>(Tabs.size()); ++i)
		{
			if (Tabs[i].get() == Existing)
			{
				ActiveTabIndex = i;
				break;
			}
		}
		return true;
	}

	auto NewTab = std::make_unique<FAnimSequenceEditorTab>(EditorEngine, NextTabId++);
	FAnimSequenceEditorTab* RawTab = NewTab.get();
	// Mode bar: SkelMesh 버튼 → 같은 FBX의 SkeletalMesh 탭 열기, Anim 버튼은 현재 활성 (no-op)
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
	NewTab->OpenFromFbxClip(FbxPath, PreviewMesh, ClipIndex);
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
		for (int32 i = 0; i < static_cast<int32>(Tabs.size()); ++i)
		{
			if (Tabs[i].get() == Existing)
			{
				ActiveTabIndex = i;
				break;
			}
		}
		return true;
	}

	auto NewTab = std::make_unique<FAnimSequenceEditorTab>(EditorEngine, NextTabId++);
	NewTab->OpenAnimSequenceAsset(AssetPath);
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
			const FString Label = Tab->GetTabLabel();
			if (ImGui::BeginTabItem(Label.c_str(), &bTabOpen, TabFlags))
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
