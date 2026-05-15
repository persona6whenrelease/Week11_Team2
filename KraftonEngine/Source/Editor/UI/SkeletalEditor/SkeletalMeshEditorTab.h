#pragma once

#include "Editor/UI/SkeletalEditor/SkeletalEditorTab.h"

class UFBXSceneAsset;

// SkeletalMesh 모드 탭: FBX 한 개 로드, 본 계층/리소스/Transform/베이크드 클립 재생.
class FSkeletalMeshEditorTab : public FSkeletalEditorTab
{
public:
	FSkeletalMeshEditorTab(UEditorEngine* InEditorEngine, int32 InTabId);

	bool OpenFbxAsset(const FString& FbxPath);

	FString GetTabLabel() const override;

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
	FString StatusMessage = "Double-click an FBX asset in ContentBrowser";
	int32 SelectedResourceIndex = -1;
	int32 SelectedBoneIndex = -1;
	bool bScrollToSelectedBone = false;
	int32 RequestSetOpenBoneIndex = -1;
	bool bRequestSetOpenValue = false;
};
