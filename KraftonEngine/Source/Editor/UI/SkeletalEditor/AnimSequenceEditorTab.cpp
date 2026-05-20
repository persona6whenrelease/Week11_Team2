#include "Editor/UI/SkeletalEditor/AnimSequenceEditorTab.h"
#include "Editor/UI/SkeletalEditor/SkeletonTreeUtil.h"

#include "Component/SkeletalMeshComponent.h"
#include "Editor/UI/EditorFileUtils.h"
#include "Editor/Viewport/SkeletalMeshViewerViewportClient.h"
#include "Asset/Animation/Core/AnimSequence.h"
#include "Asset/Import/MeshManager.h"
#include "Asset/Import/FBX/Types/FBXSceneAsset.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMesh.h"
#include "Asset/Mesh/SkeletalMesh/SkeletalMeshAsset.h"
#include "ImGui/imgui.h"
#include "Runtime/Engine.h"
#include "Render/Pipeline/Renderer.h"
#include "Platform/Paths.h"
#include "Sound/SoundManager.h"
#include "WICTextureLoader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

namespace
{
	// ===== UE 아이콘 toolbar =====
	enum class EAnimToolIcon : int32
	{
		GoToFront = 0,
		StepBackwards,
		Backwards,
		Play,
		Pause,
		StepForward,
		GoToEnd,
		Loop,
		Recording,
		Count
	};

	const wchar_t* GetAnimToolIconFileName(EAnimToolIcon Icon, bool bOn)
	{
		switch (Icon)
		{
		case EAnimToolIcon::GoToFront:     return bOn ? L"Go_To_Front_24x.png"     : L"Go_To_Front_24x_OFF.png";
		case EAnimToolIcon::StepBackwards: return bOn ? L"Step_Backwards_24x.png"  : L"Step_Backwards_24x_OFF.png";
		case EAnimToolIcon::Backwards:     return bOn ? L"Backwards_24x.png"       : L"Backwards_24x_OFF.png";
		case EAnimToolIcon::Play:          return bOn ? L"Play_24x.png"            : L"Play_24x_OFF.png";
		case EAnimToolIcon::Pause:         return bOn ? L"Pause_24x.png"           : L"Pause_24x_OFF.png";
		case EAnimToolIcon::StepForward:   return bOn ? L"Step_Forward_24x.png"    : L"Step_Forward_24x_OFF.png";
		case EAnimToolIcon::GoToEnd:       return bOn ? L"Go_To_End_24x.png"       : L"Go_To_End_24x_OFF.png";
		// Loop: ON = 루프 아이콘, OFF = 화살표 스타일(Loop_Toggle)
		case EAnimToolIcon::Loop:          return bOn ? L"Loop_24x.png"            : L"Loop_Toggle_24x.png";
		// Recording: ON = 녹화중(Recording_24x), OFF = 비활성(Record_24x_OFF, 음영)
		case EAnimToolIcon::Recording:     return bOn ? L"Recording_24x.png"       : L"Record_24x_OFF.png";
		default: return L"";
		}
	}

	struct FAnimIconSRVs
	{
		ID3D11ShaderResourceView* On = nullptr;
		ID3D11ShaderResourceView* Off = nullptr;
	};

	FAnimIconSRVs* GetAnimIconTable()
	{
		static FAnimIconSRVs Icons[static_cast<int32>(EAnimToolIcon::Count)] = {};
		return Icons;
	}

	bool bAnimIconsLoaded = false;

	void EnsureAnimIconsLoaded()
	{
		if (bAnimIconsLoaded || !GEngine) return;
		ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
		if (!Device) return;

		FAnimIconSRVs* Icons = GetAnimIconTable();
		const std::wstring IconDir = FPaths::Combine(FPaths::RootDir(), L"Asset/Editor/UEIcons/");
		for (int32 i = 0; i < static_cast<int32>(EAnimToolIcon::Count); ++i)
		{
			const auto LoadOne = [&](bool bOn) -> ID3D11ShaderResourceView*
			{
				ID3D11ShaderResourceView* SRV = nullptr;
				const std::wstring FilePath = IconDir + GetAnimToolIconFileName(static_cast<EAnimToolIcon>(i), bOn);
				DirectX::CreateWICTextureFromFile(Device, FilePath.c_str(), nullptr, &SRV);
				return SRV;
			};
			Icons[i].On = LoadOne(true);
			Icons[i].Off = LoadOne(false);
		}
		bAnimIconsLoaded = true;
	}

	bool DrawAnimIconButton(const char* Id, EAnimToolIcon Icon, bool bActive, const char* FallbackLabel)
	{
		constexpr float IconSize = 24.0f;
		const FAnimIconSRVs& SRVs = GetAnimIconTable()[static_cast<int32>(Icon)];
		ID3D11ShaderResourceView* SRV = bActive ? SRVs.On : SRVs.Off;
		if (!SRV) SRV = SRVs.On ? SRVs.On : SRVs.Off;
		if (!SRV)
		{
			return ImGui::Button(FallbackLabel);
		}
		return ImGui::ImageButton(Id, reinterpret_cast<ImTextureID>(SRV), ImVec2(IconSize, IconSize));
	}
	
	FVector GetRotationEulerNoScale(const FMatrix& Matrix)
	{
		FMatrix RotationMatrix = Matrix;
		const FVector Scale = Matrix.GetScale();
		if (std::abs(Scale.X) > 0.0001f)
		{
			RotationMatrix.M[0][0] /= Scale.X;
			RotationMatrix.M[0][1] /= Scale.X;
			RotationMatrix.M[0][2] /= Scale.X;
		}
		if (std::abs(Scale.Y) > 0.0001f)
		{
			RotationMatrix.M[1][0] /= Scale.Y;
			RotationMatrix.M[1][1] /= Scale.Y;
			RotationMatrix.M[1][2] /= Scale.Y;
		}
		if (std::abs(Scale.Z) > 0.0001f)
		{
			RotationMatrix.M[2][0] /= Scale.Z;
			RotationMatrix.M[2][1] /= Scale.Z;
			RotationMatrix.M[2][2] /= Scale.Z;
		}
		RotationMatrix.SetLocation(FVector(0.0f, 0.0f, 0.0f));
		return RotationMatrix.GetEuler();
	}

	FMatrix MakeLocalPoseMatrix(const FVector& Location, const FVector& Rotation, const FVector& Scale)
	{
		return FMatrix::MakeScaleMatrix(Scale) *
			FMatrix::MakeRotationEuler(Rotation) *
			FMatrix::MakeTranslationMatrix(Location);
	}

	// timeline notify 마커의 type별 자동 색상. Sound=청색, CameraShake=주황, None(legacy)=회색.
	ImU32 GetNotifyTypeColor(EAnimNotifyType Type)
	{
		switch (Type)
		{
		case EAnimNotifyType::Sound:       return IM_COL32( 80, 180, 230, 255);
		case EAnimNotifyType::CameraShake: return IM_COL32(240, 150,  60, 255);
		case EAnimNotifyType::None:
		default:                           return IM_COL32(140, 140, 140, 255);
		}
	}
}

// =================================================================
// 생성 / 진입점
// =================================================================

FAnimSequenceEditorTab::FAnimSequenceEditorTab(UEditorEngine* InEditorEngine, int32 InTabId)
	: FSkeletalEditorTab(InEditorEngine, InTabId)
{
}

