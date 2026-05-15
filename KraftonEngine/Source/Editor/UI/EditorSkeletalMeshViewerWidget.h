#pragma once

#include "Editor/UI/EditorWidget.h"
#include "GameFramework/World.h"
#include "ImGui/imgui.h"
#include "Render/Types/ViewTypes.h"

#include <memory>

class UFBXSceneAsset;
class USkeletalMesh;
class USkeletalMeshComponent;
class ADirectionalLightActor;
class FViewport;
class FSkeletalMeshViewerViewportClient;
class UEditorEngine;

// Persona 형식의 통합 에디터에서 각 탭이 갖는 동작 모드
enum class ESkeletalEditorMode : uint8_t
{
	SkeletalMesh,
	AnimSequence,
};

// 한 개의 탭(=한 개의 미리보기 씬)이 갖는 상태와 렌더 로직.
// SkeletalMesh / AnimSequence 모드 어느 쪽이든 자체 PreviewWorld/Viewport를 보유한다.
class FSkeletalEditorTab
{
public:
	FSkeletalEditorTab(UEditorEngine* InEditorEngine, int32 InTabId, ESkeletalEditorMode InMode);
	~FSkeletalEditorTab();

	bool OpenFbxAsset(const FString& FbxPath);
	bool OpenAnimSequenceAsset(const FString& AssetPath); // stub — AnimSequence 모드 탭 진입점

	void RenderTabContent(float DeltaTime);
	void UpdateInput(float DeltaTime);

	bool WantsMouseCapture() const { return bPreviewViewportWantsMouseCapture; }
	bool WantsKeyboardCapture() const { return bPreviewViewportWantsKeyboardCapture; }

	int32 GetTabId() const { return TabId; }
	ESkeletalEditorMode GetMode() const { return Mode; }
	const FString& GetSourcePath() const { return SourcePath; }
	FString GetTabLabel() const;

private:
	void EnsurePreviewScene();
	void ReleasePreviewScene();
	void SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bResetCamera = true);
	void TickPreviewScene(float DeltaTime);

	void RenderResourcePanel();
	void RenderViewportPanel(float DeltaTime);
	void RenderBonePanel();
	void RenderAnimationPlaybackPanel();
	void RenderTransformPanel();
	void RenderAnimSequenceTimelineStub(); // AnimSequence 모드 placeholder

	void RenderViewerViewportToolbar();
	void DrawViewerShowFlagsControls(FViewportRenderOptions& Opts, const char* TableId);

	USkeletalMesh* GetSelectedSkeletalMesh() const;
	void UpdateBoneDebugLines();

	UEditorEngine* EditorEngine = nullptr;
	int32 TabId = 0;
	ESkeletalEditorMode Mode = ESkeletalEditorMode::SkeletalMesh;
	FString SourcePath; // FBX 경로 또는 AnimSequence asset 경로

	UFBXSceneAsset* CurrentSceneAsset = nullptr;
	USkeletalMesh* PreviewSkeletalMesh = nullptr;
	FString CurrentFbxPath;
	FString StatusMessage = "Double-click an FBX asset in ContentBrowser";
	int32 SelectedResourceIndex = -1;
	int32 SelectedBoneIndex = -1;
	bool bScrollToSelectedBone = false;
	int32 RequestSetOpenBoneIndex = -1;
	bool bRequestSetOpenValue = false;

	UWorld* PreviewWorld = nullptr;
	AActor* PreviewActor = nullptr;
	ADirectionalLightActor* PreviewDirectionalLightActor = nullptr;
	USkeletalMeshComponent* PreviewMeshComponent = nullptr;
	FViewport* PreviewViewport = nullptr;
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = nullptr;
	ImVec2 PreviewViewportMin = ImVec2(0.0f, 0.0f);
	ImVec2 PreviewViewportMax = ImVec2(0.0f, 0.0f);
	bool bHasPreviewViewportRect = false;
	bool bPreviewViewportWantsMouseCapture = false;
	bool bPreviewViewportWantsKeyboardCapture = false;
	bool bDrawBoneDebugLines = true;
};

// 통합 Skeletal Editor 윈도우 셸. 여러 탭(SkeletalMesh / AnimSequence)을 띄울 수 있다.
class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	~FEditorSkeletalMeshViewerWidget() override;

	void UpdateInput(float DeltaTime);
	void Render(float DeltaTime) override;

	bool OpenFbxAsset(const FString& FbxPath);
	bool OpenAnimSequenceAsset(const FString& AssetPath); // stub

	bool WantsMouseCapture() const;
	bool WantsKeyboardCapture() const;

private:
	FSkeletalEditorTab* FindTabBySource(const FString& Path) const;
	FSkeletalEditorTab* GetActiveTab() const;
	void RequestCloseTab(int32 Index);

	TArray<std::unique_ptr<FSkeletalEditorTab>> Tabs;
	int32 ActiveTabIndex = -1;
	int32 NextTabId = 1;
	int32 RequestedFocusTabId = -1; // 새 탭 열렸을 때 그 탭으로 포커싱
};
