#pragma once

#include "Editor/UI/SkeletalEditor/SkeletalEditorTab.h"

// AnimSequence 모드 탭 (Stub): 발제 TO-DO 4번 Animation Sequence Viewer.
// 추후 Timeline / Notify track / playback 편집을 여기에 채운다.
class FAnimSequenceEditorTab : public FSkeletalEditorTab
{
public:
	FAnimSequenceEditorTab(UEditorEngine* InEditorEngine, int32 InTabId);

	bool OpenAnimSequenceAsset(const FString& AssetPath);

	FString GetTabLabel() const override;

protected:
	USkeletalMesh* GetActivePreviewMesh() const override { return nullptr; }
	void RenderLeftPanel() override;
	void RenderRightPanel() override;
};
