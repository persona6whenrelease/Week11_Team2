#include "Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h"

#include <string>

FAnimSequenceEditorTab::FAnimSequenceEditorTab(UEditorEngine* InEditorEngine, int32 InTabId)
	: FSkeletalEditorTab(InEditorEngine, InTabId)
{
}

bool FAnimSequenceEditorTab::OpenAnimSequenceAsset(const FString& AssetPath)
{
	SetSourcePath(AssetPath);
	return true;
}

FString FAnimSequenceEditorTab::GetTabLabel() const
{
	const FString& Path = GetSourcePath();
	FString Base = Path.empty() ? FString("AnimSequence") : ExtractFileStem(Path);
	Base += " [Anim]";
	return Base + "###SkelEditorTab" + std::to_string(GetTabId());
}

void FAnimSequenceEditorTab::RenderLeftPanel()
{
	ImGui::TextUnformatted("AnimSequence Timeline (stub)");
	ImGui::Separator();
	ImGui::TextDisabled("Timeline / Notify track UI coming soon");
	ImGui::TextDisabled("Asset: %s", GetSourcePath().empty() ? "<none>" : GetSourcePath().c_str());
}

void FAnimSequenceEditorTab::RenderRightPanel()
{
	ImGui::TextDisabled("Details (TODO)");
}
