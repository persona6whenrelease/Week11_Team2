#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h"
#include "GameFramework/World.h"
#include "ImGui/imgui.h"
#include "Render/Types/ViewTypes.h"

#include <memory>

class UFBXSceneAsset;
class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
class ADirectionalLightActor;
class FViewport;
class FSkeletalMeshViewerViewportClient;

class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	~FEditorSkeletalMeshViewerWidget() override;

	void UpdateInput(float DeltaTime);
	void Render(float DeltaTime) override;
	bool OpenFbxAsset(const FString& FbxPath);
	bool OpenAnimSequenceAsset(const FString& AssetPath);
	bool OpenAnimSequenceAsset(const FString& AssetPath, USkeletalMesh* PreviewMesh, UAnimSequence* Sequence);
	bool WantsMouseCapture() const;
	bool WantsKeyboardCapture() const;

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
	void RenderAnimSequenceViewer(float DeltaTime);
	void RenderModeSwitchBar();

	void RenderViewerViewportToolbar();
	void DrawViewerShowFlagsControls(FViewportRenderOptions& Opts, const char* TableId);

	USkeletalMesh* GetSelectedSkeletalMesh() const;
	bool OpenSelectedAnimSequenceViewer();
	FSkeletalEditorTab* FindAnimSequenceTabBySource(const FString& Path) const;
	FSkeletalEditorTab* GetActiveAnimSequenceTab() const;
	void RequestCloseAnimSequenceTab(int32 Index);

	void UpdateBoneDebugLines();

	UFBXSceneAsset* CurrentSceneAsset = nullptr;
	USkeletalMesh* PreviewSkeletalMesh = nullptr;
	FString CurrentFbxPath;
	FString StatusMessage = "Double-click an FBX asset in ContentBrowser";
	int32 SelectedResourceIndex = -1;
	int32 SelectedAnimSequenceIndex = 0;
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

	TArray<std::unique_ptr<FAnimSequenceEditorTab>> AnimSequenceTabs;
	int32 ActiveAnimSequenceTabIndex = -1;
	int32 NextAnimSequenceTabId = 1;
	int32 RequestedFocusAnimSequenceTabId = -1;
	bool bAnimSequenceViewerOpen = false;
};
