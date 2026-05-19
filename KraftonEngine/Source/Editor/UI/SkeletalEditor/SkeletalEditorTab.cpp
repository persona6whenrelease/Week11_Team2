#include "Editor/UI/SkeletalEditor/SkeletalEditorTab.h"

#include "Editor/Settings/EditorSettings.h"
#include "Editor/Viewport/SkeletalMeshViewerViewportClient.h"
#include "Editor/EditorEngine.h"
#include "Component/CameraComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/SkeletalGizmoComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Engine/Input/InputFrame.h"
#include "Engine/Input/InputSystem.h"
#include "Engine/UI/ImGui/ImGuiViewportPresenter.h"
#include "GameFramework/World.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "Viewport/Viewport.h"
#include "Render/Pipeline/Renderer.h"
#include "Render/Types/ViewTypes.h"
#include "Runtime/Engine.h"
#include "Core/ProjectSettings.h"
#include "Platform/Paths.h"
#include "Debug/DrawDebugHelpers.h"
#include "WICTextureLoader.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace
{
	// ===== Mode bar 아이콘 (현재 탭 종류 강조 + 다른 모드로 점프) =====
	const wchar_t* GetModeBarIconFileName(EModeBarIcon Icon)
	{
		switch (Icon)
		{
		case EModeBarIcon::SkeletalMesh: return L"SkeletalMesh.png";
		case EModeBarIcon::AnimSequence: return L"Animation.png";
		default: return L"";
		}
	}

	ID3D11ShaderResourceView** GetModeBarIconTable()
	{
		static ID3D11ShaderResourceView* Icons[static_cast<int32>(EModeBarIcon::Count)] = {};
		return Icons;
	}

	bool bModeBarIconsLoaded = false;

	void EnsureModeBarIconsLoaded()
	{
		if (bModeBarIconsLoaded || !GEngine) return;
		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		if (!Device) return;

		ID3D11ShaderResourceView** Icons = GetModeBarIconTable();
		const std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/UEIcons/");
		for (int32 i = 0; i < static_cast<int32>(EModeBarIcon::Count); ++i)
		{
			const std::wstring FilePath = IconDir + GetModeBarIconFileName(static_cast<EModeBarIcon>(i));
			DirectX::CreateWICTextureFromFile(Device, FilePath.c_str(), nullptr, &Icons[i]);
		}
		bModeBarIconsLoaded = true;
	}

	bool DrawModeBarButton(const char* Id, EModeBarIcon Icon, bool bActive, bool bEnabled, const char* FallbackLabel)
	{
		constexpr float IconSize = 22.0f;
		ID3D11ShaderResourceView* SRV = GetModeBarIconTable()[static_cast<int32>(Icon)];

		if (!bEnabled) ImGui::BeginDisabled();
		if (bActive)
		{
			ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.95f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.15f, 0.38f, 0.78f, 1.0f));
		}

		bool bClicked = false;
		if (SRV)
		{
			bClicked = ImGui::ImageButton(Id, reinterpret_cast<ImTextureID>(SRV), ImVec2(IconSize, IconSize));
		}
		else
		{
			bClicked = ImGui::Button(FallbackLabel);
		}

		if (bActive) ImGui::PopStyleColor(3);
		if (!bEnabled) ImGui::EndDisabled();
		return bClicked;
	}

	constexpr float BoneDebugJointRadiusScale = 0.005f;
	const FColor SelectedBoneDebugColor(255, 120, 0);

	void DrawDebugOctahedralBone(UWorld* World, const FVector& Head, const FVector& Tail, const FColor& Color)
	{
		FVector Dir = Tail - Head;
		float Length = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);

		if (Length < 0.001f) return;

		Dir.X /= Length;
		Dir.Y /= Length;
		Dir.Z /= Length;

		float OffsetRatio = 0.2f;
		float BoneThickness = Length * 0.05f;

		FVector MidPos = Head + FVector(Dir.X * Length * OffsetRatio, Dir.Y * Length * OffsetRatio, Dir.Z * Length * OffsetRatio);

		FVector ArbitraryUp = (std::abs(Dir.Z) > 0.99f) ? FVector(1.0f, 0.0f, 0.0f) : FVector(0.0f, 0.0f, 1.0f);

		FVector Right(
			ArbitraryUp.Y * Dir.Z - ArbitraryUp.Z * Dir.Y,
			ArbitraryUp.Z * Dir.X - ArbitraryUp.X * Dir.Z,
			ArbitraryUp.X * Dir.Y - ArbitraryUp.Y * Dir.X
		);
		float RightLen = std::sqrt(Right.X * Right.X + Right.Y * Right.Y + Right.Z * Right.Z);
		Right.X /= RightLen; Right.Y /= RightLen; Right.Z /= RightLen;

		FVector Up(
			Dir.Y * Right.Z - Dir.Z * Right.Y,
			Dir.Z * Right.X - Dir.X * Right.Z,
			Dir.X * Right.Y - Dir.Y * Right.X
		);

		FVector P1 = MidPos + FVector(Right.X * BoneThickness, Right.Y * BoneThickness, Right.Z * BoneThickness);
		FVector P2 = MidPos + FVector(Up.X * BoneThickness, Up.Y * BoneThickness, Up.Z * BoneThickness);
		FVector P3 = MidPos - FVector(Right.X * BoneThickness, Right.Y * BoneThickness, Right.Z * BoneThickness);
		FVector P4 = MidPos - FVector(Up.X * BoneThickness, Up.Y * BoneThickness, Up.Z * BoneThickness);

		DrawDebugNodepthLine(World, P1, P2, Color);
		DrawDebugNodepthLine(World, P2, P3, Color);
		DrawDebugNodepthLine(World, P3, P4, Color);
		DrawDebugNodepthLine(World, P4, P1, Color);

		DrawDebugNodepthLine(World, Head, P1, Color);
		DrawDebugNodepthLine(World, Head, P2, Color);
		DrawDebugNodepthLine(World, Head, P3, Color);
		DrawDebugNodepthLine(World, Head, P4, Color);

		DrawDebugNodepthLine(World, Tail, P1, Color);
		DrawDebugNodepthLine(World, Tail, P2, Color);
		DrawDebugNodepthLine(World, Tail, P3, Color);
		DrawDebugNodepthLine(World, Tail, P4, Color);
	}

	enum class EViewerToolbarIcon : int32
	{
		Setting = 0,
		ShowFlag,
		Translate,
		Rotate,
		Scale,
		WorldSpace,
		LocalSpace,
		TranslateSnap,
		RotateSnap,
		ScaleSnap,
		Count
	};

	const wchar_t* GetViewerToolbarIconFileName(EViewerToolbarIcon Icon)
	{
		switch (Icon)
		{
		case EViewerToolbarIcon::Setting: return L"Setting.png";
		case EViewerToolbarIcon::ShowFlag: return L"Show_Flag.png";
		case EViewerToolbarIcon::Translate: return L"Translate.png";
		case EViewerToolbarIcon::Rotate: return L"Rotate.png";
		case EViewerToolbarIcon::Scale: return L"Scale.png";
		case EViewerToolbarIcon::WorldSpace: return L"WorldSpace.png";
		case EViewerToolbarIcon::LocalSpace: return L"LocalSpace.png";
		case EViewerToolbarIcon::TranslateSnap: return L"Translate_Snap.png";
		case EViewerToolbarIcon::RotateSnap: return L"Rotate_Snap.png";
		case EViewerToolbarIcon::ScaleSnap: return L"Scale_Snap.png";
		default: return L"";
		}
	}

	ID3D11ShaderResourceView** GetViewerToolbarIconTable()
	{
		static ID3D11ShaderResourceView* Icons[static_cast<int32>(EViewerToolbarIcon::Count)] = {};
		return Icons;
	}

	bool bViewerToolbarIconsLoaded = false;

	void EnsureViewerToolbarIconsLoaded()
	{
		if (bViewerToolbarIconsLoaded || !GEngine)
		{
			return;
		}

		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		if (!Device)
		{
			return;
		}

		ID3D11ShaderResourceView** Icons = GetViewerToolbarIconTable();
		const std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/ToolIcons/");
		for (int32 i = 0; i < static_cast<int32>(EViewerToolbarIcon::Count); ++i)
		{
			const std::wstring FilePath = IconDir + GetViewerToolbarIconFileName(static_cast<EViewerToolbarIcon>(i));
			DirectX::CreateWICTextureFromFile(Device, FilePath.c_str(), nullptr, &Icons[i]);
		}

		bViewerToolbarIconsLoaded = true;
	}

	bool DrawViewerToolbarIconButton(const char* Id, EViewerToolbarIcon Icon, const char* FallbackLabel)
	{
		constexpr float IconSize = 16.0f;
		ID3D11ShaderResourceView* IconSRV = GetViewerToolbarIconTable()[static_cast<int32>(Icon)];
		if (!IconSRV)
		{
			return ImGui::Button(FallbackLabel);
		}

		return ImGui::ImageButton(Id, reinterpret_cast<ImTextureID>(IconSRV), ImVec2(IconSize, IconSize));
	}

	float CalcViewerIconButtonWidth()
	{
		return 16.0f + ImGui::GetStyle().FramePadding.x * 2.0f;
	}

	float CalcViewerTextButtonWidth(const char* Label)
	{
		return ImGui::CalcTextSize(Label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	}

	void RenderViewerTransformToolbar(FSkeletalMeshViewerViewportClient* PreviewClient, UEditorEngine* EditorEngine)
	{
		constexpr float ButtonSpacing = 4.0f;
		constexpr float GroupSpacing = 12.0f;

		UGizmoComponent* Gizmo = PreviewClient ? PreviewClient->GetBoneSelectionManager().GetGizmo() : nullptr;

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.3f));

		auto DrawGizmoIcon = [&](const char* Id, EViewerToolbarIcon Icon, EGizmoMode TargetMode, const char* FallbackLabel) -> bool
			{
				const bool bSelected = Gizmo && Gizmo->GetMode() == TargetMode;
				if (bSelected)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
				}
				const bool bClicked = DrawViewerToolbarIconButton(Id, Icon, FallbackLabel);
				if (bSelected)
				{
					ImGui::PopStyleColor();
				}
				return bClicked;
			};

		if (!Gizmo)
		{
			ImGui::BeginDisabled();
		}
		if (DrawGizmoIcon("##ViewerTranslateToolIcon", EViewerToolbarIcon::Translate, EGizmoMode::Translate, "Translate") && Gizmo)
		{
			Gizmo->SetTranslateMode();
		}
		ImGui::SameLine(0.0f, ButtonSpacing);
		if (DrawGizmoIcon("##ViewerRotateToolIcon", EViewerToolbarIcon::Rotate, EGizmoMode::Rotate, "Rotate") && Gizmo)
		{
			Gizmo->SetRotateMode();
		}
		ImGui::SameLine(0.0f, ButtonSpacing);
		if (DrawGizmoIcon("##ViewerScaleToolIcon", EViewerToolbarIcon::Scale, EGizmoMode::Scale, "Scale") && Gizmo)
		{
			Gizmo->SetScaleMode();
		}
		if (!Gizmo)
		{
			ImGui::EndDisabled();
		}

		ImGui::PopStyleColor(3);

		FEditorSettings& Settings = FEditorSettings::Get();

		ImGui::SameLine(0.0f, GroupSpacing);
		const bool bWorldCoord = Settings.CoordSystem == EEditorCoordSystem::World;
		if (bWorldCoord)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
		}
		if (DrawViewerToolbarIconButton(
			"##ViewerCoordSystemIcon",
			bWorldCoord ? EViewerToolbarIcon::WorldSpace : EViewerToolbarIcon::LocalSpace,
			bWorldCoord ? "World" : "Local"))
		{
			if (EditorEngine)
			{
				EditorEngine->ToggleCoordSystem();
			}
			else
			{
				Settings.CoordSystem = bWorldCoord ? EEditorCoordSystem::Local : EEditorCoordSystem::World;
			}
			if (EditorEngine)
			{
				EditorEngine->ApplyTransformSettingsToGizmo(Gizmo);
			}
		}
		if (bWorldCoord)
		{
			ImGui::PopStyleColor();
		}

		bool bSnapChanged = false;
		auto DrawSnapControl = [&](const char* Id, EViewerToolbarIcon Icon, const char* FallbackLabel, bool& bEnabled, float& Value, float MinValue)
			{
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::PushID(Id);
				const bool bWasEnabled = bEnabled;
				if (bWasEnabled)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.58f, 0.88f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.42f, 0.72f, 1.0f));
				}
				if (DrawViewerToolbarIconButton("##SnapToggle", Icon, FallbackLabel))
				{
					bEnabled = !bEnabled;
					bSnapChanged = true;
				}
				if (bWasEnabled)
				{
					ImGui::PopStyleColor(3);
				}
				ImGui::SameLine(0.0f, 2.0f);
				ImGui::SetNextItemWidth(48.0f);
				if (ImGui::InputFloat("##Value", &Value, 0.0f, 0.0f, "%.2f"))
				{
					if (Value < MinValue)
					{
						Value = MinValue;
					}
					bSnapChanged = true;
				}
				ImGui::PopID();
			};

		DrawSnapControl("ViewerTranslateSnap", EViewerToolbarIcon::TranslateSnap, "T", Settings.bEnableTranslationSnap, Settings.TranslationSnapSize, 0.001f);
		DrawSnapControl("ViewerRotateSnap", EViewerToolbarIcon::RotateSnap, "R", Settings.bEnableRotationSnap, Settings.RotationSnapSize, 0.001f);
		DrawSnapControl("ViewerScaleSnap", EViewerToolbarIcon::ScaleSnap, "S", Settings.bEnableScaleSnap, Settings.ScaleSnapSize, 0.001f);

		if (EditorEngine && (bSnapChanged || Gizmo))
		{
			EditorEngine->ApplyTransformSettingsToGizmo(Gizmo);
		}
	}
}

