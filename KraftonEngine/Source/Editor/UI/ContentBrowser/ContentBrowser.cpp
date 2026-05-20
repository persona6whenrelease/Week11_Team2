#include "ContentBrowser.h"

#include "ContentBrowserElement.h"
#include "Editor/EditorEngine.h"
#include "Editor/Settings/EditorSettings.h"
#include "Asset/Import/FBX/Core/FBXManager.h"
#include <algorithm>
#include <cwctype>
#include <fstream>

namespace
{
	bool IsParentDirectoryReference(const std::filesystem::path& Path)
	{
		for (const std::filesystem::path& Part : Path)
		{
			if (Part == L"..")
			{
				return true;
			}
		}

		return false;
	}

	FString MakeContentBrowserSettingsPath(const std::wstring& CurrentPath)
	{
		const std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path Path = std::filesystem::path(CurrentPath).lexically_normal();
		const std::filesystem::path RelativePath = Path.lexically_relative(RootPath);

		if (!RelativePath.empty() && !IsParentDirectoryReference(RelativePath))
		{
			return FPaths::ToUtf8(RelativePath.generic_wstring());
		}

		return FPaths::ToUtf8(Path.wstring());
	}

	std::wstring ResolveContentBrowserSettingsPath(const FString& SavedPath)
	{
		if (SavedPath.empty())
		{
			return FPaths::RootDir();
		}

		std::filesystem::path Path(FPaths::ToWide(SavedPath));
		if (!Path.is_absolute())
		{
			Path = std::filesystem::path(FPaths::RootDir()) / Path;
		}

		Path = Path.lexically_normal();
		if (std::filesystem::exists(Path) && std::filesystem::is_directory(Path))
		{
			return Path.wstring();
		}

		return FPaths::RootDir();
	}

	bool IsSubPath(const std::filesystem::path& parent, const std::filesystem::path& child)
	{
		std::filesystem::path p = std::filesystem::weakly_canonical(parent);
		std::filesystem::path c = std::filesystem::weakly_canonical(child);

		auto pIt = p.begin();
		auto cIt = c.begin();

		for (; pIt != p.end() && cIt != c.end(); ++pIt, ++cIt)
		{
			if (*pIt != *cIt)
				return false;
		}

		return pIt == p.end(); // parent 끝까지 다 맞았으면 포함됨
	}

	std::filesystem::path MakeUniqueLuaTemplatePath(const std::filesystem::path& Directory)
	{
		std::filesystem::path Candidate = Directory / L"template.lua";
		int32 Index = 1;

		while (std::filesystem::exists(Candidate))
		{
			Candidate = Directory / (L"template" + std::to_wstring(Index) + L".lua");
			++Index;
		}

		return Candidate;
	}

	bool CreateLuaTemplateScript(const std::filesystem::path& Directory)
	{
		if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
		{
			return false;
		}

		const std::filesystem::path TargetPath = MakeUniqueLuaTemplatePath(Directory);
		std::ofstream File(TargetPath, std::ios::out);
		if (!File.is_open())
		{
			return false;
		}

		File <<
			"function BeginPlay()\n    print(\"[BeginPlay] \" .. obj.UUID)\n    obj:PrintLocation()\nend\n\n"
			"function EndPlay()\n    print(\"[EndPlay] \" .. obj.UUID)\n    obj:PrintLocation()\nend\n\n"
			"function OnOverlap(OtherActor)\n    OtherActor:PrintLocation();\nend\n\n"
			"function Tick(dt)\n    obj.Location = obj.Location + obj.Velocity * dt\n    obj:PrintLocation()\nend\n\n";

		return true;
	}

	std::filesystem::path MakeUniqueCurveTemplatePath(const std::filesystem::path& Directory)
	{
		std::filesystem::path Candidate = Directory / L"template.curve";
		int32 Index = 1;

		while (std::filesystem::exists(Candidate))
		{
			Candidate = Directory / (L"template" + std::to_wstring(Index) + L".curve");
			++Index;
		}

		return Candidate;
	}

