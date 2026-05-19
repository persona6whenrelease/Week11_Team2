#include "Editor/UI/EditorPropertyWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorFileUtils.h"
#include "Serialization/PrefabSaveManager.h"

#include <filesystem>
#include "ImGui/imgui.h"
#include "Component/ActorComponent.h"
#include "Component/ControllerInputComponent.h"
#include "Component/PawnOrientationComponent.h"
#include "Component/BillboardComponent.h"
#include "Component/MeshComponent.h"
#include "Component/Movement/MovementComponent.h"
#include "Component/Movement/PawnMovementComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SceneComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/Light/LightComponentBase.h"
#include "Component/DecalComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/Collision/ShapeComponent.h"
#include "Component/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/World.h"
#include "Core/PropertyTypes.h"
#include "Core/ClassTypes.h"
#include "Resource/ResourceManager.h"
#include "Object/FName.h"
#include "Object/ObjectIterator.h"
#include "Asset/Material/Material.h"
#include "Asset/Import/MeshManager.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/StaticMesh/StaticMesh.h"
#include "Platform/Paths.h"

#include <Windows.h>
#include <commdlg.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <cwctype>
#include <filesystem>

#include "Asset/Material/MaterialManager.h"

#define SEPARATOR(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();

namespace
{
	static FString GetStemFromPath(const FString& Path);

	bool ShouldHideInComponentTree(const UActorComponent* Component, bool bShowEditorOnlyComponents)
	{
		if (!Component)
		{
			return true;
		}

		return Component->IsHiddenInComponentTree()
			&& !(bShowEditorOnlyComponents && Component->IsEditorOnlyComponent());
	}

	struct FComponentClassGroup
	{
		const char* Label = nullptr;
		UClass* AnchorClass = nullptr;
		TArray<UClass*> Classes;
	};

	void AddComponentClassGroup(TArray<FComponentClassGroup>& Groups, const char* Label, UClass* AnchorClass)
	{
		FComponentClassGroup Group;
		Group.Label = Label;
		Group.AnchorClass = AnchorClass;
		Groups.push_back(Group);
	}

	UClass* FindComponentClassGroupAnchor(UClass* ComponentClass, const TArray<FComponentClassGroup>& Groups)
	{
		if (!ComponentClass)
		{
			return nullptr;
		}

		// UTextRenderComponent??C++ ?곸냽? Billboard吏留?RTTI ?깅줉 遺紐④? Primitive?쇱꽌 紐낆떆?곸쑝濡?臾띕뒗??
		if (ComponentClass == UTextRenderComponent::StaticClass())
		{
			return UBillboardComponent::StaticClass();
		}

		for (const FComponentClassGroup& Group : Groups)
		{
			if (Group.AnchorClass && ComponentClass->IsA(Group.AnchorClass))
			{
				return Group.AnchorClass;
			}
		}

		return nullptr;
	}

	bool IsPathInsideDirectory(const std::filesystem::path& AbsolutePath, const std::filesystem::path& Directory)
	{
		const std::filesystem::path RelativePath = AbsolutePath.lexically_relative(Directory);
		if (RelativePath.empty() || RelativePath.is_absolute())
		{
			return false;
		}

		for (const std::filesystem::path& Part : RelativePath)
		{
			if (Part == L"..")
			{
				return false;
			}
		}

		return true;
	}

	FString MakeLuaScriptPropertyPath(const std::filesystem::path& AbsolutePath)
	{
		const std::filesystem::path ScriptDir = std::filesystem::path(FPaths::ScriptDir()).lexically_normal();
		const std::filesystem::path RootDir = std::filesystem::path(FPaths::RootDir()).lexically_normal();

		if (IsPathInsideDirectory(AbsolutePath, ScriptDir))
		{
			return FPaths::ToUtf8(AbsolutePath.lexically_relative(ScriptDir).generic_wstring());
		}

		if (IsPathInsideDirectory(AbsolutePath, RootDir))
		{
			return FPaths::ToUtf8(AbsolutePath.lexically_relative(RootDir).generic_wstring());
		}

		return FPaths::ToUtf8(AbsolutePath.generic_wstring());
	}