EModeBarIcon GetSkeletalEditorModeBarIconForKind(ESkeletalEditorTabKind Kind)
{
	return Kind == ESkeletalEditorTabKind::AnimSequence
		? EModeBarIcon::AnimSequence
		: EModeBarIcon::SkeletalMesh;
}

ImTextureID GetSkeletalEditorModeBarIconTexture(EModeBarIcon Icon)
{
	EnsureModeBarIconsLoaded();
	ID3D11ShaderResourceView* SRV = GetModeBarIconTable()[static_cast<int32>(Icon)];
	return reinterpret_cast<ImTextureID>(SRV);
}

// ===== FSkeletalEditorTab =====

FSkeletalEditorTab::FSkeletalEditorTab(UEditorEngine* InEditorEngine, int32 InTabId)
	: EditorEngine(InEditorEngine)
	, TabId(InTabId)
{
}

FSkeletalEditorTab::~FSkeletalEditorTab()
{
	// PreviewScene 소멸자가 알아서 Release() 호출
}

FString FSkeletalEditorTab::ExtractFileStem(const FString& Path)
{
	// FBX 애니메이션 이름 등에서 쓰이는 '|' 도 디렉터리 구분자로 처리
	const size_t SepPos = Path.find_last_of("/\\|");
	const FString FileName = (SepPos == FString::npos) ? Path : Path.substr(SepPos + 1);
	const size_t DotPos = FileName.find_last_of('.');
	return (DotPos == FString::npos) ? FileName : FileName.substr(0, DotPos);
}

