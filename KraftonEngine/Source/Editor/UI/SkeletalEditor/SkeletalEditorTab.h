#pragma once

#include "Editor/UI/SkeletalEditor/SkeletalEditorPreviewScene.h"
#include "ImGui/imgui.h"

#include <functional>

class USkeletalMesh;
class UEditorEngine;
struct FViewportRenderOptions;

// 탭 종류 — mode bar에서 현재 탭 강조용
enum class ESkeletalEditorTabKind : uint8_t
{
	SkeletalMesh,
	AnimSequence,
};

// Skeletal Editor 탭의 추상 베이스.
// 공용: preview 뷰포트 / toolbar / show flags / 뷰포트 입력 / bone debug.
// 서브클래스: mode-specific 좌·우 패널과 라벨/현재 미리보기 메시 선택.
class FSkeletalEditorTab
{
public:
	FSkeletalEditorTab(UEditorEngine* InEditorEngine, int32 InTabId);
	virtual ~FSkeletalEditorTab();

	virtual void RenderTabContent(float DeltaTime);
	void UpdateInput(float DeltaTime);

	bool WantsMouseCapture() const { return bPreviewViewportWantsMouseCapture; }
	bool WantsKeyboardCapture() const { return bPreviewViewportWantsKeyboardCapture; }

	int32 GetTabId() const { return TabId; }
	const FString& GetSourcePath() const { return SourcePath; }

	virtual FString GetTabLabel() const = 0;
	virtual ESkeletalEditorTabKind GetKind() const = 0;

	// Mode bar 콜백 (위젯이 주입). nullptr이면 해당 모드 점프 불가 (= 버튼 disabled).
	using FSimpleCallback = std::function<void()>;
	void SetOnSwitchToSkeletalMesh(FSimpleCallback Cb) { OnSwitchToSkeletalMesh = std::move(Cb); }
	void SetOnSwitchToAnimSequence(FSimpleCallback Cb) { OnSwitchToAnimSequence = std::move(Cb); }

protected:
	// 서브클래스 구현 후크
	virtual USkeletalMesh* GetActivePreviewMesh() const = 0;
	virtual void RenderLeftPanel() = 0;
	virtual void RenderRightPanel() = 0;
	virtual void OnTickPreview(float DeltaTime) { (void)DeltaTime; }

	// 모든 서브클래스가 자기 RenderTabContent 상단에서 호출하는 공용 mode bar
	void RenderTabModeBar();

	void SetSourcePath(const FString& Path) { SourcePath = Path; }

	// 파일 경로에서 확장자/디렉터리 제외한 stem 추출 (탭 라벨 표시에 공통 사용).
	static FString ExtractFileStem(const FString& Path);
	// 디렉터리(/, \, |)만 제거. 확장자(.anm 등)는 유지.
	static FString ExtractFileName(const FString& Path);

	FSkeletalEditorPreviewScene PreviewScene;
	UEditorEngine* EditorEngine = nullptr;

	bool bDrawBoneDebugLines = false;

	// 서브클래스가 자체 layout을 짤 때 호출. 공용 toolbar / 입력 / bone debug까지 모두 처리.
	void RenderViewportPanel(float DeltaTime);

private:
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

	FSimpleCallback OnSwitchToSkeletalMesh;
	FSimpleCallback OnSwitchToAnimSequence;
};
