#pragma once

#include "Editor/UI/SkeletalEditor/SkeletalEditorTab.h"

#include <functional>

class UFBXSceneAsset;

// SkeletalMesh 모드 탭: FBX 한 개 로드, 본 계층/리소스/Transform/베이크드 클립 재생.
class FSkeletalMeshEditorTab : public FSkeletalEditorTab
{
public:
	// 사용자가 RenderAnimationPlaybackPanel에서 "Edit in Anim Editor" 클릭 시 호출.
	// Widget(셸)이 새 AnimSequence 탭을 띄우는 콜백을 주입한다.
	using FOpenAnimEditorCallback = std::function<void(const FString& FbxPath, USkeletalMesh* PreviewMesh, int32 ClipIndex)>;

	FSkeletalMeshEditorTab(UEditorEngine* InEditorEngine, int32 InTabId);

	bool OpenFbxAsset(const FString& FbxPath);

	void SetOpenAnimEditorCallback(FOpenAnimEditorCallback Cb) { OpenAnimEditorCallback = std::move(Cb); }

	FString GetTabLabel() const override;
	ESkeletalEditorTabKind GetKind() const override { return ESkeletalEditorTabKind::SkeletalMesh; }

	// Mode bar / 외부 호출 편의용 (현재 미리보기 상태 조회)
	USkeletalMesh* GetCurrentPreviewMesh() const { return PreviewSkeletalMesh; }
	const FString& GetCurrentFbxPath() const { return CurrentFbxPath; }
	int32 GetCurrentClipIndex() const;

protected:
	USkeletalMesh* GetActivePreviewMesh() const override;
	void RenderLeftPanel() override;
	void RenderRightPanel() override;

private:
	void RenderResourcePanel();
	void RenderBonePanel();
	void RenderTransformPanel();
	void RenderAnimationPlaybackPanel();
	USkeletalMesh* GetSelectedSkeletalMesh() const;

	UFBXSceneAsset* CurrentSceneAsset = nullptr;
	USkeletalMesh* PreviewSkeletalMesh = nullptr;
	FString CurrentFbxPath;
	FString StatusMessage = "Double-click an FBX asset in ContentBrowser";
	FOpenAnimEditorCallback OpenAnimEditorCallback;
	int32 SelectedResourceIndex = -1;
	int32 SelectedBoneIndex = -1;
	bool bScrollToSelectedBone = false;
	int32 RequestSetOpenBoneIndex = -1;
	bool bRequestSetOpenValue = false;
};