FString FSkeletalEditorTab::ExtractFileName(const FString& Path)
{
	const size_t SepPos = Path.find_last_of("/\\|");
	return (SepPos == FString::npos) ? Path : Path.substr(SepPos + 1);
}

void FSkeletalEditorTab::RenderTabModeBar()
{
	EnsureModeBarIconsLoaded();

	constexpr float ButtonSize = 22.0f;
	constexpr float ButtonSpacing = 4.0f;
	const float FramePadX = ImGui::GetStyle().FramePadding.x * 2.0f;
	const float TotalWidth = (ButtonSize + FramePadX) * 2.0f + ButtonSpacing;

	// 우측 정렬
	const float Avail = ImGui::GetContentRegionAvail().x;
	if (Avail > TotalWidth)
	{
		ImGui::Dummy(ImVec2(Avail - TotalWidth, 0.0f));
		ImGui::SameLine(0.0f, 0.0f);
	}

	const ESkeletalEditorTabKind Kind = GetKind();
	const bool bIsSkelMode = (Kind == ESkeletalEditorTabKind::SkeletalMesh);
	const bool bIsAnimMode = (Kind == ESkeletalEditorTabKind::AnimSequence);

	if (DrawModeBarButton("##SwitchToSkelMesh", EModeBarIcon::SkeletalMesh,
		bIsSkelMode, OnSwitchToSkeletalMesh != nullptr, "Mesh"))
	{
		if (OnSwitchToSkeletalMesh) OnSwitchToSkeletalMesh();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Open SkeletalMesh Editor for this mesh");
	}

	ImGui::SameLine(0.0f, ButtonSpacing);

	if (DrawModeBarButton("##SwitchToAnimSeq", EModeBarIcon::AnimSequence,
		bIsAnimMode, OnSwitchToAnimSequence != nullptr, "Anim"))
	{
		if (OnSwitchToAnimSequence) OnSwitchToAnimSequence();
	}
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Open AnimSequence Editor for the current clip");
	}
}