bool FAnimSequenceEditorTab::OpenAnimSequenceAsset(const FString& AssetPath)
{
	// UAnimSequence asset 진입점 — ContentBrowser/Asset Browser의 anm 더블클릭이 여기로 들어온다.
	UAnimSequence* ResolvedSequence = FMeshManager::ResolveAnimSequenceReference(AssetPath);
	if (!ResolvedSequence)
	{
		return false;
	}

	// FBX outer가 이미 메모리에 있으면 거기서 preview mesh를 찾는다.
	// 없으면 mesh 없이 열고, 사용자가 나중에 직접 연결할 수 있다.
	USkeletalMesh* ResolvedPreviewMesh = nullptr;
	if (UFBXSceneAsset* SceneAsset = ResolvedSequence->GetTypedOuter<UFBXSceneAsset>())
	{
		ResolvedPreviewMesh = FMeshManager::FindSkeletalMeshForAnimSequence(SceneAsset, ResolvedSequence);
	}

	return OpenAnimSequenceAsset(AssetPath, ResolvedPreviewMesh, ResolvedSequence);
}

bool FAnimSequenceEditorTab::OpenAnimSequenceAsset(const FString& AssetPath, USkeletalMesh* InPreviewMesh, UAnimSequence* InSequence)
{
	if (!InSequence) return false;

	PreviewMesh = InPreviewMesh;
	AnimSequence = InSequence;

	const size_t AnimMarkerPos = AssetPath.find("#Anim_");
	FbxPath = AnimMarkerPos == FString::npos ? FString() : AssetPath.substr(0, AnimMarkerPos);
	SetSourcePath(AssetPath);
	DataSource = std::make_unique<FUAnimSequenceDataSource>(InSequence);

	PreviewScene.Ensure();
	if (InPreviewMesh &&
		PreviewScene.PreviewMeshComponent &&
		PreviewScene.PreviewMeshComponent->GetSkeletalMesh() != InPreviewMesh)
	{
		PreviewScene.SetPreviewMesh(InPreviewMesh);
	}

	if (USkeletalMeshComponent* Comp = PreviewScene.PreviewMeshComponent; Comp && InPreviewMesh)
	{
		// 시퀀스가 실제로 바뀐 경우에만 SetAnimation을 호출. SetAnimation은 내부적으로
		// ResetTime + SequencePlayer 캐시 재빌드를 동반하므로 같은 시퀀스에 매번 호출하면
		// 시간이 0으로 리셋되어 재생이 끊긴다.
		if (Comp->GetAnimation() != InSequence)
		{
			Comp->SetAnimation(InSequence);
		}
		Comp->SetBakedAnimTime(0.0f);
		Comp->SetBakedAnimPlaybackSpeed(1.0f);
		Comp->SetBakedAnimPaused(true);
		BakedClipIndex = 0;
	}

	CurrentTime = 0.0f;
	bPlaying = false;
	bLooping = true;
	PlayRate = 1.0f;
	SelectedNotifyIndex = -1;
	DraggingNotifyIndex = -1;
	return true;
}

FString FAnimSequenceEditorTab::GetTabLabel() const
{
	FString Base = DataSource ? DataSource->GetName() : FString();
	if (!Base.empty())
	{
		// "Skeleton|Skeleton|Ahri_skin14_run.anm" → "Ahri_skin14_run.anm"
		Base = ExtractFileName(Base);
	}
	else
	{
		Base = GetSourcePath().empty() ? FString("AnimSequence") : ExtractFileStem(GetSourcePath());
	}
	Base += " [Anim]";
	return Base + "###SkelEditorTab" + std::to_string(GetTabId());
}

// =================================================================
// Tick / Playback
// =================================================================

void FAnimSequenceEditorTab::OnTickPreview(float DeltaTime)
{
	if (!DataSource) return;

	const float Duration = DataSource->GetDuration();
	if (bPlaying && Duration > 0.0f)
	{
		CurrentTime += DeltaTime * PlayRate;
		if (bLooping)
		{
			CurrentTime = std::fmod(CurrentTime, Duration);
			if (CurrentTime < 0.0f) CurrentTime += Duration;
		}
		else
		{
			if (CurrentTime <= 0.0f)      { CurrentTime = 0.0f;     bPlaying = false; }
			else if (CurrentTime >= Duration) { CurrentTime = Duration; bPlaying = false; }
		}
	}
	SyncPlaybackToComponent();
}

void FAnimSequenceEditorTab::SyncPlaybackToComponent()
{
	USkeletalMeshComponent* Comp = PreviewScene.PreviewMeshComponent;
	if (!Comp || !DataSource) return;
	if (AnimSequence && Comp->GetAnimation() != AnimSequence)
	{
		Comp->SetAnimation(AnimSequence);
	}
	Comp->SetBakedAnimPaused(true);
	Comp->SetBakedAnimTime(CurrentTime);
}

// =================================================================
// 최상위 레이아웃: TabContent → CenterColumn (viewport + splitter + timeline)
// =================================================================