	bool IsLuaScriptFile(const std::filesystem::path& Path)
	{
		std::wstring Extension = Path.extension().wstring();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(),
			[](wchar_t Ch) { return static_cast<wchar_t>(std::towlower(Ch)); });
		return Extension == L".lua";
	}

	void CollectReflectedStructProperties(const UClass* StructType, void* StructValue, TArray<FPropertyDescriptor>& OutProps)
	{
		if (!StructType || !StructValue)
		{
			return;
		}

		TArray<const UClass*> Chain;
		for (const UClass* C = StructType; C; C = C->GetSuperClass())
		{
			Chain.push_back(C);
		}

		for (int32 Index = static_cast<int32>(Chain.size()) - 1; Index >= 0; --Index)
		{
			for (const FPropertyDescriptor& Desc : Chain[Index]->GetOwnProperties())
			{
				FPropertyDescriptor Inst = Desc;
				Inst.ValuePtr = reinterpret_cast<char*>(StructValue) + reinterpret_cast<size_t>(Desc.ValuePtr);
				OutProps.push_back(Inst);
			}
		}
	}

	bool BuildArrayElementDescriptor(const FPropertyDescriptor& ArrayProp, size_t ElementIndex, FPropertyDescriptor& OutElementProp)
	{
		if (!ArrayProp.GetArrayElementGetter() || !ArrayProp.ValuePtr)
		{
			return false;
		}

		OutElementProp = ArrayProp;
		OutElementProp.TypeDesc = ArrayProp.GetElementType();
		OutElementProp.ValuePtr = ArrayProp.GetArrayElementGetter()(ArrayProp.ValuePtr, ElementIndex);
		OutElementProp.Name = "[" + std::to_string(ElementIndex) + "]";
		return OutElementProp.ValuePtr != nullptr;
	}

	bool ArePropertyTypesCompatible(const FPropertyTypeDesc* A, const FPropertyTypeDesc* B)
	{
		if (A == B)
		{
			return true;
		}
		if (!A || !B)
		{
			return false;
		}
		if (A->Kind != B->Kind)
		{
			return false;
		}
		if (A->StructType != B->StructType
			|| A->EnumCount != B->EnumCount
			|| ((A->ObjectClass || B->ObjectClass) && A->ObjectClass != B->ObjectClass))
		{
			return false;
		}
		return ArePropertyTypesCompatible(A->ElementType, B->ElementType)
			&& ArePropertyTypesCompatible(A->KeyType, B->KeyType)
			&& ArePropertyTypesCompatible(A->ValueType, B->ValueType);
	}

	bool IsObjectRefType(const FPropertyDescriptor& Prop, const UClass* ExpectedClass)
	{
		const UClass* ObjectClass = Prop.GetObjectClass();
		return ObjectClass
			&& ExpectedClass
			&& ObjectClass->IsA(ExpectedClass);
	}

	bool RenderSceneComponentObjectRefWidget(
		const FPropertyDescriptor& Prop,
		UActorComponent* SelectedComponent,
		bool& bChanged)
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);
		UMovementComponent* MovementComp = SelectedComponent ? Cast<UMovementComponent>(SelectedComponent) : nullptr;
		FString Preview = MovementComp ? MovementComp->GetUpdatedComponentDisplayName() : FString("None");

		if (!ImGui::BeginCombo(Prop.Name.c_str(), Preview.c_str()))
		{
			return bChanged;
		}

		const bool bSelectedAuto = Val->empty();
		if (ImGui::Selectable("Auto (Root)", bSelectedAuto))
		{
			Val->clear();
			bChanged = true;
		}
		if (bSelectedAuto)
		{
			ImGui::SetItemDefaultFocus();
		}

		if (MovementComp)
		{
			for (USceneComponent* Candidate : MovementComp->GetOwnerSceneComponents())
			{
				if (!Candidate)
				{
					continue;
				}

				FString CandidatePath = MovementComp->BuildUpdatedComponentPath(Candidate);
				FString CandidateName = Candidate->GetFName().ToString();
				if (CandidateName.empty())
				{
					CandidateName = Candidate->GetClass()->GetName();
				}
				if (!CandidatePath.empty())
				{
					CandidateName += " (" + CandidatePath + ")";
				}

				const bool bSelected = (*Val == CandidatePath);
				if (ImGui::Selectable(CandidateName.c_str(), bSelected))
				{
					*Val = CandidatePath;
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
		}

		ImGui::EndCombo();
		return bChanged;
	}

	bool RenderStaticMeshObjectRefWidget(
		const FPropertyDescriptor& Prop,
		bool& bChanged,
		FString (*OpenObjFileDialogFunc)())
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);
		FString Preview = Val->empty() ? "None" : GetStemFromPath(*Val);
		if (*Val == "None")
		{
			Preview = "None";
		}

		ImGui::Text("%s", Prop.Name.c_str());
		ImGui::SameLine(120);

		const float ButtonWidth = ImGui::CalcTextSize("Import OBJ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(-(ButtonWidth + Spacing));

		if (ImGui::BeginCombo("##Mesh", Preview.c_str()))
		{
			const bool bSelectedNone = (*Val == "None");
			if (ImGui::Selectable("None", bSelectedNone))
			{
				*Val = "None";
				bChanged = true;
			}
			if (bSelectedNone)
			{
				ImGui::SetItemDefaultFocus();
			}

			const TArray<FMeshAssetListItem>& MeshFiles = FMeshManager::GetAvailableStaticMeshFiles();
			for (const FMeshAssetListItem& Item : MeshFiles)
			{
				const bool bSelected = (*Val == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					*Val = Item.FullPath;
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ButtonWidth);
		if (ImGui::Button("Import OBJ"))
		{
			FString ObjPath = OpenObjFileDialogFunc ? OpenObjFileDialogFunc() : FString();
			if (!ObjPath.empty())
			{
				ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
				UStaticMesh* Loaded = FMeshManager::LoadStaticMesh(ObjPath, Device);
				if (Loaded)
				{
					*Val = FMeshManager::GetObjBinaryFilePath(ObjPath);
					bChanged = true;
				}
			}
		}

		return bChanged;
	}

	bool RenderSkeletalMeshObjectRefWidget(const FPropertyDescriptor& Prop, bool& bChanged)
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);
		FString Preview = Val->empty() ? "None" : GetStemFromPath(*Val);
		if (*Val == "None")
		{
			Preview = "None";
		}

		ImGui::Text("%s", Prop.Name.c_str());
		ImGui::SameLine(120);
		ImGui::SetNextItemWidth(-1.0f);

		if (ImGui::BeginCombo("##SkeletalMesh", Preview.c_str()))
		{
			const bool bSelectedNone = (*Val == "None");
			if (ImGui::Selectable("None", bSelectedNone))
			{
				*Val = "None";
				bChanged = true;
			}
			if (bSelectedNone)
			{
				ImGui::SetItemDefaultFocus();
			}

			if (!Val->empty() && *Val != "None")
			{
				const bool bSelectedCurrent = true;
				if (ImGui::Selectable(Preview.c_str(), bSelectedCurrent))
				{
				}
				if (bSelectedCurrent)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		return bChanged;
	}

	bool RenderGenericObjectRefWidget(const FPropertyDescriptor& Prop, bool& bChanged)
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);
		char Buf[256];
		strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
		if (ImGui::InputText(Prop.Name.c_str(), Buf, sizeof(Buf)))
		{
			*Val = Buf;
			bChanged = true;
		}
		return bChanged;
	}

	bool CopyPropertyValueRecursive(const FPropertyDescriptor& SrcProp, FPropertyDescriptor& DstProp)
	{
		if (!ArePropertyTypesCompatible(SrcProp.TypeDesc, DstProp.TypeDesc) || !SrcProp.ValuePtr || !DstProp.ValuePtr)
		{
			return false;
		}

		switch (DstProp.GetKind())
		{
		case EPropertyType::Bool:
			*static_cast<bool*>(DstProp.ValuePtr) = *static_cast<bool*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::ByteBool:
			*static_cast<uint8*>(DstProp.ValuePtr) = *static_cast<uint8*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::Int:
			*static_cast<int32*>(DstProp.ValuePtr) = *static_cast<int32*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::Float:
			*static_cast<float*>(DstProp.ValuePtr) = *static_cast<float*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::Vec3:
			*static_cast<FVector*>(DstProp.ValuePtr) = *static_cast<FVector*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::Rotator:
			*static_cast<FRotator*>(DstProp.ValuePtr) = *static_cast<FRotator*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::Vec4:
		case EPropertyType::Color4:
			*static_cast<FVector4*>(DstProp.ValuePtr) = *static_cast<FVector4*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::String:
		case EPropertyType::ObjectRef:
			*static_cast<FString*>(DstProp.ValuePtr) = *static_cast<FString*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::Name:
			*static_cast<FName*>(DstProp.ValuePtr) = *static_cast<FName*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::MaterialSlot:
			*static_cast<FMaterialSlot*>(DstProp.ValuePtr) = *static_cast<FMaterialSlot*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::Enum:
			*static_cast<int32*>(DstProp.ValuePtr) = *static_cast<int32*>(SrcProp.ValuePtr);
			return true;

		case EPropertyType::Array:
		{
			if (!SrcProp.GetArraySizeGetter() || !DstProp.GetArraySizeGetter()
				|| !SrcProp.GetArrayResizeFunc() || !DstProp.GetArrayResizeFunc())
			{
				return false;
			}

			const size_t Count = SrcProp.GetArraySizeGetter()(SrcProp.ValuePtr);
			DstProp.GetArrayResizeFunc()(DstProp.ValuePtr, Count);
			for (size_t ElementIndex = 0; ElementIndex < Count; ++ElementIndex)
			{
				FPropertyDescriptor SrcElement;
				FPropertyDescriptor DstElement;
				if (!BuildArrayElementDescriptor(SrcProp, ElementIndex, SrcElement)
					|| !BuildArrayElementDescriptor(DstProp, ElementIndex, DstElement)
					|| !CopyPropertyValueRecursive(SrcElement, DstElement))
				{
					return false;
				}
			}
			return true;
		}

		case EPropertyType::Struct:
		{
			if (SrcProp.GetStructType() != DstProp.GetStructType())
			{
				return false;
			}

			TArray<FPropertyDescriptor> SrcChildren;
			TArray<FPropertyDescriptor> DstChildren;
			CollectReflectedStructProperties(SrcProp.GetStructType(), SrcProp.ValuePtr, SrcChildren);
			CollectReflectedStructProperties(DstProp.GetStructType(), DstProp.ValuePtr, DstChildren);
			if (SrcChildren.size() != DstChildren.size())
			{
				return false;
			}

			for (size_t ChildIndex = 0; ChildIndex < SrcChildren.size(); ++ChildIndex)
			{
				if (SrcChildren[ChildIndex].Name != DstChildren[ChildIndex].Name
					|| !CopyPropertyValueRecursive(SrcChildren[ChildIndex], DstChildren[ChildIndex]))
				{
					return false;
				}
			}
			return true;
		}

		case EPropertyType::Set:
		{
			if (!SrcProp.TypeDesc || !DstProp.TypeDesc
				|| !SrcProp.TypeDesc->SetConstSnapshotFunc || !DstProp.TypeDesc->SetClearFunc
				|| !DstProp.TypeDesc->SetInsertFunc || !DstProp.TypeDesc->SetElementSizeFunc
				|| !SrcProp.TypeDesc->ElementType)
				return false;

			DstProp.TypeDesc->SetClearFunc(DstProp.ValuePtr);
			const size_t ElemSz = DstProp.TypeDesc->SetElementSizeFunc();
			TArray<const void*> Elems;
			SrcProp.TypeDesc->SetConstSnapshotFunc(SrcProp.ValuePtr, Elems);
			TArray<uint8_t> Buf(ElemSz, 0);
			for (const void* Elem : Elems)
			{
				std::fill(Buf.begin(), Buf.end(), 0);
				FPropertyDescriptor SrcElem, DstElem;
				SrcElem.TypeDesc = SrcProp.TypeDesc->ElementType;
				SrcElem.ValuePtr = const_cast<void*>(Elem);
				DstElem.TypeDesc = DstProp.TypeDesc->ElementType;
				DstElem.ValuePtr = Buf.data();
				if (!CopyPropertyValueRecursive(SrcElem, DstElem))
					return false;
				DstProp.TypeDesc->SetInsertFunc(DstProp.ValuePtr, Buf.data());
			}
			return true;
		}

		case EPropertyType::Map:
		{
			if (!SrcProp.TypeDesc || !DstProp.TypeDesc
				|| !SrcProp.TypeDesc->MapConstSnapshotFunc || !DstProp.TypeDesc->MapClearFunc
				|| !DstProp.TypeDesc->MapInsertFunc
				|| !DstProp.TypeDesc->MapKeySizeFunc || !DstProp.TypeDesc->MapValueSizeFunc
				|| !SrcProp.TypeDesc->KeyType || !SrcProp.TypeDesc->ValueType)
				return false;

			DstProp.TypeDesc->MapClearFunc(DstProp.ValuePtr);
			const size_t KeySz = DstProp.TypeDesc->MapKeySizeFunc();
			const size_t ValSz = DstProp.TypeDesc->MapValueSizeFunc();
			TArray<const void*> Keys, Vals;
			SrcProp.TypeDesc->MapConstSnapshotFunc(SrcProp.ValuePtr, Keys, Vals);
			TArray<uint8_t> KeyBuf(KeySz, 0), ValBuf(ValSz, 0);
			for (size_t i = 0; i < Keys.size(); ++i)
			{
				std::fill(KeyBuf.begin(), KeyBuf.end(), 0);
				std::fill(ValBuf.begin(), ValBuf.end(), 0);
				FPropertyDescriptor SK, SV, DK, DV;
				SK.TypeDesc = SrcProp.TypeDesc->KeyType;   SK.ValuePtr = const_cast<void*>(Keys[i]);
				SV.TypeDesc = SrcProp.TypeDesc->ValueType; SV.ValuePtr = const_cast<void*>(Vals[i]);
				DK.TypeDesc = DstProp.TypeDesc->KeyType;   DK.ValuePtr = KeyBuf.data();
				DV.TypeDesc = DstProp.TypeDesc->ValueType; DV.ValuePtr = ValBuf.data();
				if (!CopyPropertyValueRecursive(SK, DK) || !CopyPropertyValueRecursive(SV, DV))
					return false;
				DstProp.TypeDesc->MapInsertFunc(DstProp.ValuePtr, KeyBuf.data(), ValBuf.data());
			}
			return true;
		}

		case EPropertyType::ActorRef:
			*static_cast<uint32*>(DstProp.ValuePtr) = *static_cast<uint32*>(SrcProp.ValuePtr);
			return true;
		}

		return false;
	}
}

static FString RemoveExtension(const FString& Path)
{
	size_t DotPos = Path.find_last_of('.');
	if (DotPos == FString::npos)
	{
		return Path;
	}
	return Path.substr(0, DotPos);
}

static FString GetStemFromPath(const FString& Path)
{
	size_t SlashPos = Path.find_last_of("/\\");
	FString FileName = (SlashPos == FString::npos) ? Path : Path.substr(SlashPos + 1);
	return FileName;
}