void FSkeletalEditorTab::RenderTabContent(float DeltaTime)
{
	ImGui::PushID(TabId);

	RenderTabModeBar();

	if (ImGui::BeginTable(
		"##SkeletalMeshViewerLayout",
		3,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 260.0f);
		ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthFixed, 280.0f);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		RenderLeftPanel();

		ImGui::TableSetColumnIndex(1);
		RenderViewportPanel(DeltaTime);

		ImGui::TableSetColumnIndex(2);
		RenderRightPanel();

		ImGui::EndTable();
	}

	ImGui::PopID();
}

void FSkeletalEditorTab::UpdateInput(float DeltaTime)
{
	(void)DeltaTime;
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;

	if (!bHasPreviewViewportRect || !PreviewViewportClient)
	{
		bPreviewViewportWantsMouseCapture = false;
		bPreviewViewportWantsKeyboardCapture = false;
		return;
	}

	FInputFrame InputFrame(InputSystem::Get().MakeSnapshot());
	const POINT MousePos = InputFrame.GetMousePosition();
	const float MouseX = static_cast<float>(MousePos.x);
	const float MouseY = static_cast<float>(MousePos.y);
	const bool bMouseInPreviewViewport =
		MouseX >= PreviewViewportMin.x && MouseX <= PreviewViewportMax.x &&
		MouseY >= PreviewViewportMin.y && MouseY <= PreviewViewportMax.y;

	const bool bRightMouseDown = InputFrame.GetRawSnapshotForDebug().bRightMouseDown;
	const bool bMiddleMouseDown = InputFrame.GetRawSnapshotForDebug().bMiddleMouseDown;
	const bool bAnyCaptureButtonDown = bRightMouseDown || bMiddleMouseDown;

	if (!bPreviewViewportWantsMouseCapture)
	{
		if (bMouseInPreviewViewport &&
			(InputFrame.GetRawSnapshotForDebug().bRightMousePressed ||
				InputFrame.GetRawSnapshotForDebug().bMiddleMousePressed))
		{
			bPreviewViewportWantsMouseCapture = true;
		}
	}
	else if (!bAnyCaptureButtonDown)
	{
		bPreviewViewportWantsMouseCapture = false;
	}

	bPreviewViewportWantsKeyboardCapture = bPreviewViewportWantsMouseCapture && bRightMouseDown;
}