	bool CreateCurveTemplateScript(const std::filesystem::path& Directory)
	{
		if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
		{
			return false;
		}

		const std::filesystem::path TargetPath = MakeUniqueCurveTemplatePath(Directory);
		std::ofstream File(TargetPath, std::ios::out);
		if (!File.is_open())
		{
			return false;
		}

		File <<
			"{\n"
			"    \"ControlPoints\" : [0.000000, 0.000000, 1.000000, 1.000000],\n"
			"    \"Kind\" : \"CubicBezier01\",\n"
			"    \"PresetIndex\" : 9,\n"
			"    \"Type\" : \"KraftonCurve\",\n"
			"    \"Version\" : 1\n"
			"};";

		return true;
	}
}

void FEditorContentBrowserWidget::Initialize(UEditorEngine* InEditor, ID3D11Device* InDevice)
{
	FEditorWidget::Initialize(InEditor);
	if (!InDevice) return;

	ContentBrowserContext Context;
	Context.ContentSize = ImVec2(125, 125);
	Context.EditorEngine = InEditor;
	BrowserContext = Context;
	LoadFromSettings();

	Refresh();
}

void FEditorContentBrowserWidget::Render(float DeltaTime)
{
	if (!ImGui::Begin("ContentBrowser"))
	{
		ImGui::End();
		return;
	}

	if (BrowserContext.bIsNeedRefresh)
	{
		RefreshContent();
		BrowserContext.bIsNeedRefresh = false;
	}

	if (ImGui::Button("Refresh") || BrowserContext.bIsNeedRefresh)
		Refresh();

	ImGui::SameLine();
	std::wstring PathText = BrowserContext.CurrentPath;
	if(BrowserContext.SelectedElement)
		PathText += L"/" + BrowserContext.SelectedElement->GetFileName();

	ImGui::Text(FPaths::ToUtf8(PathText).c_str());

	if (!ImGui::BeginTable("ContentBrowserLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::End();
		return;
	}

	ImGui::TableSetupColumn("Directory", ImGuiTableColumnFlags_WidthFixed, 250.0f);
	ImGui::TableSetupColumn("Content", ImGuiTableColumnFlags_WidthStretch);

	ImGui::TableNextColumn();
	{
		ImGui::BeginChild("DirectoryTree", ImVec2(0, 0), true);
		DrawDirNode(RootNode);
		BrowserContext.PendingRevealPath.clear();
		ImGui::EndChild();
	}

	ImGui::TableNextColumn();
	{
		ImGui::BeginChild("ContentArea", ImVec2(0, 0), true);
		DrawContents();
		ImGui::EndChild();
	}

	if (BrowserContext.SelectedElement)
		BrowserContext.SelectedElement->RenderDetail();

	ImGui::EndTable();
	ImGui::End();
}

void FEditorContentBrowserWidget::Refresh()
{
	RootNode = BuildDirectoryTree(FPaths::RootDir());
	RefreshContent();

	BrowserContext.bIsNeedRefresh = false;
}

void FEditorContentBrowserWidget::SetIconSize(float Size)
{
	const float ClampedSize = (std::max)(20.0f, (std::min)(Size, 100.0f));
	BrowserContext.ContentSize = ImVec2(ClampedSize, ClampedSize);
}

void FEditorContentBrowserWidget::LoadFromSettings()
{
	BrowserContext.CurrentPath = ResolveContentBrowserSettingsPath(FEditorSettings::Get().ContentBrowserPath);
	BrowserContext.PendingRevealPath = BrowserContext.CurrentPath;
}

void FEditorContentBrowserWidget::SaveToSettings() const
{
	FEditorSettings::Get().ContentBrowserPath = MakeContentBrowserSettingsPath(BrowserContext.CurrentPath);
}

void FEditorContentBrowserWidget::RefreshContent()
{
	CachedBrowserElements.clear();
	TArray<FContentItem> CurrentContents = ReadDirectory(BrowserContext.CurrentPath);
	for (const auto& Content : CurrentContents)
	{
		std::shared_ptr<ContentBrowserElement> element;

		if (Content.bIsDirectory)
		{
			element = std::make_shared<DirectoryElement>();
		}
		else if (Content.Path.extension() == ".Scene")
		{
			element = std::make_shared<SceneElement>();
		}
		else if (Content.Path.extension() == ".Prefab")
		{
			element = std::make_shared<PrefabElement>();
		}
		else if (Content.Path.extension() == ".obj")
		{
			element = std::make_shared<ObjectElement>();
		}
		else if (Content.Path.extension() == ".mat")
		{
			element = std::make_shared<MaterialElement>();
		}
		else if (Content.Path.extension() == ".lua")
		{
			element = std::make_shared<LuaScriptElement>();
		}
		else if (Content.Path.extension() == ".curve")
		{
			element = std::make_shared<CurveElement>();
		}
		else if (Content.Path.extension() == ".fbx" || Content.Path.extension() == ".FBX")
		{
			element = std::make_shared<FBXElement>();
		}
		else if (Content.Path.extension() == ".png" || Content.Path.extension() == ".PNG")
		{
			element = std::make_shared<PNGElement>();
		}
		else if ([&]() {
			// `.asset` 확장자만 소문자 정규화로 비교한다 (".asset" / ".ASSET" / ".Asset" 모두 인식).
			// 다른 확장자 분기의 대소문자 처리는 본 작업 범위 밖이라 그대로 둔다.
			std::wstring Ext = Content.Path.extension().wstring();
			std::transform(Ext.begin(), Ext.end(), Ext.begin(),
				[](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
			return Ext == L".asset";
		}())
		{
			// 헤더의 AssetType 검사는 더블클릭 시점에 한 번만 수행한다 (분류 단계 I/O 회피).
			element = std::make_shared<AnimSequenceAssetElement>();
		}
		else
		{
			element = std::make_shared<ContentBrowserElement>();
		}
		
		element.get()->SetContent(Content);
		CachedBrowserElements.push_back(std::move(element));
	}
}

void FEditorContentBrowserWidget::DrawDirNode(FDirNode InNode)
{
	ImGuiTreeNodeFlags Flag =
		InNode.Children.empty() ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_OpenOnArrow;

	if (InNode.Self.Path == BrowserContext.CurrentPath)
	{
		Flag |= ImGuiTreeNodeFlags_Selected;
	}
	if (!BrowserContext.PendingRevealPath.empty() && IsSubPath(InNode.Self.Path, BrowserContext.PendingRevealPath))
	{
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	}

	bool bIsOpen = ImGui::TreeNodeEx(FPaths::ToUtf8(InNode.Self.Name).c_str(), Flag);
	if (ImGui::IsItemClicked())
	{
		BrowserContext.CurrentPath = InNode.Self.Path;
		RefreshContent();
	}

	if (!bIsOpen)
	{
		return;
	}

	int32 ChildrenCount = static_cast<int32>(InNode.Children.size());
	for (int i = 0; i < ChildrenCount; i++)
	{
		DrawDirNode(InNode.Children[i]);
	}

	ImGui::TreePop();
}

void FEditorContentBrowserWidget::DrawContents()
{
	int elementCount = static_cast<int>(CachedBrowserElements.size());

	const float contentWidth = ImGui::GetContentRegionAvail().x;
	const float itemWidth = BrowserContext.ContentSize.x;
	const float itemHeight = BrowserContext.ContentSize.y;

	int columnCount = static_cast<int>(contentWidth / itemWidth);
	if (columnCount < 1)
	{
		columnCount = 1;
	}

	float gapSize = 0.0f;
	if (columnCount > 1)
	{
		gapSize = (contentWidth - itemWidth * columnCount) / (columnCount);
	}

	ImVec2 startPos = ImGui::GetCursorPos();
	BrowserContext.ContentGridStartPos = startPos;
	BrowserContext.ContentGridColumnCount = columnCount;
	BrowserContext.ContentGridSlotIndex = 0;
	BrowserContext.ContentGridGapX = gapSize;
	BrowserContext.ContentGridGapY = gapSize * 2.0f;
	BrowserContext.ContentGridMaxBottomY = startPos.y;
	BrowserContext.bContentGridSlotConsumed = false;

	for (int i = 0; i < elementCount; ++i)
	{
		BrowserContext.MoveToContentGridSlot();
		CachedBrowserElements[i]->Render(BrowserContext);
		if (!BrowserContext.bContentGridSlotConsumed)
		{
			BrowserContext.AdvanceContentGridSlot();
		}
	}

	if (BrowserContext.bPendingDeleteConfirm)
	{
		ImGui::OpenPopup("Delete?");
		BrowserContext.bPendingDeleteConfirm = false;
	}

	if (ImGui::BeginPopupModal("Delete?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (BrowserContext.PendingDeleteElement)
		{
			const std::string FileName = FPaths::ToUtf8(BrowserContext.PendingDeleteElement->GetFileName());
			ImGui::Text("'%s'", FileName.c_str());
			ImGui::Text("Are you sure you want to delete this?");
		}
		ImGui::Spacing();
		if (ImGui::Button("Delete", ImVec2(80, 0)))
		{
			if (BrowserContext.PendingDeleteElement)
			{
				const std::filesystem::path TargetPath = BrowserContext.PendingDeleteElement->GetFilePath();
				std::error_code Ec;
				std::filesystem::remove_all(TargetPath, Ec);
				if (!Ec)
				{
					if (BrowserContext.SelectedElement == BrowserContext.PendingDeleteElement)
					{
						BrowserContext.SelectedElement = nullptr;
					}
					BrowserContext.bIsNeedRefresh = true;
				}
				BrowserContext.PendingDeleteElement = nullptr;
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(80, 0)))
		{
			BrowserContext.PendingDeleteElement = nullptr;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	// FBX Import Options dialog
	if (BrowserContext.bShowFbxImportDialog)
	{
		ImGui::OpenPopup("FBX Import Options");
		BrowserContext.bShowFbxImportDialog = false;
	}

	ImVec2 Center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(Center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
	if (ImGui::BeginPopupModal("FBX Import Options", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextDisabled("File: %s", FPaths::ToUtf8(BrowserContext.PendingFbxImportPath).c_str());
		ImGui::Separator();

		// --- FPS 설정 ---
		ImGui::Text("Bake FPS");
		ImGui::RadioButton("30 fps", &BrowserContext.FbxImportFPSMode, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Custom", &BrowserContext.FbxImportFPSMode, 1);
		ImGui::SameLine();
		char OptimalLabel[64];
		snprintf(OptimalLabel, sizeof(OptimalLabel), "Optimal (%.0f fps)", BrowserContext.FbxNativeFPS);
		ImGui::RadioButton(OptimalLabel, &BrowserContext.FbxImportFPSMode, 2);

		if (BrowserContext.FbxImportFPSMode == 1)
		{
			ImGui::SetNextItemWidth(120.0f);
			ImGui::InputFloat("Custom fps##fbximport", &BrowserContext.FbxImportCustomFPS, 1.0f, 5.0f, "%.1f");
			if (BrowserContext.FbxImportCustomFPS < 1.0f)  BrowserContext.FbxImportCustomFPS = 1.0f;
			if (BrowserContext.FbxImportCustomFPS > 240.0f) BrowserContext.FbxImportCustomFPS = 240.0f;
		}

		ImGui::Separator();

		// --- 애니메이션 선택 ---
		ImGui::Text("Animations to import:");
		if (BrowserContext.FbxAnimationNames.empty())
		{
			ImGui::TextDisabled("  (no animations found)");
		}
		else
		{
			// Select all / none buttons
			if (ImGui::SmallButton("All"))
			{
				for (size_t i = 0; i < BrowserContext.FbxAnimSelected.size(); ++i) BrowserContext.FbxAnimSelected[i] = 1;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("None"))
			{
				for (size_t i = 0; i < BrowserContext.FbxAnimSelected.size(); ++i) BrowserContext.FbxAnimSelected[i] = 0;
			}

			ImGui::BeginChild("##animlist", ImVec2(0, 160), true);
			for (size_t i = 0; i < BrowserContext.FbxAnimationNames.size(); ++i)
			{
				bool bSelected = (i < BrowserContext.FbxAnimSelected.size()) ? BrowserContext.FbxAnimSelected[i] : true;
				char CheckLabel[256];
				snprintf(CheckLabel, sizeof(CheckLabel), "%s##anim%zu",
				         BrowserContext.FbxAnimationNames[i].c_str(), i);
				if (ImGui::Checkbox(CheckLabel, &bSelected))
				{
					if (i < BrowserContext.FbxAnimSelected.size())
					{
						BrowserContext.FbxAnimSelected[i] = bSelected;
					}
				}
			}
			ImGui::EndChild();
		}

		ImGui::Separator();
		ImGui::Spacing();

		bool bCanImport = BrowserContext.EditorEngine != nullptr;
		if (!bCanImport) ImGui::BeginDisabled();
		if (ImGui::Button("Import", ImVec2(100, 0)))
		{
			// Build import options from dialog state
			FFBXImportOptions Options;
			switch (BrowserContext.FbxImportFPSMode)
			{
			case 1: Options.FPSMode = FFBXImportOptions::EFPSMode::Custom;   Options.CustomFPS = BrowserContext.FbxImportCustomFPS; break;
			case 2: Options.FPSMode = FFBXImportOptions::EFPSMode::Optimal;  break;
			default: Options.FPSMode = FFBXImportOptions::EFPSMode::FPS30;   break;
			}
			// Animation filter: only pass indices of selected anims if not all are selected
			bool bAllSelected = true;
			for (bool b : BrowserContext.FbxAnimSelected) { if (!b) { bAllSelected = false; break; } }
			if (!bAllSelected)
			{
				for (int32 i = 0; i < static_cast<int32>(BrowserContext.FbxAnimSelected.size()); ++i)
				{
					if (BrowserContext.FbxAnimSelected[i])
					{
						Options.AnimationFilterIndices.push_back(i);
					}
				}
			}

			const FString FbxPath = FPaths::ToUtf8(BrowserContext.PendingFbxImportPath);
			if (BrowserContext.EditorEngine->ImportFbxWithOptions(FbxPath, Options))
			{
				const std::wstring DestDir =
					(std::filesystem::path(FPaths::RootDir()) / L"Asset" / L"Runtime" / L"SkeletalMesh").wstring();
				BrowserContext.CurrentPath = DestDir;
				BrowserContext.PendingRevealPath = DestDir;
				BrowserContext.bIsNeedRefresh = true;
			}
			ImGui::CloseCurrentPopup();
		}
		if (!bCanImport) ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(100, 0)))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopupContextWindow("ContentBrowserContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Create Lua Script"))
		{
			if (CreateLuaTemplateScript(std::filesystem::path(BrowserContext.CurrentPath)))
			{
				BrowserContext.bIsNeedRefresh = true;
			}
		}

		if (ImGui::MenuItem("Create Curve"))
		{
			if (CreateCurveTemplateScript(std::filesystem::path(BrowserContext.CurrentPath)))
			{
				BrowserContext.bIsNeedRefresh = true;
			}
		}

		ImGui::EndPopup();
	}
}

TArray<FContentItem> FEditorContentBrowserWidget::ReadDirectory(std::wstring Path)
{
	TArray<FContentItem> Items;

	if (!std::filesystem::exists(Path) || !std::filesystem::is_directory(Path))
		return Items;

	for (const auto& Entry : std::filesystem::directory_iterator(Path))
	{
		std::wstring Name = Entry.path().filename().wstring();
		if (Entry.is_directory())
		{
			if (Name == L"Bin" || Name == L"Build" || Name == L".git" || Name == L".vs")
				continue;
		}

		FContentItem Item;
		Item.Path = Entry.path();
		Item.Name = Name;
		Item.bIsDirectory = Entry.is_directory();

		Items.push_back(Item);
	}

	std::sort(Items.begin(), Items.end(),
		[](const FContentItem& A, const FContentItem& B)
		{
			if (A.bIsDirectory != B.bIsDirectory)
				return A.bIsDirectory > B.bIsDirectory;

			return A.Name < B.Name;
		});

	return Items;
}

FEditorContentBrowserWidget::FDirNode FEditorContentBrowserWidget::BuildDirectoryTree(const std::filesystem::path& DirPath)
{
	FDirNode Node;
	Node.Self.Path = DirPath;
	Node.Self.Name = DirPath.filename().wstring();
	Node.Self.bIsDirectory = true;

	for (const auto& Entry : std::filesystem::directory_iterator(DirPath))
	{
		if (!Entry.is_directory())
			continue;

		std::wstring DirName = Entry.path().filename().wstring();
		if (DirName == L"Bin" || DirName == L"Build" || DirName == L".git" || DirName == L".vs")
			continue;

		Node.Children.push_back(BuildDirectoryTree(Entry.path()));
	}

	if(Node.Self.Name.empty())
		Node.Self.Name = FPaths::ToWide("Project");

	return Node;
}
