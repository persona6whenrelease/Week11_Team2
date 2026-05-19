#pragma once

#include "Editor/UI/EditorWidget.h"
#include "Editor/UI/SkeletalEditor/SkeletalEditorTab.h"

#include <memory>

class USkeletalMesh;
class UAnimSequence;

// 통합 셸: 여러 Skeletal Editor 탭(SkeletalMesh / AnimSequence)을 모은 하나의 ImGui 윈도우.
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
	FSkeletalEditorTab* FindTabBySource(const FString& Path) const;
	class FSkeletalMeshEditorTab* FindSkeletalMeshTabByMesh(USkeletalMesh* Mesh) const;
	class FAnimSequenceEditorTab* FindAnimSequenceTabByMesh(USkeletalMesh* Mesh) const;
	FSkeletalEditorTab* GetActiveTab() const;
	void FocusTab(FSkeletalEditorTab* Tab);
	void RequestCloseTab(int32 Index);

	TArray<std::unique_ptr<FSkeletalEditorTab>> Tabs;
	int32 ActiveTabIndex = -1;
	int32 NextTabId = 1;
	int32 RequestedFocusTabId = -1;
};