void FSkeletalEditorTab::RenderViewportPanel(float DeltaTime)
{
	ImVec2 AvailableSize = ImGui::GetContentRegionAvail();
	if (AvailableSize.x < 1.0f) AvailableSize.x = 1.0f;
	if (AvailableSize.y < 1.0f) AvailableSize.y = 1.0f;

	ImGui::BeginChild("##SkeletalMeshViewport", AvailableSize, true, ImGuiWindowFlags_NoScrollbar);

	USkeletalMesh* SelectedMesh = GetActivePreviewMesh();
	if (!SelectedMesh)
	{
		bPreviewViewportWantsMouseCapture = false;
		bPreviewViewportWantsKeyboardCapture = false;
		ImGui::TextDisabled("No SkeletalMesh loaded");
		ImGui::EndChild();
		return;
	}

	PreviewScene.Ensure();
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;
	FViewport* PreviewViewport = PreviewScene.PreviewViewport;
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;
	UWorld* PreviewWorld = PreviewScene.PreviewWorld;

	RenderViewerViewportToolbar();
	ImGui::Separator();

	ImVec2 ViewportMin = ImGui::GetCursorScreenPos();
	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
	if (ViewportSize.x < 1.0f) ViewportSize.x = 1.0f;
	if (ViewportSize.y < 1.0f) ViewportSize.y = 1.0f;
	PreviewViewportMin = ViewportMin;
	PreviewViewportMax = ImVec2(ViewportMin.x + ViewportSize.x, ViewportMin.y + ViewportSize.y);
	bHasPreviewViewportRect = true;
	ImGui::InvisibleButton(
		"##SkeletalMeshPreviewViewportInput",
		ViewportSize,
		ImGuiButtonFlags_MouseButtonLeft |
		ImGuiButtonFlags_MouseButtonRight |
		ImGuiButtonFlags_MouseButtonMiddle);
	const bool bViewportInputHovered = ImGui::IsItemHovered();
	const bool bViewportInputActive = ImGui::IsItemActive();

	// 에디터 메인 뷰포트에서 액터를 선택하는 등 외부 동작 후 preview의 SceneProxy가
	// 누락되는 케이스 방어 — 컴포넌트 상태가 어긋났으면 매 프레임 자가 복구한다.
	// 사용자의 카메라 조작을 보존하기 위해 복구 경로에서는 FrameMesh를 건너뛴다.
	//
	// 같은 메시인데 SceneProxy만 누락된 경우와 메시 자체가 바뀐 경우를 분리해서 처리한다.
	// 같은 메시에서 SetPreviewMesh를 다시 호출하면 USkinnedMeshComponent::SetSkeletalMesh -> ResetBonePoseToBindPose가 호출되어
	// 애니메이션 평가 중인 본 포즈가 매 프레임 bind pose로 리셋되어 버림.
	const bool bMeshChanged =
		PreviewMeshComponent && PreviewMeshComponent->GetSkeletalMesh() != SelectedMesh;
	const bool bProxyMissing =
		PreviewMeshComponent && PreviewMeshComponent->GetSceneProxy() == nullptr;

	if (bMeshChanged)
	{
		PreviewScene.SetPreviewMesh(SelectedMesh, false);
	}
	else if (bProxyMissing)
	{
		// 메시는 그대로이고 렌더링 리소스만 사라진 경우 — SceneProxy만 다시 만든다.
		PreviewMeshComponent->MarkRenderStateDirty();
	}

	if (PreviewViewport && PreviewViewportClient && EditorEngine)
	{
		const ImVec2 MousePos = ImGui::GetIO().MousePos;
		const bool bViewportHovered =
			bViewportInputHovered ||
			bViewportInputActive ||
			(MousePos.x >= PreviewViewportMin.x && MousePos.x <= PreviewViewportMax.x &&
				MousePos.y >= PreviewViewportMin.y && MousePos.y <= PreviewViewportMax.y);
		const bool bRightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
		const bool bMiddleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
		const bool bAnyCaptureButtonDown = bRightMouseDown || bMiddleMouseDown;

		const uint32 NewWidth = static_cast<uint32>(ViewportSize.x);
		const uint32 NewHeight = static_cast<uint32>(ViewportSize.y);

		PreviewViewport->RequestResize(NewWidth, NewHeight);
		PreviewViewportClient->SetViewportRect(ViewportMin.x, ViewportMin.y, ViewportSize.x, ViewportSize.y);

		if (!bPreviewViewportWantsMouseCapture)
		{
			if (bViewportHovered &&
				(ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
					ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))
			{
				bPreviewViewportWantsMouseCapture = true;
			}
		}
		else if (!bAnyCaptureButtonDown)
		{
			bPreviewViewportWantsMouseCapture = false;
		}

		bPreviewViewportWantsKeyboardCapture =
			bPreviewViewportWantsMouseCapture && bRightMouseDown;

		PreviewScene.Tick(DeltaTime);
		UpdateBoneDebugLines();
		OnTickPreview(DeltaTime);

		FInputFrame InputFrame(InputSystem::Get().MakeSnapshot());
		PreviewViewportClient->Tick(
			DeltaTime,
			bViewportHovered || bPreviewViewportWantsMouseCapture,
			bPreviewViewportWantsMouseCapture,
			InputFrame);

		UpdateBoneWeightHeatmapState();
		
		EditorEngine->RenderSkeletalMeshViewerPreview(
			PreviewWorld,
			PreviewViewport,
			PreviewViewportClient);

		if (PreviewViewport->GetSRV())
		{
			FImGuiViewportPresenter::DrawInCurrentWindow(
				PreviewViewport,
				FViewportPresentationRect(ViewportMin.x, ViewportMin.y, ViewportSize.x, ViewportSize.y));
		}
		else
		{
			ImGui::TextDisabled("Preview render target is not ready");
		}
	}
	else
	{
		bPreviewViewportWantsMouseCapture = false;
		bPreviewViewportWantsKeyboardCapture = false;
		ImGui::TextDisabled("Preview scene is not ready");
	}

	ImGui::EndChild();
}