FString FEditorPropertyWidget::OpenObjFileDialog()
{
	wchar_t FilePath[MAX_PATH] = {};

	OPENFILENAMEW Ofn = {};
	Ofn.lStructSize = sizeof(Ofn);
	Ofn.hwndOwner = nullptr;
	Ofn.lpstrFilter = L"OBJ Files (*.obj)\0*.obj\0All Files (*.*)\0*.*\0";
	Ofn.lpstrFile = FilePath;
	Ofn.nMaxFile = MAX_PATH;
	Ofn.lpstrTitle = L"Import OBJ Mesh";
	Ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameW(&Ofn))
	{
		std::filesystem::path AbsPath = std::filesystem::path(FilePath).lexically_normal();
		std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir());
		std::filesystem::path RelPath = AbsPath.lexically_relative(RootPath);

		// ?곷? 寃쎈줈 蹂???ㅽ뙣 ??(?쒕씪?대툕媛 ?ㅻⅨ 寃쎌슦 ?? ?덈? 寃쎈줈瑜?洹몃?濡?諛섑솚
		if (RelPath.empty() || RelPath.wstring().starts_with(L".."))
		{
			return FPaths::ToUtf8(AbsPath.generic_wstring());
		}
		return FPaths::ToUtf8(RelPath.generic_wstring());
	}

	return FString();
}

FString FEditorPropertyWidget::OpenFbxFileDialog()
{
	wchar_t FilePath[MAX_PATH] = {};

	OPENFILENAMEW Ofn = {};
	Ofn.lStructSize = sizeof(Ofn);
	Ofn.hwndOwner = nullptr;
	Ofn.lpstrFilter = L"FBX Files (*.fbx)\0*.fbx\0All Files (*.*)\0*.*\0";
	Ofn.lpstrFile = FilePath;
	Ofn.nMaxFile = MAX_PATH;
	Ofn.lpstrTitle = L"Import FBX Skeletal Mesh";
	Ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameW(&Ofn))
	{
		std::filesystem::path AbsPath = std::filesystem::path(FilePath).lexically_normal();
		std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir());
		std::filesystem::path RelPath = AbsPath.lexically_relative(RootPath);

		if (RelPath.empty() || RelPath.wstring().starts_with(L".."))
		{
			return FPaths::ToUtf8(AbsPath.generic_wstring());
		}
		return FPaths::ToUtf8(RelPath.generic_wstring());
	}

	return FString();
}

FString FEditorPropertyWidget::OpenLuaScriptFileDialog()
{
	const std::wstring ScriptDirectory = FPaths::ScriptDir();
	FPaths::CreateDir(ScriptDirectory);

	FEditorFileDialogOptions Options;
	Options.Filter = L"Lua Scripts (*.lua)\0*.lua\0";
	Options.Title = L"Select Lua Script";
	Options.DefaultExtension = L"lua";
	Options.InitialDirectory = ScriptDirectory.c_str();
	Options.bFileMustExist = true;
	Options.bPathMustExist = true;

	const FString SelectedPath = FEditorFileUtils::OpenFileDialog(Options);
	if (SelectedPath.empty())
	{
		return FString();
	}

	const std::filesystem::path AbsolutePath = std::filesystem::path(FPaths::ToWide(SelectedPath)).lexically_normal();
	if (!IsLuaScriptFile(AbsolutePath))
	{
		return FString();
	}

	return MakeLuaScriptPropertyPath(AbsolutePath);
}

void FEditorPropertyWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	ImGui::SetNextWindowSize(ImVec2(350.0f, 500.0f), ImGuiCond_Once);

	ImGui::Begin("Property Window");

	FSelectionManager& Selection = EditorEngine->GetSelectionManager();
	AActor* PrimaryActor = Selection.GetPrimarySelection();
	if (!PrimaryActor)
	{
		SelectedComponent = nullptr;
		LastSelectedActor = nullptr;
		bActorSelected = true;
		ImGui::Text("No object selected.");
		ImGui::End();
		return;
	}

	// Actor ?좏깮??諛붾뚮㈃ 珥덇린??
	if (PrimaryActor != LastSelectedActor)
	{
		SelectedComponent = nullptr;
		LastSelectedActor = PrimaryActor;
		bActorSelected = true;
	}

	const TArray<AActor*>& SelectedActors = Selection.GetSelectedActors();
	const int32 SelectionCount = static_cast<int32>(SelectedActors.size());

	// ========== 怨좎젙 ?곸뿭: Actor Info (clickable) ==========
	if (SelectionCount > 1)
	{
		ImGui::Text("Class: %s", PrimaryActor->GetClass()->GetName());

		FString PrimaryName = PrimaryActor->GetFName().ToString();
		if (PrimaryName.empty()) PrimaryName = PrimaryActor->GetClass()->GetName();

		bool bHighlight = bActorSelected;
		if (bHighlight) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
		ImGui::Text("Name: %s (+%d)", PrimaryName.c_str(), SelectionCount - 1);
		if (bHighlight) ImGui::PopStyleColor();
		if (ImGui::IsItemClicked())
		{
			bActorSelected = true;
			SelectedComponent = nullptr;
		}
		ImGui::SameLine();
		char RemoveLabel[64];
		snprintf(RemoveLabel, sizeof(RemoveLabel), "Remove %d Objects", SelectionCount);
		if (ImGui::Button(RemoveLabel))
		{
			// ?좏깮 ?댁젣瑜?癒쇱? ?섑뻾 (dangling pointer濡?Proxy ?묎렐 諛⑹?)
			TArray<AActor*> ToDelete(SelectedActors.begin(), SelectedActors.end());
			Selection.ClearSelection();
			for (AActor* Actor : ToDelete)
			{
				if (Actor && Actor->GetWorld())
				{
					Actor->GetWorld()->DestroyActor(Actor);
				}
			}
			// GPU Occlusion staging???⑥? dangling proxy ?ъ씤??臾댄슚??
			EditorEngine->InvalidateOcclusionResults();
			SelectedComponent = nullptr;
			LastSelectedActor = nullptr;
			ImGui::End();
			return;
		}
	}
	else
	{
		ImGui::Text("Class: %s", PrimaryActor->GetClass()->GetName());

		// Actor ?대쫫: ?대┃ 媛?? ?좏깮 ???섏씠?쇱씠??
		bool bHighlight = bActorSelected;
		if (bHighlight) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
		ImGui::Text("Name: %s", PrimaryActor->GetFName().ToString().c_str());
		if (bHighlight) ImGui::PopStyleColor();
		if (ImGui::IsItemClicked())
		{
			bActorSelected = true;
			SelectedComponent = nullptr;
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove"))
		{
			// ?좏깮 ?댁젣瑜?癒쇱? ?섑뻾 (dangling pointer濡?Proxy ?묎렐 諛⑹?)
			AActor* ToDelete = PrimaryActor;
			Selection.ClearSelection();
			if (ToDelete && ToDelete->GetWorld())
			{
				ToDelete->GetWorld()->DestroyActor(ToDelete);
			}
			// GPU Occlusion staging???⑥? dangling proxy ?ъ씤??臾댄슚??
			EditorEngine->InvalidateOcclusionResults();
			SelectedComponent = nullptr;
			LastSelectedActor = nullptr;
			ImGui::End();
			return;
		}

		ImGui::SameLine();
		if (ImGui::Button("Save Prefab"))
		{
			std::wstring PrefabDir = FPaths::PrefabDir();
			FPaths::CreateDir(PrefabDir);

			FString DefaultName = PrimaryActor->GetFName().ToString();
			if (DefaultName.empty()) DefaultName = PrimaryActor->GetClass()->GetName();

			std::wstring WideDefaultName = FPaths::ToWide(DefaultName);

			FEditorFileDialogOptions Options;
			Options.Title = L"Save Prefab As...";
			Options.Filter = L"Prefab Files (*.Prefab)\0*.Prefab\0All Files (*.*)\0*.*\0";
			Options.DefaultExtension = L"Prefab";
			Options.InitialDirectory = PrefabDir.c_str();
			Options.DefaultFileName = WideDefaultName.c_str();
			Options.bPromptOverwrite = true;
			Options.bReturnRelativeToProjectRoot = false;

			FString SavePath = FEditorFileUtils::SaveFileDialog(Options);
			if (!SavePath.empty())
			{
				// Save to the exact path picked in the dialog, then refresh the content browser
				// so newly-created prefabs appear without typing "cb refresh".
				if (FPrefabSaveManager::SaveActorAsPrefab(PrimaryActor, SavePath) && EditorEngine)
				{
					EditorEngine->RefreshContentBrowser();
				}
			}
		}
	}

	// ========== 怨좎젙 ?곸뿭: Component Tree ==========
	RenderComponentTree(PrimaryActor);

	// ========== ?ㅽ겕濡??곸뿭: Details ==========
	SEPARATOR();
	ImGui::Text("Details");
	ImGui::Separator();

	float ScrollHeight = ImGui::GetContentRegionAvail().y;
	if (ScrollHeight < 50.0f) ScrollHeight = 50.0f;

	ImGui::BeginChild("##Details", ImVec2(0, ScrollHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	{
		RenderDetails(PrimaryActor, SelectedActors);
	}
	ImGui::EndChild();

	ImGui::End();
}

void FEditorPropertyWidget::RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	if (bActorSelected)
	{
		RenderActorProperties(PrimaryActor, SelectedActors);
	}
	else if (SelectedComponent && SelectedActors.size() >= 2)
	{
		// ?ㅼ쨷 ?좏깮 ??紐⑤뱺 ?≫꽣????낆씠 ?숈씪?쒖? 寃利?
		UClass* PrimaryClass = PrimaryActor->GetClass();
		bool bAllSameType = true;
		for (const AActor* Actor : SelectedActors)
		{
			if (Actor && Actor->GetClass() != PrimaryClass)
			{
				bAllSameType = false;
				break;
			}
		}

		if (!bAllSameType)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Multi-edit unavailable");
			ImGui::TextWrapped(
				"Selected actors have different types. "
				"Multi-component editing requires all selected actors to be the same type.");

			ImGui::Spacing();
			ImGui::TextDisabled("Primary: %s", PrimaryClass->GetName());
			for (const AActor* Actor : SelectedActors)
			{
				if (Actor && Actor->GetClass() != PrimaryClass)
				{
					ImGui::TextDisabled("  Mismatch: %s (%s)",
						Actor->GetFName().ToString().c_str(),
						Actor->GetClass()->GetName());
				}
			}
		}
		else
		{
			RenderComponentProperties(PrimaryActor, SelectedActors);
		}
	}
	else if (SelectedComponent)
	{
		RenderComponentProperties(PrimaryActor, SelectedActors);
	}
	else
	{
		ImGui::TextDisabled("Select an actor or component to view details.");
	}
}