void FAnimSequenceEditorTab::RenderTabContent(float DeltaTime)
{
	ImGui::PushID(GetTabId());

	RenderTabModeBar();

	if (ImGui::BeginTable(
		"##AnimSeqViewerLayout",
		3,
		ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Hierarchy", ImGuiTableColumnFlags_WidthFixed, 260.0f);
		ImGui::TableSetupColumn("Center",    ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Details",   ImGuiTableColumnFlags_WidthFixed, 280.0f);

		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		RenderLeftPanel();

		ImGui::TableSetColumnIndex(1);
		RenderCenterColumn(DeltaTime);

		ImGui::TableSetColumnIndex(2);
		RenderRightPanel();

		ImGui::EndTable();
	}

	ImGui::PopID();
}

void FAnimSequenceEditorTab::RenderCenterColumn(float DeltaTime)
{
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	constexpr float SplitterThickness = 5.0f;
	constexpr float MinViewportHeight = 120.0f;
	constexpr float MinTimelineHeight = 160.0f;
	const float MaxTimelineHeight = std::max(MinTimelineHeight, Avail.y - MinViewportHeight - SplitterThickness);

	TimelinePanelHeight = std::clamp(TimelinePanelHeight, MinTimelineHeight, MaxTimelineHeight);
	const float ViewportHeight = std::max(MinViewportHeight, Avail.y - TimelinePanelHeight - SplitterThickness);

	if (ImGui::BeginChild("##AnimSeqViewportArea", ImVec2(0.0f, ViewportHeight), false))
	{
		RenderViewportPanel(DeltaTime);
	}
	ImGui::EndChild();

	// 가변 splitter (viewport ↔ timeline)
	ImGui::InvisibleButton("##VSplitter", ImVec2(-1.0f, SplitterThickness));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	if (ImGui::IsItemActive())
	{
		TimelinePanelHeight -= ImGui::GetIO().MouseDelta.y;
	}
	{
		const ImVec2 SpMin = ImGui::GetItemRectMin();
		const ImVec2 SpMax = ImGui::GetItemRectMax();
		const ImU32 SpColor = ImGui::IsItemActive() ? IM_COL32(100, 150, 200, 255)
		                                            : (ImGui::IsItemHovered() ? IM_COL32(110, 110, 115, 255)
		                                                                       : IM_COL32(60, 60, 65, 255));
		ImGui::GetWindowDrawList()->AddRectFilled(SpMin, SpMax, SpColor);
	}

	if (ImGui::BeginChild("##AnimSeqTimelineArea", ImVec2(0.0f, 0.0f), true))
	{
		RenderTimelinePanel();
	}
	ImGui::EndChild();
}

// =================================================================
// Timeline 패널 (UE Persona식 layout)
//   ┌ Filter row (left header col) | status (right)
//   ├ Tracks: header col (라벨) | content col (ruler + 마커 + scrub)
//   ├ Playback toolbar
//   └ (선택 시) Notify property editor inline
// =================================================================

void FAnimSequenceEditorTab::RenderTimelinePanel()
{
	if (!DataSource)
	{
		ImGui::TextDisabled("No anim sequence loaded.");
		return;
	}

	const float Duration   = DataSource->GetDuration();
	const float FrameRate  = DataSource->GetFrameRate();
	const int32 FrameCount = DataSource->GetFrameCount();
	if (Duration <= 0.0f || FrameCount <= 0)
	{
		ImGui::TextDisabled("Clip has zero duration");
		return;
	}

	const TArray<FAnimNotifyEntry>& Notifies = DataSource->GetNotifies();
	const bool bNotifySelected = (SelectedNotifyIndex >= 0
		&& SelectedNotifyIndex < static_cast<int32>(Notifies.size()));

	// ── Filter row (헤더 컬럼) + status (컨텐츠 컬럼) ───────────────
	ImGui::SetNextItemWidth(TimelineHeaderColWidth - 32.0f);
	ImGui::InputTextWithHint("##TLFilter", "Filter", TimelineFilterBuf, sizeof(TimelineFilterBuf));
	ImGui::SameLine();
	ImGui::TextDisabled("%d", static_cast<int32>(Notifies.size()));
	ImGui::SameLine();
	const int32 CurFrame = (FrameRate > 0.0f) ? static_cast<int32>(std::round(CurrentTime * FrameRate)) : 0;
	const float CurPct   = (Duration > 0.0f) ? (CurrentTime / Duration) * 100.0f : 0.0f;
	ImGui::TextDisabled("Frame: %d / %d   Time: %.3f / %.3fs   %.1f%%",
		CurFrame, FrameCount, CurrentTime, Duration, CurPct);

	// ── Tracks 영역 ──────────────────────────────────────────────
	const float HeaderW = TimelineHeaderColWidth;
	constexpr float RulerH   = 22.0f;
	constexpr float RowH     = 26.0f;
	constexpr int32 RowCount = 4; // Notifies / Curves / Additive / Attributes
	const float TracksH = RulerH + RowH * RowCount;

	const ImVec2 Origin   = ImGui::GetCursorScreenPos();
	const float  TotalW   = std::max(50.0f, ImGui::GetContentRegionAvail().x);
	const float  ContentX = Origin.x + HeaderW;
	const float  ContentW = TotalW - HeaderW;

	ImGui::InvisibleButton("##TLTracksHit", ImVec2(TotalW, TracksH),
		ImGuiButtonFlags_MouseButtonLeft);
	const bool bHovered     = ImGui::IsItemHovered();
	const bool bActive      = ImGui::IsItemActive();
	const bool bActivated   = ImGui::IsItemActivated();
	const bool bDeactivated = ImGui::IsItemDeactivated();

	const ImVec2 MousePos = ImGui::GetIO().MousePos;
	const bool bMouseInContent = MousePos.x >= ContentX;

	auto TimeToX = [&](float T)
	{
		return ContentX + std::clamp(T / Duration, 0.0f, 1.0f) * ContentW;
	};
	auto XToFrame = [&](float SX) -> int32
	{
		const float N = std::clamp((SX - ContentX) / ContentW, 0.0f, 1.0f);
		return static_cast<int32>(std::round(N * FrameCount));
	};
	auto FrameToTime = [&](int32 F)
	{
		return std::clamp(static_cast<float>(F) / static_cast<float>(FrameCount) * Duration, 0.0f, Duration);
	};

	const float NotifyY   = Origin.y + RulerH;
	const float CurveY    = NotifyY + RowH;
	const float AdditiveY = CurveY + RowH;
	const float AttribY   = AdditiveY + RowH;
	const float MaxY      = AttribY + RowH;

	auto HitTestNotify = [&](float SX, float SY) -> int32
	{
		if (SX < ContentX) return -1;
		if (SY < NotifyY || SY > NotifyY + RowH) return -1;
		for (int32 i = static_cast<int32>(Notifies.size()) - 1; i >= 0; --i)
		{
			const auto& N = Notifies[i];
			const float StartX = TimeToX(N.TriggerTime);
			const float EndX = (N.Duration > 0.0f)
				? TimeToX(std::min(N.TriggerTime + N.Duration, Duration)) : StartX;
			const float HMin = (N.Duration > 0.0f) ? StartX : StartX - 6.0f;
			const float HMax = (N.Duration > 0.0f) ? EndX   : StartX + 6.0f;
			if (SX >= HMin && SX <= HMax) return i;
		}
		return -1;
	};

	if (bActivated && bMouseInContent)
	{
		const int32 Hit = HitTestNotify(MousePos.x, MousePos.y);
		if (Hit >= 0) { SelectedNotifyIndex = Hit; DraggingNotifyIndex = Hit; bScrubbing = false; }
		else          { DraggingNotifyIndex = -1; bScrubbing = true; }
	}
	if (bActive && ImGui::IsMouseDown(ImGuiMouseButton_Left) && bMouseInContent)
	{
		const int32 SnapF = XToFrame(MousePos.x);
		const float SnapT = FrameToTime(SnapF);
		if (DraggingNotifyIndex >= 0 && DraggingNotifyIndex < static_cast<int32>(Notifies.size()))
		{
			FAnimNotifyEntry Upd = Notifies[DraggingNotifyIndex];
			Upd.TriggerTime = std::clamp(SnapT, 0.0f, Duration - Upd.Duration);
			DataSource->UpdateNotify(DraggingNotifyIndex, Upd);
		}
		else if (bScrubbing)
		{
			CurrentTime = SnapT;
			bPlaying = false;
		}
	}
	if (bDeactivated)
	{
		DraggingNotifyIndex = -1;
		bScrubbing = false;
	}
	if (bHovered && bMouseInContent && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		const int32 Hit = HitTestNotify(MousePos.x, MousePos.y);
		if (Hit >= 0) SelectedNotifyIndex = Hit;
		ImGui::OpenPopup("##TLCtxMenu");
	}

	ImDrawList* DL = ImGui::GetWindowDrawList();
	const ImVec2 RegMin = Origin;
	const ImVec2 RegMax = ImVec2(Origin.x + TotalW, MaxY);

	// 배경
	DL->AddRectFilled(RegMin, RegMax, IM_COL32(28, 28, 32, 255));
	DL->AddRectFilled(ImVec2(Origin.x, Origin.y), ImVec2(ContentX, MaxY),    IM_COL32(34, 34, 38, 255));
	DL->AddRectFilled(ImVec2(ContentX, Origin.y), ImVec2(RegMax.x, NotifyY), IM_COL32(46, 46, 52, 255));

	// 행별 zebra (컨텐츠)
	DL->AddRectFilled(ImVec2(ContentX, NotifyY),   ImVec2(RegMax.x, CurveY),    IM_COL32(40, 40, 44, 255));
	DL->AddRectFilled(ImVec2(ContentX, CurveY),    ImVec2(RegMax.x, AdditiveY), IM_COL32(36, 36, 40, 255));
	DL->AddRectFilled(ImVec2(ContentX, AdditiveY), ImVec2(RegMax.x, AttribY),   IM_COL32(40, 40, 44, 255));
	DL->AddRectFilled(ImVec2(ContentX, AttribY),   ImVec2(RegMax.x, MaxY),      IM_COL32(36, 36, 40, 255));

	// 행 구분선
	for (int32 i = 1; i <= RowCount; ++i)
	{
		const float Y = NotifyY + RowH * i;
		DL->AddLine(ImVec2(Origin.x, Y), ImVec2(RegMax.x, Y), IM_COL32(60, 60, 65, 255));
	}
	// 헤더↔컨텐츠 세로 구분
	DL->AddLine(ImVec2(ContentX, Origin.y), ImVec2(ContentX, MaxY), IM_COL32(70, 70, 75, 255));
	DL->AddLine(ImVec2(ContentX, NotifyY), ImVec2(RegMax.x, NotifyY), IM_COL32(70, 70, 75, 255));

	// Frame tick stride 자동
	int32 TickStride = 1;
	const float PixPerFrame = ContentW / static_cast<float>(FrameCount);
	if (PixPerFrame > 0.0f)
	{
		const float MinPx = 50.0f;
		const float Needed = MinPx / PixPerFrame;
		const int32 Cand[] = { 1, 2, 5, 10, 15, 20, 30, 50, 100, 200, 500 };
		TickStride = Cand[10];
		for (int32 c : Cand) if (static_cast<float>(c) >= Needed) { TickStride = c; break; }
	}
	for (int32 f = 0; f <= FrameCount; f += TickStride)
	{
		const float X = TimeToX(FrameToTime(f));
		DL->AddLine(ImVec2(X, NotifyY - 6.0f), ImVec2(X, NotifyY), IM_COL32(200, 200, 200, 255));
		char L[8]; snprintf(L, sizeof(L), "%d", f);
		DL->AddText(ImVec2(X + 2.0f, Origin.y + 4.0f), IM_COL32(210, 210, 210, 255), L);
		DL->AddLine(ImVec2(X, NotifyY), ImVec2(X, RegMax.y), IM_COL32(70, 70, 75, 60));
	}

	// UE Persona처럼 시퀀스 범위 시작/끝을 명시한다.
	const float StartX = TimeToX(0.0f);
	const float EndX = TimeToX(Duration);
	const ImU32 StartColor = IM_COL32(60, 220, 90, 255);
	const ImU32 EndColor = IM_COL32(235, 65, 55, 255);
	DL->AddLine(ImVec2(StartX, Origin.y), ImVec2(StartX, RegMax.y), StartColor, 2.0f);
	DL->AddLine(ImVec2(EndX, Origin.y), ImVec2(EndX, RegMax.y), EndColor, 2.0f);

	char StartLabel[16]; snprintf(StartLabel, sizeof(StartLabel), "%d", 0);
	char EndLabel[16]; snprintf(EndLabel, sizeof(EndLabel), "%d", FrameCount);
	const ImVec2 EndLabelSize = ImGui::CalcTextSize(EndLabel);
	const float BoundaryLabelY = RegMax.y - ImGui::GetTextLineHeight() - 3.0f;
	DL->AddText(ImVec2(StartX + 4.0f, BoundaryLabelY), StartColor, StartLabel);
	DL->AddText(ImVec2(std::max(ContentX, EndX - EndLabelSize.x - 4.0f), BoundaryLabelY), EndColor, EndLabel);

	// 트랙 라벨
	const ImU32 LabelColor = IM_COL32(190, 195, 205, 255);
	DL->AddText(ImVec2(Origin.x + 10.0f, NotifyY + 6.0f),   LabelColor, "Notifies");
	DL->AddText(ImVec2(Origin.x + 10.0f, CurveY + 6.0f),    LabelColor, "Curves (0)");
	DL->AddText(ImVec2(Origin.x + 10.0f, AdditiveY + 6.0f), LabelColor, "Additive Layer Tracks");
	DL->AddText(ImVec2(Origin.x + 10.0f, AttribY + 6.0f),   LabelColor, "Attributes");

	// Notify 마커
	const float MTop = NotifyY + 5.0f;
	const float MBot = NotifyY + RowH - 5.0f;
	const float MCY  = (MTop + MBot) * 0.5f;
	for (int32 i = 0; i < static_cast<int32>(Notifies.size()); ++i)
	{
		const auto& N = Notifies[i];
		const float SX = TimeToX(N.TriggerTime);
		const bool bSel = (SelectedNotifyIndex == i);
		const ImU32 BC = bSel ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 180);
		const float BT = bSel ? 2.0f : 1.0f;

		const ImU32 MarkerColor = GetNotifyTypeColor(N.Type);
		if (N.Duration > 0.0f)
		{
			const float EX = TimeToX(std::min(N.TriggerTime + N.Duration, Duration));
			DL->AddRectFilled(ImVec2(SX, MTop), ImVec2(EX, MBot), MarkerColor, 2.0f);
			DL->AddRect     (ImVec2(SX, MTop), ImVec2(EX, MBot), BC,           2.0f, 0, BT);
		}
		else
		{
			const float HW = 6.0f;
			const float HH = (MBot - MTop) * 0.5f;
			ImVec2 P[4] = { {SX, MCY - HH}, {SX + HW, MCY}, {SX, MCY + HH}, {SX - HW, MCY} };
			DL->AddConvexPolyFilled(P, 4, MarkerColor);
			DL->AddPolyline(P, 4, BC, ImDrawFlags_Closed, BT);
		}

		if (bSel)
		{
			const ImVec2 LS = ImGui::CalcTextSize(N.Name.c_str());
			const float LX = std::min(SX + 10.0f, RegMax.x - LS.x - 4.0f);
			DL->AddRectFilled(
				ImVec2(LX - 2.0f, MTop - LS.y - 4.0f),
				ImVec2(LX + LS.x + 2.0f, MTop - 2.0f),
				IM_COL32(0, 0, 0, 200));
			DL->AddText(ImVec2(LX, MTop - LS.y - 3.0f),
				IM_COL32(255, 255, 255, 240), N.Name.c_str());
		}
	}

	// Hover indicator (content col만)
	if (bHovered && bMouseInContent && DraggingNotifyIndex < 0)
	{
		const int32 HF = XToFrame(MousePos.x);
		const float HX = TimeToX(FrameToTime(HF));
		DL->AddLine(ImVec2(HX, NotifyY), ImVec2(HX, RegMax.y), IM_COL32(255, 255, 255, 70));

		char HL[40]; snprintf(HL, sizeof(HL), "f%d  %.3fs", HF, FrameToTime(HF));
		const ImVec2 LS = ImGui::CalcTextSize(HL);
		const float LX = std::min(HX + 6.0f, RegMax.x - LS.x - 4.0f);
		DL->AddRectFilled(
			ImVec2(LX - 2.0f, NotifyY + 2.0f),
			ImVec2(LX + LS.x + 2.0f, NotifyY + 2.0f + LS.y + 2.0f),
			IM_COL32(0, 0, 0, 180));
		DL->AddText(ImVec2(LX, NotifyY + 3.0f),
			IM_COL32(255, 255, 255, 230), HL);
	}

	// Scrub head — content col만 관통
	const float ScrubX = TimeToX(CurrentTime);
	const ImU32 ScrubColor = IM_COL32(255, 130, 30, 255);
	DL->AddLine(ImVec2(ScrubX, Origin.y), ImVec2(ScrubX, RegMax.y), ScrubColor, 2.0f);
	DL->AddTriangleFilled(
		ImVec2(ScrubX - 6.0f, Origin.y),
		ImVec2(ScrubX + 6.0f, Origin.y),
		ImVec2(ScrubX, Origin.y + 9.0f),
		ScrubColor);

	// 외곽
	DL->AddRect(RegMin, RegMax, IM_COL32(70, 70, 75, 255));

	// Context menu
	if (ImGui::BeginPopup("##TLCtxMenu"))
	{
		const bool bOnM = (SelectedNotifyIndex >= 0 && SelectedNotifyIndex < static_cast<int32>(Notifies.size()));
		if (bOnM)
		{
			if (ImGui::MenuItem("Delete Notify"))
			{
				DataSource->RemoveNotify(SelectedNotifyIndex);
				SelectedNotifyIndex = -1;
			}
			ImGui::Separator();
		}
		if (ImGui::MenuItem("Add Sound Notify at current frame"))
		{
			FAnimNotifyEntry NN;
			NN.Name = "SoundNotify";
			NN.TriggerTime = CurrentTime;
			NN.Duration = 0.0f;
			NN.ColorPacked = IM_COL32(120, 220, 120, 255);
			NN.Type = EAnimNotifyType::Sound;
			SelectedNotifyIndex = DataSource->AddNotify(NN);
		}
		if (ImGui::MenuItem("Add Camera Shake Notify at current frame"))
		{
			FAnimNotifyEntry NN;
			NN.Name = "CameraShakeNotify";
			NN.TriggerTime = CurrentTime;
			NN.Duration = 0.0f;
			NN.ColorPacked = IM_COL32(120, 220, 120, 255);
			NN.Type = EAnimNotifyType::CameraShake;
			SelectedNotifyIndex = DataSource->AddNotify(NN);
		}
		ImGui::EndPopup();
	}

	// ── 하단: Playback toolbar ────────────────────────────────────
	ImGui::Spacing();
	RenderPlaybackControls();

	// ── 선택된 Notify 인라인 속성 편집 ────────────────────────────
	if (bNotifySelected)
	{
		ImGui::Separator();
		RenderNotifyPropertyInline();
	}
}

// =================================================================
// Playback toolbar (UE 아이콘)
// =================================================================

void FAnimSequenceEditorTab::RenderPlaybackControls()
{
	EnsureAnimIconsLoaded();

	const float Duration = DataSource ? DataSource->GetDuration() : 0.0f;
	const float FrameRate = DataSource ? DataSource->GetFrameRate() : 30.0f;
	const float FrameStep = (FrameRate > 0.0f) ? (1.0f / FrameRate) : 0.0f;

	constexpr float ButtonSpacing = 2.0f;
	constexpr float GroupSpacing = 12.0f;

	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.15f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 1.0f, 1.0f, 0.3f));

	// |◀ 처음으로
	if (DrawAnimIconButton("##GoToFront", EAnimToolIcon::GoToFront, true, "|<"))
	{
		CurrentTime = 0.0f;
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	// ◀ 한 프레임 뒤로
	if (DrawAnimIconButton("##StepBack", EAnimToolIcon::StepBackwards, true, "<|"))
	{
		if (FrameStep > 0.0f)
		{
			CurrentTime = std::max(0.0f, CurrentTime - FrameStep);
			bPlaying = false;
		}
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	// ◀◀ 역재생 토글 — 활성 시 || 아이콘으로 전환, 비활성은 음영 처리 X
	{
		const bool bReversePlaying = bPlaying && PlayRate < 0.0f;
		const EAnimToolIcon BackIcon = bReversePlaying ? EAnimToolIcon::Pause : EAnimToolIcon::Backwards;
		if (DrawAnimIconButton("##Backwards", BackIcon, true, bReversePlaying ? "||" : "<<"))
		{
			if (bReversePlaying) bPlaying = false;
			else { PlayRate = -std::abs(PlayRate); bPlaying = true; }
		}
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	// ⏺ Recording — < 와 > 사이 중간에 배치 (placeholder, 실제 녹화 기능 미구현)
	if (DrawAnimIconButton("##Recording", EAnimToolIcon::Recording, bRecording, bRecording ? "Rec:On" : "Rec:Off"))
	{
		bRecording = !bRecording;
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	// ▶ / ⏸
	{
		const bool bForwardPlaying = bPlaying && PlayRate >= 0.0f;
		const EAnimToolIcon PlayIcon = bForwardPlaying ? EAnimToolIcon::Pause : EAnimToolIcon::Play;
		if (DrawAnimIconButton("##PlayPause", PlayIcon, true, bForwardPlaying ? "Pause" : "Play"))
		{
			if (bForwardPlaying) bPlaying = false;
			else { PlayRate = std::abs(PlayRate); bPlaying = true; }
		}
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	// ▶ 한 프레임 앞으로
	if (DrawAnimIconButton("##StepFwd", EAnimToolIcon::StepForward, true, "|>"))
	{
		if (FrameStep > 0.0f)
		{
			CurrentTime = std::min(Duration, CurrentTime + FrameStep);
			bPlaying = false;
		}
	}
	ImGui::SameLine(0.0f, ButtonSpacing);

	// ▶| 끝으로
	if (DrawAnimIconButton("##GoToEnd", EAnimToolIcon::GoToEnd, true, ">|"))
	{
		CurrentTime = Duration;
	}

	ImGui::SameLine(0.0f, GroupSpacing);

	// Loop 토글 — ON=Loop, OFF=Loop_Toggle(화살표)
	if (DrawAnimIconButton("##Loop", EAnimToolIcon::Loop, bLooping, bLooping ? "Loop:On" : "Loop:Off"))
	{
		bLooping = !bLooping;
	}

	ImGui::PopStyleColor(3);

	// Speed
	ImGui::SameLine(0.0f, GroupSpacing);
	ImGui::SetNextItemWidth(120.0f);
	float SpeedDisplay = std::abs(PlayRate);
	if (ImGui::SliderFloat("##Speed", &SpeedDisplay, 0.0f, 3.0f, "%.2fx"))
	{
		PlayRate = (PlayRate < 0.0f) ? -SpeedDisplay : SpeedDisplay;
	}
}

// =================================================================
// Notify 인라인 속성 편집 (timeline 패널 안)
// =================================================================

void FAnimSequenceEditorTab::RenderNotifyPropertyInline()
{
	if (!DataSource) return;
	const TArray<FAnimNotifyEntry>& Notifies = DataSource->GetNotifies();
	if (SelectedNotifyIndex < 0 || SelectedNotifyIndex >= static_cast<int32>(Notifies.size())) return;

	FAnimNotifyEntry Edited = Notifies[SelectedNotifyIndex];
	bool bChanged = false;

	ImGui::TextUnformatted("Selected Notify");
	ImGui::SameLine();
	ImGui::TextDisabled("(#%d)", SelectedNotifyIndex);

	char NameBuf[128];
	strncpy_s(NameBuf, sizeof(NameBuf), Edited.Name.c_str(), _TRUNCATE);
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::InputText("Name", NameBuf, sizeof(NameBuf)))
	{
		Edited.Name = NameBuf;
		bChanged = true;
	}
	ImGui::SameLine();

	const float Duration = DataSource->GetDuration();
	float Trigger = Edited.TriggerTime;
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::SliderFloat("Trigger", &Trigger, 0.0f, std::max(0.001f, Duration - Edited.Duration), "%.3fs"))
	{
		Edited.TriggerTime = std::clamp(Trigger, 0.0f, Duration - Edited.Duration);
		bChanged = true;
	}
	ImGui::SameLine();

	float DurationVal = Edited.Duration;
	const float MaxDuration = std::max(0.0f, Duration - Edited.TriggerTime);
	ImGui::SetNextItemWidth(140.0f);
	if (ImGui::SliderFloat("Duration", &DurationVal, 0.0f, MaxDuration, "%.3fs"))
	{
		Edited.Duration = std::clamp(DurationVal, 0.0f, MaxDuration);
		bChanged = true;
	}
	ImGui::SameLine();

	ImVec4 Color = ImGui::ColorConvertU32ToFloat4(Edited.ColorPacked);
	if (ImGui::ColorEdit3("##NotifyColor", &Color.x, ImGuiColorEditFlags_NoInputs))
	{
		Edited.ColorPacked = ImGui::ColorConvertFloat4ToU32(Color);
		bChanged = true;
	}
	ImGui::SameLine();

	if (ImGui::Button("Go"))
	{
		CurrentTime = Edited.TriggerTime;
		bPlaying = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Delete"))
	{
		DataSource->RemoveNotify(SelectedNotifyIndex);
		SelectedNotifyIndex = -1;
		return;
	}

	if (Edited.Duration <= 0.0f) ImGui::TextDisabled("Instant notify");
	else                          ImGui::TextDisabled("State notify (range)");

	ImGui::Separator();

	// Type별 payload 위젯 (R3: SoundId는 char buffer 경유, R7: None은 read-only)
	if (Edited.Type == EAnimNotifyType::None)
	{
		ImGui::TextDisabled("(no type — legacy)");
	}
	else if (Edited.Type == EAnimNotifyType::Sound)
	{
		// SoundManager에 등록된 effect ID 중 하나를 라디오로 선택. 자유 입력 미지원.
		// 등록되지 않은 ID는 여기서 선택할 수 없으므로 SoundManager::LoadEffect 호출 위치(CrossyGameModule 등)
		// 에 사전 등록이 필요하다.
		ImGui::PushID("SoundIdPicker");
		ImGui::TextUnformatted("SoundId:");
		ImGui::SameLine();
		ImGui::TextDisabled("(current: %s)", Edited.SoundId.empty() ? "<none>" : Edited.SoundId.c_str());

		ImGui::Indent();
		if (ImGui::RadioButton("<none>", Edited.SoundId.empty()))
		{
			if (!Edited.SoundId.empty())
			{
				Edited.SoundId.clear();
				bChanged = true;
			}
		}

		const TArray<FSoundId> RegisteredIds = FSoundManager::Get().GetRegisteredEffectIds();
		if (RegisteredIds.empty())
		{
			ImGui::TextDisabled("(no registered effects — register via FSoundManager::LoadEffect)");
		}
		for (const FSoundId &Id : RegisteredIds)
		{
			const bool bSelected = (Id == Edited.SoundId);
			if (ImGui::RadioButton(Id.c_str(), bSelected))
			{
				if (!bSelected)
				{
					Edited.SoundId = Id;
					bChanged = true;
				}
			}
		}
		ImGui::Unindent();
		ImGui::PopID();
	}
	else if (Edited.Type == EAnimNotifyType::CameraShake)
	{
		if (ImGui::CollapsingHeader("Shake Params", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// 상단 Trigger/Duration slider와 라벨이 겹치므로 ID scope를 분리한다.
			ImGui::PushID("ShakeParams");

			FCameraShakeParams &SP = Edited.ShakeParams;

			const char *Patterns[] = { "Sine", "Perlin" };
			int PatternIdx = static_cast<int>(SP.Pattern);
			if (ImGui::Combo("Pattern", &PatternIdx, Patterns, IM_ARRAYSIZE(Patterns)))
			{
				SP.Pattern = static_cast<ECameraShakePattern>(PatternIdx);
				bChanged = true;
			}

			if (ImGui::InputFloat("Duration", &SP.Duration))
			{
				SP.Duration = std::max(0.0f, SP.Duration);
				bChanged = true;
			}
			if (ImGui::InputFloat("BlendInTime", &SP.BlendInTime))
			{
				SP.BlendInTime = std::max(0.0f, SP.BlendInTime);
				bChanged = true;
			}
			if (ImGui::InputFloat("BlendOutTime", &SP.BlendOutTime))
			{
				SP.BlendOutTime = std::max(0.0f, SP.BlendOutTime);
				bChanged = true;
			}

			float Loc[3] = { SP.LocationAmplitude.X, SP.LocationAmplitude.Y, SP.LocationAmplitude.Z };
			if (ImGui::InputFloat3("LocationAmplitude", Loc))
			{
				SP.LocationAmplitude.X = Loc[0];
				SP.LocationAmplitude.Y = Loc[1];
				SP.LocationAmplitude.Z = Loc[2];
				bChanged = true;
			}

			float Rot[3] = { SP.RotationAmplitude.Pitch, SP.RotationAmplitude.Yaw, SP.RotationAmplitude.Roll };
			if (ImGui::InputFloat3("RotationAmplitude (P/Y/R)", Rot))
			{
				SP.RotationAmplitude.Pitch = Rot[0];
				SP.RotationAmplitude.Yaw = Rot[1];
				SP.RotationAmplitude.Roll = Rot[2];
				bChanged = true;
			}

			if (ImGui::InputFloat("FOVAmplitude", &SP.FOVAmplitude))
			{
				bChanged = true;
			}
			if (ImGui::InputFloat("Frequency", &SP.Frequency))
			{
				SP.Frequency = std::max(0.0f, SP.Frequency);
				bChanged = true;
			}
			if (ImGui::InputFloat("Roughness", &SP.Roughness))
			{
				SP.Roughness = std::max(0.0f, SP.Roughness);
				bChanged = true;
			}
			if (ImGui::Checkbox("ApplyInCameraLocalSpace", &SP.bApplyInCameraLocalSpace))
			{
				bChanged = true;
			}
			if (ImGui::Checkbox("SingleInstance", &SP.bSingleInstance))
			{
				bChanged = true;
			}
			if (ImGui::InputScalar("Seed", ImGuiDataType_U32, &SP.Seed))
			{
				bChanged = true;
			}

			ImGui::PopID();
		}
	}

	if (bChanged)
	{
		DataSource->UpdateNotify(SelectedNotifyIndex, Edited);
	}
}

// =================================================================
// Left panel: asset info + skeleton tree
// =================================================================

void FAnimSequenceEditorTab::RenderLeftPanel()
{
	FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;
	USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;

	if (ImGui::BeginChild("##AnimSeqAssetInfo", ImVec2(0, 0), false))
	{
		if (!DataSource)
		{
			ImGui::TextDisabled("No anim sequence loaded.");
			ImGui::Spacing();
			ImGui::TextWrapped("Open from a SkeletalMesh tab -> 'Edit in Anim Editor' button.");
		}
		else
		{
			ImGui::TextUnformatted("Asset");
			ImGui::Separator();
			ImGui::Text("Name:       %s", DataSource->GetName().c_str());
			ImGui::Text("Duration:   %.3f s", DataSource->GetDuration());
			ImGui::Text("Frame rate: %.1f fps", DataSource->GetFrameRate());
			ImGui::Text("Frames:     %d", DataSource->GetFrameCount());
			ImGui::Text("Tracks:     %d", static_cast<int>(DataSource->GetTracks().size()));

			ImGui::Spacing();
			if (AnimSequence)
			{
				if (ImGui::Button("Save Animation As...##AnimSeqSaveAs"))
				{
					// 시퀀스 이름이 비어 있으면 탭의 source path stem을 fallback으로 쓴다.
					FString DefaultStem = AnimSequence->GetSequenceName();
					if (DefaultStem.empty())
					{
						DefaultStem = ExtractFileStem(GetSourcePath());
					}
					if (DefaultStem.empty())
					{
						DefaultStem = "AnimSequence";
					}
					// "Skeleton|Skeleton|Foo" 처럼 들어오는 경우 디렉터리/구분자만 떼어낸 stem을 다시 추출.
					DefaultStem = ExtractFileStem(DefaultStem);

					const std::wstring DefaultFileNameW = FPaths::ToWide(DefaultStem) + L".asset";
					const std::wstring InitialDirW = FPaths::Combine(FPaths::RootDir(), L"Asset/Content/");

					FEditorFileDialogOptions Options;
					Options.Title = L"Save Anim Sequence As";
					Options.Filter = L"Asset Files (*.asset)\0*.asset\0All Files (*.*)\0*.*\0";
					Options.DefaultExtension = L"asset";
					Options.DefaultFileName = DefaultFileNameW.c_str();
					Options.InitialDirectory = InitialDirW.c_str();
					Options.bFileMustExist = false;
					Options.bPathMustExist = true;
					Options.bPromptOverwrite = true;
					Options.bReturnRelativeToProjectRoot = true;

					const FString SavePath = FEditorFileUtils::SaveFileDialog(Options);
					if (!SavePath.empty())
					{
						FMeshManager::SaveAnimSequenceToFile(AnimSequence, SavePath);
					}
				}
				ImGui::Spacing();
			}

			ImGui::TextUnformatted("Preview Mesh");
			ImGui::Separator();
			{
				const FString MeshName = PreviewMesh
					? ExtractFileStem(PreviewMesh->GetAssetPathFileName())
					: FString("None");
				ImGui::TextUnformatted(MeshName.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Load...##PreviewMeshLoad"))
				{
					const std::wstring InitialDirW = FPaths::Combine(FPaths::RootDir(), L"Asset/Content/");
					FEditorFileDialogOptions Options;
					Options.Title            = L"Select Skeletal Mesh";
					Options.Filter           = L"Asset Files (*.asset)\0*.asset\0All Files (*.*)\0*.*\0";
					Options.DefaultExtension = L"asset";
					Options.InitialDirectory = InitialDirW.c_str();
					Options.bFileMustExist   = true;
					Options.bPathMustExist   = true;
					Options.bReturnRelativeToProjectRoot = true;

					const FString SelectedPath = FEditorFileUtils::OpenFileDialog(Options);
					if (!SelectedPath.empty())
					{
						USkeletalMesh* NewMesh = FMeshManager::LoadSkeletalMeshFromFile(SelectedPath);
						if (NewMesh)
						{
							if (!FMeshManager::IsAnimSequenceCompatibleWithMesh(AnimSequence, NewMesh))
							{
								ImGui::OpenPopup("##IncompatibleMesh");
							}
							else
							{
								PreviewMesh = NewMesh;
								PreviewScene.SetPreviewMesh(NewMesh);
								if (USkeletalMeshComponent* Comp = PreviewScene.PreviewMeshComponent)
								{
									Comp->SetAnimation(AnimSequence);
									Comp->SetBakedAnimTime(0.0f);
									Comp->SetBakedAnimPaused(true);
								}
								SelectedBoneIndex = -1;
							}
						}
					}
				}
				if (PreviewMesh)
				{
					ImGui::SameLine();
					if (ImGui::SmallButton("Clear##PreviewMeshClear"))
					{
						PreviewMesh = nullptr;
						PreviewScene.SetPreviewMesh(nullptr);
						SelectedBoneIndex = -1;
					}
				}
			}
			if (ImGui::BeginPopup("##IncompatibleMesh"))
			{
				ImGui::TextUnformatted("Incompatible rig.");
				ImGui::TextDisabled("Root bone of animation not found in mesh.");
				ImGui::Spacing();
				if (ImGui::Button("OK", ImVec2(80, 0)))
				{
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}

			ImGui::Spacing();

			ImGui::TextUnformatted("Skeleton Tree");
			ImGui::Separator();

			const USkeleton* Skeleton = PreviewMesh ? PreviewMesh->GetSkeleton() : nullptr;
			if (!Skeleton || Skeleton->GetBones().empty())
			{
				ImGui::TextDisabled("No skeleton");
			}
			else
			{
				const TArray<FBoneInfo>& Bones = Skeleton->GetBones();
				
				if (PreviewViewportClient)
				{
					SelectedBoneIndex = PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone();
				}
				const int32 PrevSelected = SelectedBoneIndex;

				const int32 DoubleClicked = SkeletonTreeUtil::RenderSkeletonTree(
					Bones,
					SelectedBoneIndex,
					bScrollToSelectedBone,
					RequestSetOpenBoneIndex,
					bRequestSetOpenValue);

				if (DoubleClicked >= 0 && PreviewViewportClient)
				{
					PreviewViewportClient->FocusBone(PreviewMeshComponent, DoubleClicked);
				}
				if (PrevSelected != SelectedBoneIndex && PreviewViewportClient)
				{
					PreviewViewportClient->GetBoneSelectionManager().SelectBone(SelectedBoneIndex);
				}
			}
		}
	}
	ImGui::EndChild();
}

// =================================================================
// Right panel: placeholder (notify props는 timeline 안으로 이동)
// =================================================================

void FAnimSequenceEditorTab::RenderRightPanel()
{
	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	constexpr float SplitterH = 5.0f;
	constexpr float MinTop = 60.0f;
	constexpr float MinBottom = 100.0f;
	const float MaxTop = std::max(MinTop, Avail.y - MinBottom - SplitterH);
	RightPanelTopHeight = std::clamp(RightPanelTopHeight, MinTop, MaxTop);

	// 상단: Bone Transform 패널 (선택된 본의 Location/Rotation/Scale 편집)
	if (ImGui::BeginChild("##AnimSeqDetails", ImVec2(0, RightPanelTopHeight), false))
	{
		RenderBoneTransformPanel();
	}
	ImGui::EndChild();

	// 가변 splitter (Details ↔ Asset Browser)
	ImGui::InvisibleButton("##RPSplitter", ImVec2(-1.0f, SplitterH));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive())
	{
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	if (ImGui::IsItemActive())
	{
		RightPanelTopHeight += ImGui::GetIO().MouseDelta.y;
	}
	{
		const ImVec2 SpMin = ImGui::GetItemRectMin();
		const ImVec2 SpMax = ImGui::GetItemRectMax();
		const ImU32 SpColor = ImGui::IsItemActive() ? IM_COL32(100, 150, 200, 255)
		                                            : (ImGui::IsItemHovered() ? IM_COL32(110, 110, 115, 255)
		                                                                       : IM_COL32(60, 60, 65, 255));
		ImGui::GetWindowDrawList()->AddRectFilled(SpMin, SpMax, SpColor);
	}

	// 하단: Asset Browser
	if (ImGui::BeginChild("##AnimSeqAssetBrowser", ImVec2(0, 0), true))
	{
		ImGui::TextUnformatted("Asset Browser");
		ImGui::Separator();
		RenderAssetBrowser();
	}
	ImGui::EndChild();
}

void FAnimSequenceEditorTab::RenderAssetBrowser()
{
	const FSkeletalMesh* MeshAsset = PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr;
	UFBXSceneAsset* SceneAsset = PreviewMesh ? PreviewMesh->GetTypedOuter<UFBXSceneAsset>() : nullptr;
	if (!MeshAsset || !SceneAsset)
	{
		ImGui::TextDisabled("No mesh");
		return;
	}
	const int32 SequenceCount =
		FMeshManager::GetAnimSequenceCountForSkeletalMesh(SceneAsset, PreviewMesh);
	if (SequenceCount <= 0)
	{
		ImGui::TextDisabled("No anim sequences for this mesh");
		return;
	}

	// Filter input
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##AssetBrowserFilter", "Search Assets",
		AssetBrowserFilterBuf, sizeof(AssetBrowserFilterBuf));

	// Filter 소문자 사본 (case-insensitive 비교용)
	auto ToLower = [](FString S) -> FString
	{
		for (auto& C : S) C = static_cast<char>(std::tolower(static_cast<unsigned char>(C)));
		return S;
	};
	const FString FilterLower = ToLower(AssetBrowserFilterBuf);
	const FString& CurrentPath = GetSourcePath();

	if (ImGui::BeginTable("##AssetBrowserTable", 2,
		ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();

		for (int32 i = 0; i < SequenceCount; ++i)
		{
			FString Path;
			UAnimSequence* Seq = FMeshManager::FindAnimSequenceForSkeletalMesh(
				SceneAsset, PreviewMesh, i, &Path);

			// Name: 실제 anim sequence의 이름을 resolve해서 가져옴
			//       (없으면 path의 stem으로 fallback)
			FString Name;
			if (Seq)
			{
				Name = Seq->GetSequenceName().empty() ? Seq->GetFName().ToString() : Seq->GetSequenceName();
			}
			if (Name.empty()) Name = ExtractFileName(Path);
			else              Name = ExtractFileName(Name); // "Skeleton|Skeleton|X.anm" → "X.anm"

			if (!FilterLower.empty())
			{
				const FString NameLower = ToLower(Name);
				if (NameLower.find(FilterLower) == FString::npos) continue;
			}

			// Path 표시: "Asset/..."부터 시작 + "#Anim_..." suffix 제거
			FString DisplayPath = Path;
			const size_t AssetPos = DisplayPath.find("Asset");
			if (AssetPos != FString::npos) DisplayPath = DisplayPath.substr(AssetPos);
			const size_t HashPos = DisplayPath.find('#');
			if (HashPos != FString::npos) DisplayPath = DisplayPath.substr(0, HashPos);

			const bool bSelected = (Path == CurrentPath);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::PushID(i);
			if (ImGui::Selectable(Name.c_str(), bSelected,
				ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
			{
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					OpenAnimSequenceAsset(Path);
				}
			}
			ImGui::PopID();

			ImGui::TableSetColumnIndex(1);
			ImGui::TextDisabled("%s", DisplayPath.c_str());
		}
		ImGui::EndTable();
	}
}

void FAnimSequenceEditorTab::RenderBoneTransformPanel()
{
    USkeletalMeshComponent* PreviewMeshComponent = PreviewScene.PreviewMeshComponent;
    FSkeletalMeshViewerViewportClient* PreviewViewportClient = PreviewScene.PreviewViewportClient;

    ImGui::TextUnformatted("Transform");
    ImGui::Separator();

    // 본 선택은 BoneSelectionManager가 단일 진리이므로 매 프레임 sync한다.
    if (PreviewViewportClient)
    {
        SelectedBoneIndex = PreviewViewportClient->GetBoneSelectionManager().GetPrimarySelectedBone();
    }

    if (!PreviewMeshComponent || SelectedBoneIndex < 0)
    {
        ImGui::TextDisabled("No bone selected");
        return;
    }

    // 본 배열은 USkeleton에서 (FSkeletalMesh.Bones는 비어있음)
    const USkeleton* Skeleton = PreviewMesh ? PreviewMesh->GetSkeleton() : nullptr;
    if (!Skeleton || SelectedBoneIndex >= static_cast<int32>(Skeleton->GetBones().size()))
    {
        ImGui::TextDisabled("No bone selected");
        return;
    }

    const TArray<FMatrix>& LocalPoses = PreviewMeshComponent->GetLocalBonePoseMatrices();
    if (SelectedBoneIndex >= static_cast<int32>(LocalPoses.size()))
    {
        ImGui::TextDisabled("No bone pose available");
        return;
    }

    const FBoneInfo& Bone = Skeleton->GetBones()[SelectedBoneIndex];
    const FMatrix& LocalPose = LocalPoses[SelectedBoneIndex];
    const FVector LocationVector = LocalPose.GetLocation();
    const FVector RotationVector = GetRotationEulerNoScale(LocalPose);
    const FVector ScaleVector = LocalPose.GetScale();
    float Location[3] = { LocationVector.X, LocationVector.Y, LocationVector.Z };
    float Rotation[3] = { RotationVector.X, RotationVector.Y, RotationVector.Z };
    float Scale[3] = { ScaleVector.X, ScaleVector.Y, ScaleVector.Z };

    ImGui::TextUnformatted(Bone.Name.c_str());
    ImGui::Separator();

    bool bChanged = false;
    bChanged |= ImGui::DragFloat3("Location", Location, 0.1f, -100000.0f, 100000.0f, "%.3f");
    bChanged |= ImGui::DragFloat3("Rotation", Rotation, 0.1f, -360.0f, 360.0f, "%.3f");
    bChanged |= ImGui::DragFloat3("Scale", Scale, 0.01f, 0.001f, 100.0f, "%.3f");

    if (bChanged)
    {
        FVector NewLocation(Location[0], Location[1], Location[2]);
        FVector NewRotation(Rotation[0], Rotation[1], Rotation[2]);
        FVector NewScale(
            (std::max)(Scale[0], 0.001f),
            (std::max)(Scale[1], 0.001f),
            (std::max)(Scale[2], 0.001f));
        // 이 호출이 LocalBonePoseMatrices[i]를 갱신하고 BoneOverrideMask[i] = true 마킹한다.
        // 이후 Tick에서 ApplyEvaluatedPose가 마스크 보고 사용자 값을 유지.
        PreviewMeshComponent->SetBoneLocalPose(
            SelectedBoneIndex,
            MakeLocalPoseMatrix(NewLocation, NewRotation, NewScale));
    }

    // === Override 마스크 표시 + Reset ===
    ImGui::Separator();
    const bool bThisBoneOverridden = PreviewMeshComponent->IsBoneOverridden(SelectedBoneIndex);
    if (bThisBoneOverridden)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "* User Modified");
    }
    else
    {
        ImGui::TextDisabled("(Follows animation)");
    }

    if (ImGui::Button("Reset This Bone", ImVec2(140.0f, 0.0f)))
    {
        PreviewMeshComponent->ClearBoneOverride(SelectedBoneIndex);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset All Bones", ImVec2(140.0f, 0.0f)))
    {
        PreviewMeshComponent->ClearAllBoneOverrides();
    }
}
