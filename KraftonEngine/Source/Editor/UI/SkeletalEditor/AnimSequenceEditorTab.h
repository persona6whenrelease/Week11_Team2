#pragma once

#include "Editor/UI/SkeletalEditor/SkeletalEditorTab.h"
#include "Editor/UI/SkeletalEditor/AnimSequenceDataSource.h"

#include <memory>

// AnimSequence 모드 탭: 발제 TO-DO 4번 Animation Sequence Viewer.
// 현재는 UAnimSequence 기반 데이터 소스로 동작한다.
// 뷰어/편집기 코드는 DataSource 인터페이스를 통해 시퀀스 데이터에 접근한다.
class FAnimSequenceEditorTab : public FSkeletalEditorTab
{
public:
	FAnimSequenceEditorTab(UEditorEngine* InEditorEngine, int32 InTabId);

	// 정식 entry point — UAnimSequence asset 경로용. 현재는 stub.
	bool OpenAnimSequenceAsset(const FString& AssetPath);
	bool OpenAnimSequenceAsset(const FString& AssetPath, USkeletalMesh* InPreviewMesh, UAnimSequence* InSequence);

	// 임시 entry point — FBX의 baked clip 한 개를 anim sequence처럼 다룸.
	// FbxPath은 mode bar의 "Open SkeletalMesh Editor" 버튼이 같은 FBX를 열기 위해 보관.
	const FString& GetFbxPath() const { return FbxPath; }
	int32 GetClipIndex() const { return BakedClipIndex; }
	USkeletalMesh* GetCurrentPreviewMesh() const { return PreviewMesh; }
	UAnimSequence* GetCurrentAnimSequence() const { return AnimSequence; }

	FString GetTabLabel() const override;
	ESkeletalEditorTabKind GetKind() const override { return ESkeletalEditorTabKind::AnimSequence; }

	// UE Persona식 레이아웃: 중앙을 위(viewport)/아래(timeline)로 분할
	void RenderTabContent(float DeltaTime) override;

protected:
	USkeletalMesh* GetActivePreviewMesh() const override { return PreviewMesh; }
	void RenderLeftPanel() override;   // Asset info / (future Skeleton tree)
	void RenderRightPanel() override;  // Notify details
	void OnTickPreview(float DeltaTime) override;

private:
	void RenderCenterColumn(float DeltaTime); // viewport + 가변 splitter + timeline
	void RenderTimelinePanel();               // header(filter) + tracks(label/content) + playback + notify props
	void RenderPlaybackControls();
	void RenderNotifyPropertyInline();        // 선택된 notify의 속성을 timeline 패널 안에 인라인 표시
	void RenderAssetBrowser();                // 우하단: 같은 mesh의 다른 anim sequence 리스트
	void RenderBoneTransformPanel();
	void SyncPlaybackToComponent();

	std::unique_ptr<IAnimSequenceDataSource> DataSource;
	USkeletalMesh* PreviewMesh = nullptr;
	UAnimSequence* AnimSequence = nullptr;
	int32 BakedClipIndex = -1; // PreviewMeshComponent에 setBakedAnimClipIndex로 설정한 값
	FString FbxPath;           // mode bar 점프용 원본 FBX 경로

	float CurrentTime = 0.0f;
	bool  bPlaying = false;
	bool  bLooping = true;
	bool  bRecording = false;        // UI placeholder (실제 녹화 기능 미구현)
	float PlayRate = 1.0f;

	// Notify 편집 state
	int32 SelectedNotifyIndex = -1;
	int32 DraggingNotifyIndex = -1;
	bool  bScrubbing = false;        // 좌클릭 드래그가 scrub 모드인지

	// Skeleton tree (좌측 패널) state
	int32 SelectedBoneIndex = -1;
	bool  bScrollToSelectedBone = false;
	int32 RequestSetOpenBoneIndex = -1;
	bool  bRequestSetOpenValue = false;

	// Timeline 패널 layout state
	float TimelinePanelHeight = 260.0f;     // viewport↔timeline 가변 splitter
	float TimelineHeaderColWidth = 200.0f;  // 좌측 트랙 헤더 컬럼 너비
	char  TimelineFilterBuf[64] = "";

	// Right panel layout state (Details 위 / Asset Browser 아래)
	float RightPanelTopHeight = 280.0f;
	char  AssetBrowserFilterBuf[64] = "";
};