void FEditorPropertyWidget::RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	ImGui::Text("Actor: %s", PrimaryActor->GetClass()->GetName());
	ImGui::Text("Name: %s", PrimaryActor->GetFName().ToString().c_str());

	if (PrimaryActor->GetRootComponent())
	{
		ImGui::Separator();
		ImGui::Text("Transform");
		ImGui::Spacing();

		FVector Pos = PrimaryActor->GetActorLocation();
		float PosArray[3] = { Pos.X, Pos.Y, Pos.Z };

		USceneComponent* RootComp = PrimaryActor->GetRootComponent();

		FVector Scale = PrimaryActor->GetActorScale();
		float ScaleArray[3] = { Scale.X, Scale.Y, Scale.Z };

		if (ImGui::DragFloat3("Location", PosArray, 0.1f))
		{
			FVector Delta = FVector(PosArray[0], PosArray[1], PosArray[2]) - Pos;
			for (AActor* Actor : SelectedActors)
			{
				if (Actor) Actor->AddActorWorldOffset(Delta);
			}
			EditorEngine->GetGizmo()->UpdateGizmoTransform();
		}
		{
			// Rotation: CachedEditRotator瑜?X=Roll(X異?, Y=Pitch(Y異?, Z=Yaw(Z異?濡??몄텧
			FRotator& CachedRot = RootComp->GetCachedEditRotator();
			FRotator PrevRot = CachedRot;
			float RotXYZ[3] = { CachedRot.Roll, CachedRot.Pitch, CachedRot.Yaw };

			if (ImGui::DragFloat3("Rotation", RotXYZ, 0.1f))
			{
				CachedRot.Roll = RotXYZ[0];
				CachedRot.Pitch = RotXYZ[1];
				CachedRot.Yaw = RotXYZ[2];

				if (SelectedActors.size() > 1)
				{
					FRotator Delta = CachedRot - PrevRot;
					for (AActor* Actor : SelectedActors)
					{
						if (!Actor || Actor == PrimaryActor) continue;
						USceneComponent* Root = Actor->GetRootComponent();
						if (Root)
						{
							FRotator Other = Root->GetCachedEditRotator();
							Root->SetRelativeRotation(Other + Delta);
						}
					}
				}
				RootComp->ApplyCachedEditRotator();
				EditorEngine->GetGizmo()->UpdateGizmoTransform();
			}
		}
		if (ImGui::DragFloat3("Scale", ScaleArray, 0.1f))
		{
			FVector Delta = FVector(ScaleArray[0], ScaleArray[1], ScaleArray[2]) - Scale;
			for (AActor* Actor : SelectedActors)
			{
				if (Actor) Actor->SetActorScale(Actor->GetActorScale() + Delta);
			}
		}


	}

	ImGui::Separator();
	bool bVisible = PrimaryActor->IsVisible();
	if (ImGui::Checkbox("Visible", &bVisible))
	{
		PrimaryActor->SetVisible(bVisible);
	}

	// PlayerController ??Pawn Possess / Active Camera ?곌껐
	if (APlayerController* PC = Cast<APlayerController>(PrimaryActor))
	{
		SEPARATOR();
		ImGui::Text("PlayerController");
		ImGui::Separator();

		ImGui::TextDisabled("Auto: pawn-owned view camera = pawn, separated view camera = camera.");

		AActor* CurActor = PC->GetPossessedActor();
		if (CurActor)
		{
			const FString& N = CurActor->GetFName().ToString();
			ImGui::Text("Possessed: %s", N.empty() ? "Actor" : N.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("UnPossess"))
				PC->UnPossess();
		}
		else
		{
			ImGui::TextDisabled("Possessed: (none)");
		}

		if (ImGui::Button("Possess Actor..."))
			ImGui::OpenPopup("##PawnPicker");
		if (ImGui::BeginPopup("##PawnPicker"))
		{
			UWorld* World = EditorEngine->GetWorld();
			bool bAny = false;
			for (AActor* A : World->GetActors())
			{
				bool bPossessable = Cast<APawn>(A) != nullptr;
				if (!bPossessable)
				{
					for (UActorComponent* Comp : A->GetComponents())
					{
						if (Cast<UPawnMovementComponent>(Comp)) { bPossessable = true; break; }
					}
				}
				if (!bPossessable) continue;
				bAny = true;
				const FString& N = A->GetFName().ToString();
				if (ImGui::MenuItem(N.empty() ? "Actor" : N.c_str()))
				{
					PC->Possess(A);
					ImGui::CloseCurrentPopup();
				}
			}
			if (!bAny) ImGui::TextDisabled("No possessable actors in world");
			ImGui::EndPopup();
		}

		ImGui::Spacing();
		ImGui::Text("Camera");
		ImGui::TextDisabled("Possessing an actor with a CameraComponent selects that camera automatically.");

		if (UCameraComponent* ActiveCamera = PC->GetActiveCamera())
		{
			FString OwnerName = ActiveCamera->GetOwner() ? ActiveCamera->GetOwner()->GetFName().ToString() : FString();
			FString CameraName = ActiveCamera->GetFName().ToString();
			FString Label = OwnerName.empty() ? FString("Actor") : OwnerName;
			Label += ".";
			Label += CameraName.empty() ? ActiveCamera->GetClass()->GetName() : CameraName;
			ImGui::Text("Player Camera: %s", Label.c_str());
		}
		else
		{
			ImGui::TextDisabled("Player Camera: (none)");
		}

		if (ImGui::Button("Choose Player Camera..."))
		{
			ImGui::OpenPopup("##ControllerCameraPicker");
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Player Camera"))
		{
			PC->ClearActiveCamera();
		}

		if (ImGui::BeginPopup("##ControllerCameraPicker"))
		{
			UWorld* World = EditorEngine->GetWorld();
			bool bAnyCamera = false;
			if (World)
			{
				for (AActor* CandidateActor : World->GetActors())
				{
					if (!CandidateActor)
					{
						continue;
					}

					const FString ActorName = CandidateActor->GetFName().ToString();
					for (UActorComponent* CandidateComponent : CandidateActor->GetComponents())
					{
						if (!CandidateComponent || CandidateComponent->IsHiddenInComponentTree())
						{
							continue;
						}

						UCameraComponent* Camera = Cast<UCameraComponent>(CandidateComponent);
						if (!Camera)
						{
							continue;
						}

						bAnyCamera = true;
						FString CameraName = Camera->GetFName().ToString();
						FString Label = ActorName.empty() ? "Actor" : ActorName;
						Label += ".";
						Label += CameraName.empty() ? Camera->GetClass()->GetName() : CameraName;

						if (ImGui::MenuItem(Label.c_str()))
						{
							PC->SetActiveCamera(Camera);
							PC->GetCameraManager().SnapToActiveCamera();
							if (UCameraComponent* ViewCamera = PC->ResolveViewCamera())
							{
								World->SetViewCamera(ViewCamera);
								World->SetActiveCamera(ViewCamera);
							}
							ImGui::CloseCurrentPopup();
						}
					}
				}
			}
			if (!bAnyCamera)
			{
				ImGui::TextDisabled("No CameraComponent in world");
			}
			ImGui::EndPopup();
		}
	}

	// Pawn ???꾩옱 Controller ?쒖떆
	else if (APawn* Pawn = Cast<APawn>(PrimaryActor))
	{
		SEPARATOR();
		ImGui::Text("Pawn");
		ImGui::Separator();
		APlayerController* Ctrl = Pawn->GetController();
		if (Ctrl)
		{
			const FString& N = Ctrl->GetFName().ToString();
			ImGui::Text("Controller: %s", N.empty() ? "PlayerController" : N.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("UnPossess##Pawn"))
			{
				Ctrl->UnPossess();
			}
		}
		else
		{
			ImGui::TextDisabled("Controller: (none)");
		}

		if (ImGui::Button("Possessed By Controller..."))
		{
			ImGui::OpenPopup("##ControllerPickerForPawn");
		}
		if (ImGui::BeginPopup("##ControllerPickerForPawn"))
		{
			UWorld* World = EditorEngine->GetWorld();
			bool bAny = false;
			if (World)
			{
				for (APlayerController* Controller : World->GetPlayerControllers())
				{
					if (!Controller) continue;
					bAny = true;
					const FString& N = Controller->GetFName().ToString();
					if (ImGui::MenuItem(N.empty() ? "PlayerController" : N.c_str()))
					{
						Controller->Possess(Pawn);
						ImGui::CloseCurrentPopup();
					}
				}
			}
			if (!bAny) ImGui::TextDisabled("No PlayerControllers in world");
			ImGui::EndPopup();
		}

		if (ImGui::Button("Create Controller And Possess"))
		{
			if (UWorld* World = EditorEngine->GetWorld())
			{
				if (APlayerController* Controller = World->FindOrCreatePlayerController())
				{
					Controller->Possess(Pawn);
				}
			}
		}

		if (ImGui::Button("Use This Pawn Camera"))
		{
			if (UWorld* World = EditorEngine->GetWorld())
			{
				if (APlayerController* Controller = World->FindOrCreatePlayerController())
				{
					Controller->Possess(Pawn);
					Controller->SetActiveCamera(Pawn->FindPawnCamera());
				}
			}
		}
	}

}

