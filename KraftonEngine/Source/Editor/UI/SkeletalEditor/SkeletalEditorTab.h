#pragma once

#include "Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.h"
#include "ImGui/imgui.h"

class USkeletalMesh;
class UEditorEngine;
struct FViewportRenderOptions;

// Skeletal Editor 탭의 추상 베이스.
// 공용: preview 뷰포트 / toolbar / show flags / 뷰포트 입력 / bone debug.
// 서브클래스: mode-specific 좌·우 패널과 라벨/현재 미리보기 메시 선택.
class FSkeletalEditorTab
{
public:
	FSkeletalEditorTab(UEditorEngine* InEditorEngine, int32 InTabId);
	virtual ~FSkeletalEditorTab();

	void RenderTabContent(float DeltaTime);
	void UpdateInput(float DeltaTime);

	bool WantsMouseCapture() const { return bPreviewViewportWantsMouseCapture; }
	bool WantsKeyboardCapture() const { return bPreviewViewportWantsKeyboardCapture; }

	int32 GetTabId() const { return TabId; }
	const FString& GetSourcePath() const { return SourcePath; }

	virtual FString GetTabLabel() const = 0;

protected:
	// 서브클래스 구현 후크
	virtual USkeletalMesh* GetActivePreviewMesh() const = 0;
	virtual void RenderLeftPanel() = 0;
	virtual void RenderRightPanel() = 0;
	virtual void OnTickPreview(float DeltaTime) { (void)DeltaTime; }

	void SetSourcePath(const FString& Path) { SourcePath = Path; }

	// 파일 경로에서 확장자/디렉터리 제외한 stem 추출 (탭 라벨 표시에 공통 사용).
	static FString ExtractFileStem(const FString& Path);

	FSkeletalEditorPreviewScene PreviewScene;
	UEditorEngine* EditorEngine = nullptr;

	bool bDrawBoneDebugLines = true;

private:
	void RenderViewportPanel(float DeltaTime);
	void RenderViewerViewportToolbar();
	void DrawViewerShowFlagsControls(FViewportRenderOptions& Opts, const char* TableId);
	void UpdateBoneDebugLines();

	int32 TabId = 0;
	FString SourcePath;

	ImVec2 PreviewViewportMin = ImVec2(0.0f, 0.0f);
	ImVec2 PreviewViewportMax = ImVec2(0.0f, 0.0f);
	bool bHasPreviewViewportRect = false;
	bool bPreviewViewportWantsMouseCapture = false;
	bool bPreviewViewportWantsKeyboardCapture = false;
};