void FSkeletalEditorTab::DrawViewerShowFlagsControls(FViewportRenderOptions& Opts, const char* TableId)
{
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;

	ImGui::Text("Show");
	if (ImGui::BeginTable(TableId, 6, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame))
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		if (PreviewMeshComponent)
		{
			bool bMeshVisible = PreviewMeshComponent->IsVisible();
			if (ImGui::Checkbox("Mesh", &bMeshVisible))
				PreviewMeshComponent->SetVisibility(bMeshVisible);
		}
		ImGui::TableNextColumn();
		ImGui::Checkbox("Grid", &Opts.ShowFlags.bGrid);
		ImGui::TableNextColumn();
		ImGui::Checkbox("World Axis", &Opts.ShowFlags.bWorldAxis);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Gizmo", &Opts.ShowFlags.bGizmo);

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Checkbox("Debug Draw", &Opts.ShowFlags.bDebugDraw);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Bone Weight Heatmap", &Opts.ShowFlags.bBoneWeightHeatmap);
		ImGui::TableNextColumn();
		
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::Checkbox("FXAA", &Opts.ShowFlags.bFXAA);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Visualize2.5D", &Opts.ShowFlags.bVisualize25DCulling);
		ImGui::Checkbox("Draw Debug Line", &bDrawBoneDebugLines);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Shadows", &FProjectSettings::Get().Shadow.bEnabled);
		ImGui::TableNextColumn();
		ImGui::Checkbox("Shadow Frustum", &Opts.ShowFlags.bShowShadowFrustum);
		ImGui::TableNextColumn();

		ImGui::EndTable();
	}
}

