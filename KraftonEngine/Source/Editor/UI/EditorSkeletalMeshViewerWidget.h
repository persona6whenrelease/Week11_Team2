#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/UI/SkeletalEditor/SkeletalEditorTab.h"

#include <memory>

class USkeletalMesh;

// 통합 셸: 여러 Skeletal Editor 탭(SkeletalMesh / AnimSequence)을 모은 하나의 ImGui 윈도우.
class FEditorSkeletalMeshViewerWidget : public FEditorWidget
{
public:
	~FEditorSkeletalMeshViewerWidget() override;

	void UpdateInput(float DeltaTime);
	void Render(float DeltaTime) override;

	bool OpenFbxAsset(const FString& FbxPath);
	bool OpenAnimSequenceAsset(const FString& AssetPath);
	// 임시: FBX 한 클립을 AnimSequence Editor 탭으로 띄움 (UAnimSequence asset 도입 전 stand-in)
	bool OpenAnimSequenceFromFbxClip(const FString& FbxPath, USkeletalMesh* PreviewMesh, int32 ClipIndex);

	bool WantsMouseCapture() const;
	bool WantsKeyboardCapture() const;

private:
	FSkeletalEditorTab* FindTabBySource(const FString& Path) const;
	FSkeletalEditorTab* GetActiveTab() const;
	void RequestCloseTab(int32 Index);

	TArray<std::unique_ptr<FSkeletalEditorTab>> Tabs;
	int32 ActiveTabIndex = -1;
	int32 NextTabId = 1;
	int32 RequestedFocusTabId = -1;
};