void FEditorPropertyWidget::RenderComponentTree(AActor* Actor)
{
	ImGui::Text("Components");

	if (SelectedComponent && ShouldHideInComponentTree(SelectedComponent, bShowEditorOnlyComponents))
	{
		SelectedComponent = nullptr;
		bActorSelected = true;
	}

	// Get All Component Classes
	TArray<UClass*>& AllClasses = UClass::GetAllClasses();

	TArray<UClass*> ComponentClasses;
	for (UClass* Cls : AllClasses)
	{
		if (Cls->IsA(UActorComponent::StaticClass()) && !Cls->HasAnyClassFlags(CF_HiddenInComponentList))
			ComponentClasses.push_back(Cls);
	}

	std::sort(ComponentClasses.begin(), ComponentClasses.end(),
		[](const UClass* A, const UClass* B)
		{
			return strcmp(A->GetName(), B->GetName()) < 0;
		});

	//?꾨옒 ?대옒?ㅻ뱾濡?而댄룷?뚰듃 由ъ뒪?몃? 遺꾨쪟?⑸땲??
	TArray<FComponentClassGroup> ComponentGroups;
	AddComponentClassGroup(ComponentGroups, "Light", ULightComponentBase::StaticClass());
	AddComponentClassGroup(ComponentGroups, "Movement", UMovementComponent::StaticClass());
	AddComponentClassGroup(ComponentGroups, "Input", UControllerInputComponent::StaticClass());
	AddComponentClassGroup(ComponentGroups, "Orientation", UPawnOrientationComponent::StaticClass());
	AddComponentClassGroup(ComponentGroups, "Collision", UShapeComponent::StaticClass());
	AddComponentClassGroup(ComponentGroups, "Camera", UCameraComponent::StaticClass());
	AddComponentClassGroup(ComponentGroups, "Primitive", UPrimitiveComponent::StaticClass());

	TArray<UClass*> OtherClasses;
	for (UClass* Cls : ComponentClasses)
	{
		UClass* AnchorClass = FindComponentClassGroupAnchor(Cls, ComponentGroups);
		if (!AnchorClass)
		{
			OtherClasses.push_back(Cls);
			continue;
		}

		for (FComponentClassGroup& Group : ComponentGroups)
		{
			if (Group.AnchorClass == AnchorClass)
			{
				Group.Classes.push_back(Cls);
				break;
			}
		}
	}

	for (FComponentClassGroup& Group : ComponentGroups)
	{
		std::sort(Group.Classes.begin(), Group.Classes.end(),
			[](const UClass* A, const UClass* B)
			{
				return strcmp(A->GetName(), B->GetName()) < 0;
			});
	}
	std::sort(OtherClasses.begin(), OtherClasses.end(),
		[](const UClass* A, const UClass* B)
		{
			return strcmp(A->GetName(), B->GetName()) < 0;
		});

	static UClass* SelectedClass = nullptr;
	auto IsCurrentSelectionValid = [&]()
	{
		for (UClass* Cls : ComponentClasses)
		{
			if (Cls == SelectedClass)
			{
				return true;
			}
		}
		return false;
	};

	if (ComponentClasses.empty())
	{
		SelectedClass = nullptr;
	}
	else if (!IsCurrentSelectionValid())
	{
		SelectedClass = ComponentClasses.front();
	}
	const char* Preview = SelectedClass ? SelectedClass->GetName() : "None";

	const ImGuiStyle& Style = ImGui::GetStyle();
	const float ComboWidth = ImGui::GetContentRegionAvail().x;
	const float ComboHeight = ImGui::GetFrameHeight();
	const ImVec2 ComboButtonSize(ComboWidth, ComboHeight);
	if (ImGui::InvisibleButton("##ComponentClassButton", ComboButtonSize))
	{
		ImGui::OpenPopup("##ComponentClassPopup");
	}

	const ImVec2 ComboMin = ImGui::GetItemRectMin();
	const ImVec2 ComboMax = ImGui::GetItemRectMax();
	const bool bHovered = ImGui::IsItemHovered();
	const bool bPopupOpen = ImGui::IsPopupOpen("##ComponentClassPopup");
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	const ImU32 FrameColor = ImGui::GetColorU32((bHovered || bPopupOpen) ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
	const ImU32 BorderColor = ImGui::GetColorU32(ImGuiCol_Border);
	const ImU32 TextColor = ImGui::GetColorU32(ImGuiCol_Text);
	const float ArrowWidth = ComboHeight;
	const ImVec2 ArrowMin(ComboMax.x - ArrowWidth, ComboMin.y);
	const ImVec2 ArrowMax(ComboMax.x, ComboMax.y);
	const float Rounding = Style.FrameRounding;

	DrawList->AddRectFilled(ComboMin, ComboMax, FrameColor, Rounding);
	DrawList->AddRect(ComboMin, ComboMax, BorderColor, Rounding);
	DrawList->AddLine(ImVec2(ArrowMin.x, ArrowMin.y), ImVec2(ArrowMin.x, ArrowMax.y), BorderColor);

	const float ArrowCenterX = (ArrowMin.x + ArrowMax.x) * 0.5f;
	const float ArrowCenterY = (ArrowMin.y + ArrowMax.y) * 0.5f;
	DrawList->AddTriangleFilled(
		ImVec2(ArrowCenterX - 4.0f, ArrowCenterY - 2.0f),
		ImVec2(ArrowCenterX + 4.0f, ArrowCenterY - 2.0f),
		ImVec2(ArrowCenterX, ArrowCenterY + 3.0f),
		TextColor);

	const ImVec2 TextMin(ComboMin.x + Style.FramePadding.x, ComboMin.y + Style.FramePadding.y);
	const ImVec2 TextMax(ArrowMin.x - Style.FramePadding.x, ComboMax.y - Style.FramePadding.y);
	DrawList->PushClipRect(TextMin, TextMax, true);
	DrawList->AddText(TextMin, TextColor, Preview);
	DrawList->PopClipRect();

	ImGui::SetNextWindowPos(ComboMin, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 320.0f), ImVec2(FLT_MAX, 520.0f));
	if (ImGui::BeginPopup("##ComponentClassPopup"))
	{
		auto RenderClassItem = [&](UClass* Cls)
		{
			bool bSelected = (SelectedClass == Cls);
			if (ImGui::Selectable(Cls->GetName(), bSelected))
			{
				SelectedClass = Cls;
				ImGui::CloseCurrentPopup();
			}
			if (bSelected)
				ImGui::SetItemDefaultFocus();
		};

		for (const FComponentClassGroup& Group : ComponentGroups)
		{
			if (Group.Classes.empty())
			{
				continue;
			}

			FString SeparatorLabel = "----";
			SeparatorLabel += Group.Label;
			SeparatorLabel += "----";
			ImGui::TextDisabled("%s", SeparatorLabel.c_str());

			for (UClass* Cls : Group.Classes)
			{
				RenderClassItem(Cls);
			}
		}

		if (!OtherClasses.empty())
		{
			ImGui::TextDisabled("----Other----");
			for (UClass* Cls : OtherClasses)
			{
				RenderClassItem(Cls);
			}
		}
		ImGui::EndPopup();
	}

	USceneComponent* Root = Actor->GetRootComponent();

	// Add Component
	if (SelectedClass && ImGui::Button("Add"))
	{
		UActorComponent* Comp = Actor->AddComponentByClass(SelectedClass);
		if (!Comp)
		{
			return;
		}

		if (SelectedClass->IsA(USceneComponent::StaticClass()))
		{
			USceneComponent* SceneComp = Cast<USceneComponent>(Comp);
			if (!Root && SceneComp)
			{
				Actor->SetRootComponent(SceneComp);
				Root = SceneComp;
			}
			else if (SceneComp)
			{
				if (SelectedComponent != nullptr && SelectedComponent->GetClass()->IsA(USceneComponent::StaticClass()))
					SceneComp->AttachToComponent(Cast<USceneComponent>(SelectedComponent));
				else if (Root)
					SceneComp->AttachToComponent(Root);
			}

			// 鍮뚮낫?쒓? ?꾩슂??而댄룷?뚰듃?ㅼ뿉 ???鍮뚮낫???앹꽦 蹂댁옣
			if (Comp->IsA<ULightComponentBase>())
			{
				Cast<ULightComponentBase>(Comp)->EnsureEditorBillboard();
			}
			else if (Comp->IsA<UDecalComponent>())
			{
				Cast<UDecalComponent>(Comp)->EnsureEditorBillboard();
			}
			else if (Comp->IsA<UHeightFogComponent>())
			{
				Cast<UHeightFogComponent>(Comp)->EnsureEditorBillboard();
			}
		}

		SelectedComponent = Comp;
		bActorSelected = false;
	}

	ImGui::Separator();

	if (Root)
	{
		RenderSceneComponentNode(Root);
	}

	// Non-scene ActorComponents
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp) continue;
		if (Comp->IsA<USceneComponent>()) continue;
		if (ShouldHideInComponentTree(Comp, bShowEditorOnlyComponents)) continue;

		FString Name = Comp->GetFName().ToString();
		const FString TypeName = Comp->GetClass()->GetName();
		const FString DefaultNamePrefix = TypeName + "_";
		const bool bUseTypeAsLabel = Name.empty()
			|| Name == TypeName
			|| Name.rfind(DefaultNamePrefix, 0) == 0;
		const char* Label = bUseTypeAsLabel ? TypeName.c_str() : Name.c_str();

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (!bActorSelected && SelectedComponent == Comp)
			Flags |= ImGuiTreeNodeFlags_Selected;

		ImGui::TreeNodeEx(Comp, Flags, "%s", Label);
		if (ImGui::IsItemClicked())
		{
			SelectedComponent = Comp;
			bActorSelected = false;
		}
	}
}