void FSkeletalEditorTab::RenderViewerViewportToolbar()
{
	FSkeletalMeshViewerViewportClient* PreviewClient = PreviewScene.PreviewViewportClient;
	if (!PreviewClient)
	{
		return;
	}

	EnsureViewerToolbarIconsLoaded();
	FViewportRenderOptions& Opts = PreviewClient->GetRenderOptions();

	static const char* ViewportTypeNames[] = {
		"Perspective", "Top", "Bottom", "Left", "Right", "Front", "Back", "Free Orthographic"
	};
	constexpr int32 ViewportTypeCount = sizeof(ViewportTypeNames) / sizeof(ViewportTypeNames[0]);
	int32 CurrentTypeIdx = static_cast<int32>(Opts.ViewportType);
	const char* CurrentTypeName =
		(CurrentTypeIdx >= 0 && CurrentTypeIdx < ViewportTypeCount)
		? ViewportTypeNames[CurrentTypeIdx]
		: ViewportTypeNames[0];

	static const char* ViewModeNames[] = { "Phong", "Unlit", "Gouraud", "Lambert", "Wireframe", "SceneDepth", "WorldNormal", "LightCulling" };
	const int32 ViewModeIndex = static_cast<int32>(Opts.ViewMode);
	const char* CurrentViewModeName = (ViewModeIndex >= 0 && ViewModeIndex < static_cast<int32>(EViewMode::Count))
		? ViewModeNames[ViewModeIndex]
		: ViewModeNames[static_cast<int32>(EViewMode::Lit_Phong)];

	const float RowStartX = ImGui::GetCursorPosX();
	const float RowRightX = RowStartX + ImGui::GetContentRegionAvail().x;
	RenderViewerTransformToolbar(PreviewClient, Cast<UEditorEngine>(GEngine));

	const float RightToolbarWidth =
		CalcViewerTextButtonWidth(CurrentTypeName) +
		ImGui::GetStyle().ItemSpacing.x +
		CalcViewerTextButtonWidth(CurrentViewModeName) +
		ImGui::GetStyle().ItemSpacing.x +
		CalcViewerIconButtonWidth() +
		ImGui::GetStyle().ItemSpacing.x +
		CalcViewerIconButtonWidth();
	const float RightToolbarStartX = RowRightX - RightToolbarWidth;

	if (ImGui::GetCursorPosX() < RightToolbarStartX)
	{
		ImGui::SameLine();
		ImGui::SetCursorPosX(RightToolbarStartX);
	}
	else
	{
		ImGui::SameLine();
	}

	if (ImGui::Button(CurrentTypeName))
	{
		ImGui::OpenPopup("ViewerViewportTypePopup");
	}
	if (ImGui::BeginPopup("ViewerViewportTypePopup"))
	{
		for (int32 TypeIndex = 0; TypeIndex < ViewportTypeCount; ++TypeIndex)
		{
			const bool bSelected = TypeIndex == CurrentTypeIdx;
			if (ImGui::Selectable(ViewportTypeNames[TypeIndex], bSelected))
			{
				PreviewClient->SetViewportType(static_cast<ELevelViewportType>(TypeIndex));
			}
		}
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (ImGui::Button(CurrentViewModeName))
	{
		ImGui::OpenPopup("ViewerViewModePopup");
	}
	if (ImGui::BeginPopup("ViewerViewModePopup"))
	{
		int32 CurrentMode = (ViewModeIndex >= 0 && ViewModeIndex < static_cast<int32>(EViewMode::Count))
			? ViewModeIndex
			: static_cast<int32>(EViewMode::Lit_Phong);

		if (ImGui::BeginTable("ViewerViewModeTable", 3, ImGuiTableFlags_SizingStretchSame))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::RadioButton("Unlit", &CurrentMode, static_cast<int32>(EViewMode::Unlit));
			ImGui::TableNextColumn();
			ImGui::RadioButton("Phong", &CurrentMode, static_cast<int32>(EViewMode::Lit_Phong));
			ImGui::TableNextColumn();
			ImGui::RadioButton("Gouraud", &CurrentMode, static_cast<int32>(EViewMode::Lit_Gouraud));

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::RadioButton("Lambert", &CurrentMode, static_cast<int32>(EViewMode::Lit_Lambert));
			ImGui::TableNextColumn();
			ImGui::RadioButton("Wireframe", &CurrentMode, static_cast<int32>(EViewMode::Wireframe));
			ImGui::TableNextColumn();
			ImGui::RadioButton("SceneDepth", &CurrentMode, static_cast<int32>(EViewMode::SceneDepth));
			ImGui::TableNextColumn();
			ImGui::RadioButton("WorldNormal", &CurrentMode, static_cast<int32>(EViewMode::WorldNormal));

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::RadioButton("LightCulling", &CurrentMode, static_cast<int32>(EViewMode::LightCulling));
			ImGui::EndTable();
		}

		Opts.ViewMode = static_cast<EViewMode>(CurrentMode);
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (DrawViewerToolbarIconButton("##ViewerShowFlagsIcon", EViewerToolbarIcon::ShowFlag, "Show"))
	{
		ImGui::OpenPopup("ViewerShowFlagsPopup");
	}
	if (ImGui::BeginPopup("ViewerShowFlagsPopup"))
	{
		DrawViewerShowFlagsControls(Opts, "ViewerShowFlagsTable");
		ImGui::EndPopup();
	}

	ImGui::SameLine();
	if (DrawViewerToolbarIconButton("##ViewerSettingsIcon", EViewerToolbarIcon::Setting, "Settings"))
	{
		ImGui::OpenPopup("ViewerSettingsPopup");
	}
	if (ImGui::BeginPopup("ViewerSettingsPopup"))
	{
		if (ImGui::CollapsingHeader("Viewport Utility Settings (Grid , Camera , SceneDepth , FXAA)"))
		{
			ImGui::Text("Grid");
			ImGui::SliderFloat("Spacing", &Opts.GridSpacing, 0.1f, 10.0f, "%.1f");
			ImGui::SliderInt("Half Line Count", &Opts.GridHalfLineCount, 10, 500);

			ImGui::Separator();
			ImGui::Text("Camera");
			ImGui::SliderFloat("Move Sensitivity", &Opts.CameraMoveSensitivity, 0.1f, 5.0f, "%.1f");
			ImGui::SliderFloat("Rotate Sensitivity", &Opts.CameraRotateSensitivity, 0.1f, 5.0f, "%.1f");

			ImGui::Separator();
			ImGui::Text("SceneDepth");
			ImGui::SliderFloat("Exponent", &Opts.Exponent, 1.0f, 512.0f, "%.0f");
			ImGui::Combo("Mode", &Opts.SceneDepthVisMode, "Power\0Linear\0");

			ImGui::Text("FXAA");
			ImGui::SliderFloat("EdgeThreshold", &Opts.EdgeThreshold, 0.06f, 0.333f, "%.3f");
			ImGui::SliderFloat("EdgeThresholdMin", &Opts.EdgeThresholdMin, 0.0312f, 0.0833f, "%.4f");
		}

		if (ImGui::CollapsingHeader("Light Culling Settings"))
		{
			int32 CullingMode = static_cast<int32>(Opts.LightCullingMode);
			ImGui::RadioButton("Off", &CullingMode, static_cast<int32>(ELightCullingMode::Off));
			ImGui::SameLine();
			ImGui::RadioButton("Tile", &CullingMode, static_cast<int32>(ELightCullingMode::Tile));
			ImGui::SameLine();
			ImGui::RadioButton("Cluster", &CullingMode, static_cast<int32>(ELightCullingMode::Cluster));
			Opts.LightCullingMode = static_cast<ELightCullingMode>(CullingMode);
			ImGui::SliderFloat("HeatMapMax", &Opts.HeatMapMax, 1.0f, 100.0f, "%.0f");
			ImGui::Checkbox("Enable2.5DCulling", &Opts.Enable25DCulling);
			ImGui::Checkbox("Visualize2.5DCulling", &Opts.ShowFlags.bVisualize25DCulling);
		}

		ImGui::EndPopup();
	}
}

void FSkeletalEditorTab::UpdateBoneDebugLines()
{
	if (!bDrawBoneDebugLines)
	{
		return;
	}

	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;
	UWorld* PreviewWorld = PreviewScene.PreviewWorld;

	USkeletalMesh* MeshObj = PreviewMeshComponent ? PreviewMeshComponent->GetSkeletalMesh() : nullptr;
	const USkeleton* Skeleton = MeshObj ? MeshObj->GetSkeleton() : nullptr;
	if (!Skeleton || !PreviewMeshComponent || !PreviewWorld)
	{
		return;
	}

	const TArray<FBoneInfo>& Bones = Skeleton->GetBones();
	if (Bones.empty())
	{
		return;
	}

	const auto& BoneMatrices = PreviewMeshComponent->GetMeshSpaceBoneMatrices();
	// MeshSpace 본 행렬 캐시가 아직 채워지지 않은 시점(첫 SetSkeletalMesh 직후 등)에는
	// BoneMatrices[i] 접근이 즉시 크래시한다. 비어 있으면 다음 프레임을 기다린다.
	if (BoneMatrices.size() < Bones.size())
	{
		return;
	}

	const int32 CurrentSelectedBoneIndex = PreviewViewportClient
		? PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone()
		: -1;

	FVector Subtract = PreviewMeshComponent->GetWorldAABB().Max - PreviewMeshComponent->GetWorldAABB().Min;
	const float BoneDebugDistance = std::sqrt(Subtract.Dot(Subtract));
	const float BoneDebugJointRadius = BoneDebugDistance * BoneDebugJointRadiusScale;

	FMatrix ComponentWorldTransform = PreviewMeshComponent->GetWorldMatrix();

	for (int i = 0; i < Bones.size(); ++i)
	{
		FMatrix WorldMat = BoneMatrices[i] * ComponentWorldTransform;
		FVector WorldPos = WorldMat.GetLocation();
		const FColor BoneColor = i == CurrentSelectedBoneIndex ? SelectedBoneDebugColor : FColor::Yellow();
		DrawDebugNodepthSphere(PreviewWorld, WorldPos, BoneDebugJointRadius, 8, BoneColor);
	}

	for (int i = 0; i < Bones.size(); ++i)
	{
		const auto& Bone = Bones[i];

		if (Bone.ParentIndex < 0 || Bone.ParentIndex >= BoneMatrices.size())
		{
			continue;
		}

		FMatrix ParentWorldMat = BoneMatrices[Bone.ParentIndex] * ComponentWorldTransform;
		FVector ParentPos = ParentWorldMat.GetLocation();

		FMatrix ChildWorldMat = BoneMatrices[i] * ComponentWorldTransform;
		FVector ChildPos = ChildWorldMat.GetLocation();

		const FColor BoneColor = i == CurrentSelectedBoneIndex ? SelectedBoneDebugColor : FColor::Green();
		DrawDebugOctahedralBone(PreviewWorld, ParentPos, ChildPos, BoneColor);
	}
}

void FSkeletalEditorTab::UpdateBoneWeightHeatmapState()
{
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;
	
	if (!PreviewMeshComponent || !PreviewViewportClient)
	{
		return;
	}
	
	FViewportRenderOptions& Opts = PreviewViewportClient->GetRenderOptions();
	const int32 SelectedBoneIndex = PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone();
	
	PreviewMeshComponent->SetBoneWeightHeatmapState(Opts.ShowFlags.bBoneWeightHeatmap, SelectedBoneIndex);
}