void FEditorPropertyWidget::RenderSceneComponentNode(USceneComponent* Comp)
{
	if (!Comp) return;
	if (ShouldHideInComponentTree(Comp, bShowEditorOnlyComponents)) return;

	FString Name = Comp->GetFName().ToString();
	if (Name.empty()) Name = Comp->GetClass()->GetName();

	const auto& Children = Comp->GetChildren();
	bool bHasVisibleChildren = false;
	for (USceneComponent* Child : Children)
	{
		if (Child && !ShouldHideInComponentTree(Child, bShowEditorOnlyComponents))
		{
			bHasVisibleChildren = true;
			break;
		}
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (!bHasVisibleChildren)
		Flags |= ImGuiTreeNodeFlags_Leaf;
	if (!bActorSelected && SelectedComponent == Comp)
		Flags |= ImGuiTreeNodeFlags_Selected;

	bool bIsRoot = (Comp->GetParent() == nullptr);
	bool bOpen = ImGui::TreeNodeEx(
		Comp, Flags, "%s%s (%s)",
		bIsRoot ? "[Root] " : "",
		Name.c_str(),
		Comp->GetClass()->GetName()
	);

	if (ImGui::IsItemClicked())
	{
		SelectedComponent = Comp;
		bActorSelected = false;
		EditorEngine->GetSelectionManager().SelectComponent(Comp);
	}

	// 而댄룷?뚰듃 ?몃━?먯꽌 媛꾨떒?섍쾶 ?쒕옒洹????쒕엻?쇰줈 遺紐??먯떇 愿怨?蹂寃?媛?ν븯?꾨줉 吏??
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("SCENE_COMPONENT_REPARENT", &Comp, sizeof(USceneComponent*));
		ImGui::Text("Reparent %s", Name.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_COMPONENT_REPARENT"))
		{
			USceneComponent* DraggedComp = *(USceneComponent**)payload->Data;
			if (DraggedComp && DraggedComp != Comp)
			{
				// Circular dependency check: Ensure Comp is not a child of DraggedComp
				bool bIsChildOfDragged = false;
				USceneComponent* Check = Comp;
				while (Check)
				{
					if (Check == DraggedComp)
					{
						bIsChildOfDragged = true;
						break;
					}
					Check = Check->GetParent();
				}

				if (!bIsChildOfDragged)
				{
					DraggedComp->SetParent(Comp);
					if (EditorEngine && EditorEngine->GetGizmo())
					{
						EditorEngine->GetGizmo()->UpdateGizmoTransform();
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (bOpen)
	{
		for (USceneComponent* Child : Children)
		{
			RenderSceneComponentNode(Child);
		}
		ImGui::TreePop();
	}
}

void FEditorPropertyWidget::RenderComponentProperties(AActor* Actor, const TArray<AActor*>& SelectedActors)
{
	ImGui::Text("Component: %s", SelectedComponent->GetClass()->GetName());
	ImGui::Text("Name: %s", SelectedComponent->GetFName().ToString().c_str());
	ImGui::SameLine();
	if (SelectedComponent != Actor->GetRootComponent())
	{
		if (ImGui::Button("Remove"))
		{
			if (SelectedComponent != nullptr)
			{
				Actor->RemoveComponent(SelectedComponent);
				SelectedComponent = nullptr;
				return;
			}
		}
	}

	ImGui::Separator();

	// CameraComponent ??player view selection is explicit; follow target is handled by the camera settings below.
	if (UCameraComponent* Cam = Cast<UCameraComponent>(SelectedComponent))
	{
		UWorld* World = EditorEngine->GetWorld();
		if (World)
		{
			APlayerController* Controller = World->GetPlayerController(0);
			const bool bIsPlayerCamera = Controller && Controller->GetActiveCamera() == Cam;
			if (bIsPlayerCamera)
			{
				ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Player Camera");
			}
			else
			{
				ImGui::TextDisabled("Not used as the player camera");
			}

			if (ImGui::Button("Use as Player Camera"))
			{
				Cam->SetActiveCamera();
			}

			ImGui::TextDisabled("Follow Target below changes who this camera follows. It is visible once this camera is the player camera.");
		}
		ImGui::Separator();
	}

	// PropertyDescriptor 湲곕컲 ?먮룞 ?꾩젽 ?뚮뜑留?
	TArray<FPropertyDescriptor> Props;
	SelectedComponent->GetEditableProperties(Props);

	bool bIsRoot = false;
	if (SelectedComponent->IsA<USceneComponent>())
	{
		USceneComponent* SceneComp = static_cast<USceneComponent*>(SelectedComponent);
		bIsRoot = (SceneComp->GetParent() == nullptr);
	}

	// Transform ?꾨줈?쇳떚 ?대쫫 紐⑸줉
	auto IsTransformProp = [](const FString& Name) {
		return Name == "Location"
			|| Name == "Rotation"
			|| Name == "Scale";
		};

	bool bAnyChanged = false;

	// Pass 1: Transform ?꾨줈?쇳떚 癒쇱? (Root媛 ?꾨땺 ?뚮쭔)
	if (!bIsRoot)
	{
		for (int32 i = 0; i < (int32)Props.size(); ++i)
		{
			if (IsTransformProp(Props[i].Name))
			{
				if (RenderPropertyWidget(Props, i))
				{
					bAnyChanged = true;
					PropagatePropertyChange(Props[i].Name, SelectedActors);
				}
			}
		}
		ImGui::Separator();
	}

	// Pass 2: ?섎㉧吏 ?꾨줈?쇳떚
	// Pass 2: 나머지 프로퍼티 (Category별 그룹핑)
	std::string LastCategory;
	for (int32 i = 0; i < (int32)Props.size(); ++i)
	{
		if (IsTransformProp(Props[i].Name))
			continue;
		if (Props[i].Flags & EPF_Hidden)
			continue;

		// 카테고리가 바뀌면 구분선 + 레이블 출력
		const std::string& Cat = Props[i].Category;
		if (Cat != LastCategory)
		{
			ImGui::SeparatorText(Cat.c_str());
			LastCategory = Cat;
		}

		bool bChanged = RenderPropertyWidget(Props, i);
		if (bChanged)
		{
			bAnyChanged = true;
			PropagatePropertyChange(Props[i].Name, SelectedActors);

			if (IsObjectRefType(Props[i], UStaticMesh::StaticClass()) || IsObjectRefType(Props[i], USkeletalMesh::StaticClass()))
				break;
		}
	}

	// ?ㅼ젣 蹂寃쎌씠 ?덉뿀???뚮쭔 Transform dirty 留덊궧
	if (bAnyChanged && SelectedComponent->IsA<USceneComponent>())
	{
		static_cast<USceneComponent*>(SelectedComponent)->MarkTransformDirty();
	}
}

void FEditorPropertyWidget::PropagatePropertyChange(const FString& PropName, const TArray<AActor*>& SelectedActors)
{
	if (!SelectedComponent || SelectedActors.size() < 2) return;

	UClass* CompClass = SelectedComponent->GetClass();
	AActor* PrimaryActor = SelectedActors[0];

	// Primary 而댄룷?뚰듃?먯꽌 蹂寃쎈맂 ?꾨줈?쇳떚??媛??ъ씤??李얘린
	TArray<FPropertyDescriptor> SrcProps;
	SelectedComponent->GetEditableProperties(SrcProps);

	const FPropertyDescriptor* SrcProp = nullptr;
	for (const auto& P : SrcProps)
	{
		if (P.Name == PropName) { SrcProp = &P; break; }
	}
	if (!SrcProp) return;

	for (AActor* Actor : SelectedActors)
	{
		if (!Actor || Actor == PrimaryActor) continue;

		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (!Comp || Comp->GetClass() != CompClass) continue;

			TArray<FPropertyDescriptor> DstProps;
			Comp->GetEditableProperties(DstProps);

			for (auto& DstProp : DstProps)
			{
				if (DstProp.Name != PropName || !ArePropertyTypesCompatible(DstProp.TypeDesc, SrcProp->TypeDesc)) continue;

				if (!CopyPropertyValueRecursive(*SrcProp, DstProp))
				{
					continue;
				}

				Comp->PostEditProperty(PropName.c_str());
				break;
			}
			break; // 媛숈? ??낆쓽 泥?踰덉㎏ 而댄룷?뚰듃?먮쭔 ?꾪뙆
		}
	}
}

bool FEditorPropertyWidget::RenderPropertyWidget(TArray<FPropertyDescriptor>& Props, int32& Index, const char* PostEditPropertyName)
{
	ImGui::PushID(Index);
	FPropertyDescriptor& Prop = Props[Index];

	// Hidden 프로퍼티는 렌더링 자체를 건너뜀
	if (Prop.Flags & EPF_Hidden)
	{
		ImGui::PopID();
		return false;
	}

	const char* EffectivePostEditPropertyName = PostEditPropertyName ? PostEditPropertyName : Prop.Name.c_str();
	bool bChanged = false;

	const bool bReadOnly = (Prop.Flags & EPF_ReadOnly) != 0;
	if (bReadOnly) ImGui::BeginDisabled(true);

	switch (Prop.GetKind())
	{
	case EPropertyType::Bool:
	{
		bool* Val = static_cast<bool*>(Prop.ValuePtr);
		bChanged = ImGui::Checkbox(Prop.Name.c_str(), Val);
		break;
	}
	case EPropertyType::ByteBool:
	{
		uint8* Val = static_cast<uint8*>(Prop.ValuePtr);
		bool bVal = (*Val != 0);
		if (ImGui::Checkbox(Prop.Name.c_str(), &bVal))
		{
			*Val = bVal ? 1 : 0;
			bChanged = true;
		}
		break;
	}
	case EPropertyType::Int:
	{
		int32* Val = static_cast<int32*>(Prop.ValuePtr);
		if (Prop.Min != 0.0f || Prop.Max != 0.0f)
			bChanged = ImGui::DragInt(Prop.Name.c_str(), Val, (int32)Prop.Speed, (int32)Prop.Min, (int32)Prop.Max);
		else
			bChanged = ImGui::DragInt(Prop.Name.c_str(), Val, (int32)Prop.Speed);
		break;
	}
	case EPropertyType::Float:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		if (Prop.Min != 0.0f || Prop.Max != 0.0f)
			bChanged = ImGui::DragFloat(Prop.Name.c_str(), Val, Prop.Speed, Prop.Min, Prop.Max, "%.4f");
		else
			bChanged = ImGui::DragFloat(Prop.Name.c_str(), Val, Prop.Speed);
		break;
	}
	case EPropertyType::Vec3:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::DragFloat3(Prop.Name.c_str(), Val, Prop.Speed);
		break;
	}
	case EPropertyType::Rotator:
	{
		// FRotator 硫붾え由??덉씠?꾩썐 [Pitch,Yaw,Roll] ??UI X=Roll(X異?, Y=Pitch(Y異?, Z=Yaw(Z異?
		FRotator* Rot = static_cast<FRotator*>(Prop.ValuePtr);
		float RotXYZ[3] = { Rot->Roll, Rot->Pitch, Rot->Yaw };
		bChanged = ImGui::DragFloat3(Prop.Name.c_str(), RotXYZ, Prop.Speed);
		if (bChanged)
		{
			Rot->Roll = RotXYZ[0];
			Rot->Pitch = RotXYZ[1];
			Rot->Yaw = RotXYZ[2];
			if (SelectedComponent && SelectedComponent->IsA<USceneComponent>())
			{
				static_cast<USceneComponent*>(SelectedComponent)->ApplyCachedEditRotator();
			}
		}
		break;
	}
	case EPropertyType::Vec4:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::DragFloat4(Prop.Name.c_str(), Val, Prop.Speed);
		break;
	}
	case EPropertyType::Color4:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::ColorEdit4(Prop.Name.c_str(), Val);
		break;
	}
	case EPropertyType::String:
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);
		if (Prop.Name == "ScriptPath")
		{
			ImGui::Text("%s", Prop.Name.c_str());
			ImGui::SameLine(120);

			const float ButtonWidth = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float ClearWidth = ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			const float Spacing = ImGui::GetStyle().ItemSpacing.x;
			ImGui::SetNextItemWidth(-(ButtonWidth + ClearWidth + Spacing * 2.0f));

			char Buf[512];
			strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
			ImGui::InputText("##ScriptPath", Buf, sizeof(Buf), ImGuiInputTextFlags_ReadOnly);
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("LuaScriptContentItem"))
				{
					const FContentItem* Item = reinterpret_cast<const FContentItem*>(Payload->Data);
					const std::filesystem::path AbsPath = std::filesystem::path(Item->Path).lexically_normal();
					if (IsLuaScriptFile(AbsPath))
					{
						*Val = MakeLuaScriptPropertyPath(AbsPath);
						bChanged = true;
					}
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::SameLine();
			if (ImGui::Button("..."))
			{
				FString LuaPath = OpenLuaScriptFileDialog();
				if (!LuaPath.empty())
				{
					*Val = LuaPath;
					bChanged = true;
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Select .lua file");
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear"))
			{
				Val->clear();
				bChanged = true;
			}
		}
		else
		{
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
			if (ImGui::InputText(Prop.Name.c_str(), Buf, sizeof(Buf)))
			{
				*Val = Buf;
				bChanged = true;
			}
		}
		break;
	}
	case EPropertyType::ObjectRef:
	{
		if (IsObjectRefType(Prop, UStaticMesh::StaticClass()))
		{
			bChanged = RenderStaticMeshObjectRefWidget(Prop, bChanged, &FEditorPropertyWidget::OpenObjFileDialog);
		}
		else if (IsObjectRefType(Prop, USkeletalMesh::StaticClass()))
		{
			bChanged = RenderSkeletalMeshObjectRefWidget(Prop, bChanged);
		}
		else if (IsObjectRefType(Prop, USceneComponent::StaticClass()))
		{
			bChanged = RenderSceneComponentObjectRefWidget(Prop, SelectedComponent, bChanged);
		}
		else
		{
			bChanged = RenderGenericObjectRefWidget(Prop, bChanged);
		}
		break;
	}
	case EPropertyType::MaterialSlot:
	{
		FMaterialSlot* Slot = static_cast<FMaterialSlot*>(Prop.ValuePtr);
		int32          ElemIdx = (strncmp(Prop.Name.c_str(), "Element ", 8) == 0) ? atoi(&Prop.Name[8]) : -1;

		FString SlotName = "None";
		if (ElemIdx != -1 && SelectedComponent && SelectedComponent->IsA<UMeshComponent>())
		{
			UMeshComponent* MeshComp = static_cast<UMeshComponent*>(SelectedComponent);
			SlotName = MeshComp->GetMaterialSlotName(ElemIdx);
		}

		// 醫뚯륫: Element ?몃뜳??+ ?щ’ ?대쫫
		ImGui::BeginGroup();
		ImGui::Text("Element %d", ElemIdx);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::TextUnformatted(SlotName.c_str());
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", SlotName.c_str());
		ImGui::EndGroup();

		ImGui::SameLine(120);

		// ?곗륫: Material 肄ㅻ낫
		ImGui::BeginGroup();
		ImGui::SetNextItemWidth(-1);

		FString Preview = (Slot->Path.empty() || Slot->Path == "None") ? "None" : Slot->Path;
		if (ImGui::BeginCombo("##Mat", Preview.c_str()))
		{
			// "None" ?좏깮吏 湲곕낯 ?쒓났
			bool bSelectedNone = (Slot->Path == "None" || Slot->Path.empty());
			if (ImGui::Selectable("None", bSelectedNone))
			{
				Slot->Path = "None";
				bChanged = true;
			}
			if (bSelectedNone) ImGui::SetItemDefaultFocus();

			// TObjectIterator ???FMaterialManager ?뚯씪 紐⑸줉 ?ㅼ틪 ?곗씠???ъ슜
			const TArray<FMaterialAssetListItem>& MatFiles = FMaterialManager::Get().GetAvailableMaterialFiles();
			for (const FMaterialAssetListItem& Item : MatFiles)
			{
				bool bSelected = (Slot->Path == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					Slot->Path = Item.FullPath; // ?곗씠?곕뒗 ?꾩껜 寃쎈줈濡????
					bChanged = true;
				}
				if (bSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MaterialContentItem"))
			{
				FContentItem ContentItem = *reinterpret_cast<const FContentItem*>(payload->Data);
				Slot->Path = FPaths::ToUtf8(
					ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring()
				);
				bChanged = true;
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::EndGroup();
		break;
	}
	case EPropertyType::Name:
	{
		FName* Val = static_cast<FName*>(Prop.ValuePtr);
		FString Current = Val->ToString();

		// 由ъ냼???ㅼ? 留ㅼ묶?섎뒗 ?꾨줈?쇳떚硫?肄ㅻ낫 諛뺤뒪濡??뚮뜑留?
		TArray<FString> Names;
		if (strcmp(Prop.Name.c_str(), "Font") == 0)
			Names = FResourceManager::Get().GetFontNames();
		else if (strcmp(Prop.Name.c_str(), "Particle") == 0)
			Names = FResourceManager::Get().GetParticleNames();
		else if (strcmp(Prop.Name.c_str(), "Texture") == 0)
			Names = FResourceManager::Get().GetTextureNames();

		if (!Names.empty())
		{
			if (ImGui::BeginCombo(Prop.Name.c_str(), Current.c_str()))
			{
				for (const auto& Name : Names)
				{
					bool bSelected = (Current == Name);
					if (ImGui::Selectable(Name.c_str(), bSelected))
					{
						*Val = FName(Name);
						bChanged = true;
					}
					if (bSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		else
		{
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Current.c_str(), _TRUNCATE);
			if (ImGui::InputText(Prop.Name.c_str(), Buf, sizeof(Buf)))
			{
				*Val = FName(Buf);
				bChanged = true;
			}
		}
		break;
	}
	case EPropertyType::Enum:
	{
		if (!Prop.GetEnumNames() || Prop.GetEnumCount() == 0) break;
		int32* Val = static_cast<int32*>(Prop.ValuePtr);
		const char* Preview = ((uint32)*Val < Prop.GetEnumCount()) ? Prop.GetEnumNames()[*Val] : "Unknown";
		if (ImGui::BeginCombo(Prop.Name.c_str(), Preview))
		{
			for (uint32 i = 0; i < Prop.GetEnumCount(); ++i)
			{
				bool bSelected = (*Val == (int32)i);
				if (ImGui::Selectable(Prop.GetEnumNames()[i], bSelected))
				{
					*Val = (int32)i;
					bChanged = true;
				}
				if (bSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		break;
	}
	case EPropertyType::Array:
	{
		if (!Prop.GetArraySizeGetter() || !Prop.GetArrayResizeFunc() || !Prop.GetArrayElementGetter())
		{
			ImGui::TextDisabled("%s (unsupported array metadata)", Prop.Name.c_str());
			break;
		}

		ImGui::TextUnformatted(Prop.Name.c_str());

		const size_t Count = Prop.GetArraySizeGetter()(Prop.ValuePtr);
		int32 RemoveIdx = -1;
		for (size_t ElementIndex = 0; ElementIndex < Count; ++ElementIndex)
		{
			FPropertyDescriptor ElementProp;
			if (!BuildArrayElementDescriptor(Prop, ElementIndex, ElementProp))
			{
				continue;
			}

			ImGui::PushID(static_cast<int>(ElementIndex));
			TArray<FPropertyDescriptor> ElementProps = { ElementProp };
			int32 ElementRenderIndex = 0;
			if (RenderPropertyWidget(ElementProps, ElementRenderIndex, EffectivePostEditPropertyName))
			{
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("x"))
			{
				RemoveIdx = static_cast<int32>(ElementIndex);
			}
			ImGui::PopID();
		}

		if (RemoveIdx >= 0)
		{
			const size_t NewCount = Count > 0 ? Count - 1 : 0;
			for (size_t ElementIndex = static_cast<size_t>(RemoveIdx); ElementIndex + 1 < Count; ++ElementIndex)
			{
				FPropertyDescriptor SrcElement;
				FPropertyDescriptor DstElement;
				if (BuildArrayElementDescriptor(Prop, ElementIndex + 1, SrcElement)
					&& BuildArrayElementDescriptor(Prop, ElementIndex, DstElement))
				{
					CopyPropertyValueRecursive(SrcElement, DstElement);
				}
			}
			Prop.GetArrayResizeFunc()(Prop.ValuePtr, NewCount);
			bChanged = true;
		}

		if (ImGui::Button("+ Add Element"))
		{
			Prop.GetArrayResizeFunc()(Prop.ValuePtr, Count + 1);
			bChanged = true;
		}
		break;
	}
	case EPropertyType::Struct:
	{
		const char* Label = Prop.Name.c_str();
		if (ImGui::TreeNodeEx(Label, ImGuiTreeNodeFlags_DefaultOpen))
		{
			TArray<FPropertyDescriptor> ChildProps;
			CollectReflectedStructProperties(Prop.GetStructType(), Prop.ValuePtr, ChildProps);
			for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(ChildProps.size()); ++ChildIndex)
			{
				if (RenderPropertyWidget(ChildProps, ChildIndex, EffectivePostEditPropertyName))
				{
					bChanged = true;
				}
			}
			ImGui::TreePop();
		}
		break;
	}
	case EPropertyType::Set:
	{
		if (!Prop.TypeDesc || !Prop.TypeDesc->SetSnapshotFunc
			|| !Prop.TypeDesc->SetRemoveFunc || !Prop.TypeDesc->SetInsertFunc
			|| !Prop.TypeDesc->SetElementSizeFunc || !Prop.TypeDesc->ElementType)
		{
			ImGui::TextDisabled("%s (unsupported set metadata)", Prop.Name.c_str());
			break;
		}

		if (!ImGui::TreeNodeEx(Prop.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			break;

		TArray<void*> Snapshot;
		Prop.TypeDesc->SetSnapshotFunc(Prop.ValuePtr, Snapshot);
		const size_t Count = Snapshot.size();

		// Render existing elements with Remove button
		const size_t ElemSz = Prop.TypeDesc->SetElementSizeFunc();
		for (size_t i = 0; i < Count; ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			FPropertyDescriptor ElemDesc;
			ElemDesc.TypeDesc = Prop.TypeDesc->ElementType;
			ElemDesc.ValuePtr = Snapshot[i];
			TArray<FPropertyDescriptor> EP = { ElemDesc };
			int32 Idx = 0;
			if (RenderPropertyWidget(EP, Idx, EffectivePostEditPropertyName))
			{
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("x"))
			{
				Prop.TypeDesc->SetRemoveFunc(Prop.ValuePtr, Snapshot[i]);
				bChanged = true;
			}
			ImGui::PopID();
		}

		// Add new element (zero-initialized)
		if (ImGui::Button("+ Add Element"))
		{
			TArray<uint8_t> Buf(ElemSz, 0);
			Prop.TypeDesc->SetInsertFunc(Prop.ValuePtr, Buf.data());
			bChanged = true;
		}

		ImGui::TreePop();
		break;
	}

	case EPropertyType::Map:
	{
		if (!Prop.TypeDesc || !Prop.TypeDesc->MapSnapshotFunc
			|| !Prop.TypeDesc->MapClearFunc
			|| !Prop.TypeDesc->MapKeySizeFunc || !Prop.TypeDesc->MapValueSizeFunc
			|| !Prop.TypeDesc->KeyType || !Prop.TypeDesc->ValueType)
		{
			ImGui::TextDisabled("%s (unsupported map metadata)", Prop.Name.c_str());
			break;
		}

		if (!ImGui::TreeNodeEx(Prop.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			break;

		TArray<void*> Keys, Vals;
		Prop.TypeDesc->MapSnapshotFunc(Prop.ValuePtr, Keys, Vals);

		bool bMapChanged = false;
		int32 RemoveIdx = -1;
		for (size_t i = 0; i < Keys.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			FPropertyDescriptor KDesc, VDesc;
			KDesc.TypeDesc = Prop.TypeDesc->KeyType;
			KDesc.ValuePtr = Keys[i];
			VDesc.TypeDesc = Prop.TypeDesc->ValueType;
			VDesc.ValuePtr = Vals[i];

			// x remove button
			if (Prop.TypeDesc->MapRemoveFunc)
			{
				if (ImGui::SmallButton("x"))
					RemoveIdx = static_cast<int32>(i);
				ImGui::SameLine();
			}

			// Render key (read-only) and value (editable)
			ImGui::BeginDisabled(true);
			TArray<FPropertyDescriptor> KP = { KDesc };
			int32 KIdx = 0;
			RenderPropertyWidget(KP, KIdx, "");
			ImGui::EndDisabled();

			ImGui::SameLine();
			TArray<FPropertyDescriptor> VP = { VDesc };
			int32 VIdx = 0;
			if (RenderPropertyWidget(VP, VIdx, EffectivePostEditPropertyName))
				bMapChanged = true;

			ImGui::PopID();
		}

		// Remove after iteration to avoid invalidating snapshot pointers mid-loop
		if (RemoveIdx >= 0 && RemoveIdx < static_cast<int32>(Keys.size()))
		{
			Prop.TypeDesc->MapRemoveFunc(Prop.ValuePtr, Keys[RemoveIdx]);
			bMapChanged = true;
		}

		// Add new zero-initialized entry
		if (Prop.TypeDesc->MapInsertFunc
			&& Prop.TypeDesc->MapKeySizeFunc && Prop.TypeDesc->MapValueSizeFunc)
		{
			if (ImGui::Button("+ Add Row"))
			{
				const size_t KeySz = Prop.TypeDesc->MapKeySizeFunc();
				const size_t ValSz = Prop.TypeDesc->MapValueSizeFunc();
				TArray<uint8_t> KeyBuf(KeySz, 0), ValBuf(ValSz, 0);
				Prop.TypeDesc->MapInsertFunc(Prop.ValuePtr, KeyBuf.data(), ValBuf.data());
				bMapChanged = true;
			}
		}

		if (bMapChanged)
			bChanged = true;

		ImGui::TreePop();
		break;
	}

	case EPropertyType::ActorRef:
	{
		uint32* ActorUUID = static_cast<uint32*>(Prop.ValuePtr);
		UWorld* World = EditorEngine ? EditorEngine->GetWorld() : nullptr;
		FString Preview = "None";
		if (World && ActorUUID && *ActorUUID != 0)
		{
			if (AActor* Actor = World->FindActorByUUIDInWorld(*ActorUUID))
			{
				Preview = Actor->GetFName().ToString();
				if (Preview.empty())
				{
					Preview = Actor->GetClass()->GetName();
				}
			}
			else
			{
				Preview = "Missing Actor";
			}
		}

		if (ImGui::BeginCombo(Prop.Name.c_str(), Preview.c_str()))
		{
			const bool bSelectedNone = (!ActorUUID || *ActorUUID == 0);
			if (ImGui::Selectable("None", bSelectedNone))
			{
				if (ActorUUID) *ActorUUID = 0;
				bChanged = true;
			}

			if (World)
			{
				for (AActor* Actor : World->GetActors())
				{
					if (!Actor) continue;
					FString Label = Actor->GetFName().ToString();
					if (Label.empty()) Label = Actor->GetClass()->GetName();
					Label += " (";
					Label += Actor->GetClass()->GetName();
					Label += ")";
					const bool bSelected = ActorUUID && *ActorUUID == Actor->GetUUID();
					if (ImGui::Selectable(Label.c_str(), bSelected))
					{
						if (ActorUUID) *ActorUUID = Actor->GetUUID();
						bChanged = true;
					}
					if (bSelected) ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		break;
	}
	}

	if (bReadOnly) ImGui::EndDisabled();

	// Tooltip: 마우스 호버 시 표시
	if (!Prop.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("%s", Prop.Tooltip.c_str());

	if (bChanged && SelectedComponent)
	{
		SelectedComponent->PostEditProperty(EffectivePostEditPropertyName);
	}

	ImGui::PopID();
	return bChanged;
}

