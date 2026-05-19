#include "Editor/Subsystem/OverlayStatSystem.h"

#include "Editor/EditorEngine.h"
#include "Component/SkinnedMeshComponent.h"
#include "Object/Object.h"
#include "Engine/Profiling/Timer.h"
#include "Engine/Profiling/MemoryStats.h"
#include "Engine/Profiling/ShadowStats.h"
#include "Engine/Profiling/GPUProfiler.h"
#include "Engine/Profiling/SkinningStats.h"
#include "UI/SWindow.h"
#include "ImGui/imgui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// バイト数を適切な単位 (B / KB / MB / GB) に変換して文字列化
static int FormatBytes(char* Buffer, int32 BufferSize, const char* Label, uint64 Bytes)
{
	const double B = static_cast<double>(Bytes);
	const double KB = B / 1024.0;
	const double MB = KB / 1024.0;
	const double GB = MB / 1024.0;

	if (GB >= 1.0)
		return snprintf(Buffer, BufferSize, "%s : %.2f GB", Label, GB);
	if (MB >= 1.0)
		return snprintf(Buffer, BufferSize, "%s : %.2f MB", Label, MB);
	if (KB >= 1.0)
		return snprintf(Buffer, BufferSize, "%s : %.2f KB", Label, KB);
	return snprintf(Buffer, BufferSize, "%s : %llu B", Label, static_cast<unsigned long long>(Bytes));
}

void FOverlayStatSystem::AppendLine(TArray<FOverlayStatLine>& OutLines, float Y, const FString& Text) const
{
	FOverlayStatLine Line;
	Line.Text = Text;
	Line.ScreenPosition = FVector2(Layout.StartX, Y);
	OutLines.push_back(std::move(Line));
}

void FOverlayStatSystem::RecordPickingAttempt(double ElapsedMs)
{
	LastPickingTimeMs = ElapsedMs;
	AccumulatedPickingTimeMs += ElapsedMs;
	++PickingAttemptCount;
}

void FOverlayStatSystem::BuildFPSLines(const UEditorEngine& Editor, TArray<FString>& OutLines) const
{
	const FTimer* Timer = Editor.GetTimer();
	if (Timer)
	{
		constexpr double FPSAverageWindowSeconds = 0.3;
		const double CurrentTime = Timer->GetTotalTime();

		if (!bFPSAverageInitialized)
		{
			FPSAverageWindowStartTime = CurrentTime;
			FPSAccumulatedFrameTimeMs = 0.0;
			FPSAccumulatedFrameCount = 0;
			bFPSAverageInitialized = true;
		}

		FPSAccumulatedFrameTimeMs += Timer->GetFrameTimeMs();
		++FPSAccumulatedFrameCount;

		const double WindowElapsed = CurrentTime - FPSAverageWindowStartTime;
		if (WindowElapsed >= FPSAverageWindowSeconds && FPSAccumulatedFrameCount > 0)
		{
			const float AverageMS = static_cast<float>(FPSAccumulatedFrameTimeMs / FPSAccumulatedFrameCount);
			const float AverageFPS = AverageMS > 0.0f ? 1000.0f / AverageMS : 0.0f;

			char Buffer[128] = {};
			snprintf(Buffer, sizeof(Buffer), "FPS : %.1f (%.2f ms)", AverageFPS, AverageMS);
			CachedFPSLine = Buffer;

			FPSAverageWindowStartTime = CurrentTime;
			FPSAccumulatedFrameTimeMs = 0.0;
			FPSAccumulatedFrameCount = 0;
		}
	}
	else
	{
		CachedFPSLine = "FPS : 0.0 (0.00 ms)";
		bFPSAverageInitialized = false;
		FPSAccumulatedFrameTimeMs = 0.0;
		FPSAccumulatedFrameCount = 0;
	}

	if (CachedFPSLine.empty())
	{
		CachedFPSLine = "FPS : 0.0 (0.00 ms)";
	}

	OutLines.push_back(CachedFPSLine);

	if (bShowPickingTime)
	{
		char Buffer[160] = {};
		snprintf(Buffer, sizeof(Buffer), "Picking Time %.5f ms : Num Attempts %d : Accumulated Time %.5f ms",
			LastPickingTimeMs,
			static_cast<int32>(PickingAttemptCount),
			AccumulatedPickingTimeMs);
		CachedPickingLine = Buffer;
		OutLines.push_back(CachedPickingLine);
	}
}

void FOverlayStatSystem::BuildMemoryLines(TArray<FString>& OutLines) const
{
	char Buffer[128] = {};

	// 할당 횟수 (단위 없음)
	snprintf(Buffer, sizeof(Buffer), "Allocation Count : %u", MemoryStats::GetTotalAllocationCount());
	OutLines.push_back(FString(Buffer));

	// 바이트 단위 메모리 — 자동 단위 변환 (B/KB/MB/GB)
	struct { const char* Label; uint64 Bytes; } MemEntries[] = {
		{ "Total Allocated",       MemoryStats::GetTotalAllocationBytes() },
		{ "PixelShader Memory",    MemoryStats::GetPixelShaderMemory() },
		{ "VertexShader Memory",   MemoryStats::GetVertexShaderMemory() },
		{ "VertexBuffer Memory",   MemoryStats::GetVertexBufferMemory() },
		{ "IndexBuffer Memory",    MemoryStats::GetIndexBufferMemory() },
		{ "StaticMesh CPU Memory", MemoryStats::GetStaticMeshCPUMemory() },
		{ "Texture Memory",        MemoryStats::GetTextureMemory() },
	};

	for (const auto& Entry : MemEntries)
	{
		FormatBytes(Buffer, sizeof(Buffer), Entry.Label, Entry.Bytes);
		OutLines.push_back(FString(Buffer));
	}
}

void FOverlayStatSystem::BuildShadowLines(TArray<FString>& OutLines) const
{
#if STATS
	char Buffer[128] = {};

	OutLines.push_back(FString("--- Shadow ---"));

	// Shadow map 메모리
	FormatBytes(Buffer, sizeof(Buffer), "Shadow Map Memory", FShadowStats::ShadowMapMemoryBytes);
	OutLines.push_back(FString(Buffer));

	// GPU 시간 (GPUProfiler snapshot에서 "ShadowMapPass" 검색)
	const TArray<FStatEntry>& GPUSnapshot = FGPUProfiler::Get().GetGPUSnapshot();
	double ShadowGpuMs = 0.0;
	for (const FStatEntry& Entry : GPUSnapshot)
	{
		if (Entry.Name && strcmp(Entry.Name, "ShadowMapPass") == 0)
		{
			ShadowGpuMs = Entry.LastTime * 1000.0;
			break;
		}
	}
	snprintf(Buffer, sizeof(Buffer), "Shadow GPU Time : %.3f ms", ShadowGpuMs);
	OutLines.push_back(FString(Buffer));

	// Shadow draw call 수
	snprintf(Buffer, sizeof(Buffer), "Shadow Draw Calls : %u", FShadowStats::ShadowDrawCallCount);
	OutLines.push_back(FString(Buffer));

	// 라이트별 shadow caster 수
	snprintf(Buffer, sizeof(Buffer), "Shadow Casters (Spot: %u  Point: %u  Dir: %u)",
		FShadowStats::SpotLightCasterCount,
		FShadowStats::PointLightCasterCount,
		FShadowStats::DirectionalLightCasterCount);
	OutLines.push_back(FString(Buffer));

	// Shadow-casting 라이트 수
	snprintf(Buffer, sizeof(Buffer), "Shadow Lights (Spot: %u  Point: %u  Dir: %u)",
		FShadowStats::SpotLightShadowCount,
		FShadowStats::PointLightShadowCount,
		FShadowStats::DirectionalLightShadowCount);
	OutLines.push_back(FString(Buffer));

	// directional light CSM Shadow map 해상도
	snprintf(Buffer, sizeof(Buffer), "CSM Shadow Map Resolution : %ux%u",
		FShadowStats::ShadowMapResolution, FShadowStats::ShadowMapResolution);
	OutLines.push_back(FString(Buffer));
#else
	OutLines.push_back(FString("Shadow stats unavailable (STATS=0)"));
#endif
}

void FOverlayStatSystem::BuildSkinningLines(TArray<FString>& OutLines) const
{
#if STATS
    uint32 VertexCount = 0;
    uint32 BoneCount = 0;
    uint32 ComponentCount = 0;
    uint32 DrawCallCount = 0;
    uint32 CPUComponentCount = 0;
    uint32 GPUComponentCount = 0;

    for (UObject* Obj : GUObjectArray)
    {
        USkinnedMeshComponent* Skinned = Cast<USkinnedMeshComponent>(Obj);
        if (!Skinned)
        {
            continue;
        }

        USkeletalMesh* Mesh = Skinned->GetSkeletalMesh();
        const FSkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
        if (!MeshAsset)
        {
            continue;
        }

        ++ComponentCount;
        VertexCount += static_cast<uint32>(MeshAsset->Vertices.size());
        DrawCallCount += static_cast<uint32>(Mesh->GetSections().size());

        const USkeleton* Skeleton = Mesh->GetSkeleton();
        if (Skeleton)
        {
            BoneCount += static_cast<uint32>(Skeleton->GetBones().size());
        }

        if (Skinned->ShouldUseGPUSkinning())
        {
            ++GPUComponentCount;
        }
        else
        {
            ++CPUComponentCount;
        }
    }

    FSkinningStats::SetSceneCounts(VertexCount, BoneCount, ComponentCount, DrawCallCount);
    FSkinningStats::SetComponentModeCounts(CPUComponentCount, GPUComponentCount);

    char Buffer[160] = {};
    OutLines.push_back(FString("--- Skinning ---"));

    snprintf(Buffer, sizeof(Buffer), "[Mode: %s]", FSkinningStats::GetModeLabel());
    OutLines.push_back(FString(Buffer));

    snprintf(Buffer, sizeof(Buffer), "Component Split          : CPU %u / GPU %u", CPUComponentCount, GPUComponentCount);
    OutLines.push_back(FString(Buffer));

    OutLines.push_back(FString("[Count]"));
    snprintf(Buffer, sizeof(Buffer), "Vertex Count             : %u", VertexCount);
    OutLines.push_back(FString(Buffer));
    snprintf(Buffer, sizeof(Buffer), "Bone Count               : %u", BoneCount);
    OutLines.push_back(FString(Buffer));
    snprintf(Buffer, sizeof(Buffer), "Component Count          : %u", ComponentCount);
    OutLines.push_back(FString(Buffer));
    snprintf(Buffer, sizeof(Buffer), "Draw Call Count          : %u", DrawCallCount);
    OutLines.push_back(FString(Buffer));

    double GPUSkinningTimeMs = 0.0;
    const TArray<FStatEntry>& GPUSnapshot = FGPUProfiler::Get().GetGPUSnapshot();
    for (const FStatEntry& Entry : GPUSnapshot)
    {
        if (Entry.Name && strcmp(Entry.Name, "SkinningCompute") == 0)
        {
            GPUSkinningTimeMs = Entry.LastTime * 1000.0;
            break;
        }
    }
    if (GPUSkinningTimeMs <= 0.0)
    {
        GPUSkinningTimeMs = FSkinningStats::GetGPUSkeletalPassTimeMs();
    }

    OutLines.push_back(FString("[Time]"));
    snprintf(Buffer, sizeof(Buffer), "Pose Sampling Time       : %.3f ms", FSkinningStats::GetPoseSamplingTimeMs());
    OutLines.push_back(FString(Buffer));
    snprintf(Buffer, sizeof(Buffer), "Skinning Matrix Update   : %.3f ms", FSkinningStats::GetSkinningMatrixUpdateTimeMs());
    OutLines.push_back(FString(Buffer));

    if (CPUComponentCount > 0)
    {
        snprintf(Buffer, sizeof(Buffer), "CPU Vertex Skinning Time : %.3f ms", FSkinningStats::GetCPUSkinningTimeMs());
    }
    else
    {
        snprintf(Buffer, sizeof(Buffer), "CPU Vertex Skinning Time : Not Measured");
    }
    OutLines.push_back(FString(Buffer));

    if (GPUComponentCount > 0)
    {
        snprintf(Buffer, sizeof(Buffer), "GPU Skeletal Pass Time   : %.3f ms", GPUSkinningTimeMs);
    }
    else
    {
        snprintf(Buffer, sizeof(Buffer), "GPU Skeletal Pass Time   : Not Measured");
    }
    OutLines.push_back(FString(Buffer));

    if (GPUComponentCount > 0)
    {
        snprintf(Buffer, sizeof(Buffer), "Bone Matrix Upload Time  : %.3f ms", FSkinningStats::GetBoneUploadTimeMs());
    }
    else
    {
        snprintf(Buffer, sizeof(Buffer), "Bone Matrix Upload Time  : Not Measured");
    }
    OutLines.push_back(FString(Buffer));
#else
    OutLines.push_back(FString("Skinning stats unavailable (STATS=0)"));
#endif
}

void FOverlayStatSystem::BuildLines(const UEditorEngine& Editor, TArray<FOverlayStatLine>& OutLines) const
{
	OutLines.clear();

	uint32 EstimatedLineCount = 0;
	if (bShowFPS)
	{
		++EstimatedLineCount;
	}
	if (bShowPickingTime)
	{
		++EstimatedLineCount;
	}
	if (bShowMemory)
	{
		EstimatedLineCount += 8;
	}
	if (bShowShadow)
	{
		EstimatedLineCount += 8;
	}
    if (bShowSkinning)
    {
        EstimatedLineCount += 14;
    }
	OutLines.reserve(EstimatedLineCount);

	TArray<FString> Lines;
	float CurrentY = Layout.StartY;
	auto AppendGroup = [&](const TArray<FString>& GroupLines)
		{
			for (const FString& Line : GroupLines)
			{
				AppendLine(OutLines, CurrentY, Line);
				CurrentY += Layout.LineHeight;
			}
			if (!GroupLines.empty())
			{
				CurrentY += Layout.GroupSpacing;
			}
		};

	if (bShowFPS)
	{
		Lines.clear();
		BuildFPSLines(Editor, Lines);
		AppendGroup(Lines);
	}

	if (bShowMemory)
	{
		Lines.clear();
		BuildMemoryLines(Lines);
		AppendGroup(Lines);
	}

	if (bShowShadow)
	{
		Lines.clear();
		BuildShadowLines(Lines);
		AppendGroup(Lines);
	}

    if (bShowSkinning)
    {
        Lines.clear();
        BuildSkinningLines(Lines);
        AppendGroup(Lines);
    }
}

TArray<FOverlayStatLine> FOverlayStatSystem::BuildLines(const UEditorEngine& Editor) const
{
	TArray<FOverlayStatLine> Result;
	BuildLines(Editor, Result);
	return Result;
}

void FOverlayStatSystem::RenderImGui(const UEditorEngine& Editor, const FRect& ViewportRect) const
{
	if (ViewportRect.Width <= 1.0f || ViewportRect.Height <= 1.0f)
	{
		return;
	}

	constexpr float PaddingX = 10.0f;
	constexpr float PaddingY = 30.0f;
	constexpr float WindowGap = 6.0f;
	constexpr float ColumnGap = 8.0f;
	constexpr float InnerPaddingX = 10.0f;
	constexpr float InnerPaddingY = 8.0f;
	const float ViewportLeft = ViewportRect.X;
	const float ViewportTop = ViewportRect.Y;
	const float ViewportRight = ViewportRect.X + ViewportRect.Width;
	const float ViewportBottom = ViewportRect.Y + ViewportRect.Height;

	float CurrentX = ViewportLeft + PaddingX;
	float CurrentY = ViewportTop + PaddingY;
	float CurrentColumnWidth = 0.0f;
	ImDrawList* ForegroundDrawList = ImGui::GetForegroundDrawList();
	if (!ForegroundDrawList)
	{
		return;
	}

	const ImVec4 TitleColor(1.0f, 1.0f, 1.0f, 0.95f);
	const ImVec4 TextColor(0.92f, 0.95f, 0.98f, 0.95f);
	const ImU32 SeparatorColor = IM_COL32(255, 255, 255, 48);

	auto RenderWindow = [&](const char* WindowID, const char* Title, const ImVec4& BgColor, const TArray<FString>& Lines)
		{
			(void)WindowID;
			if (Lines.empty())
			{
				return;
			}

			float MaxTextWidth = ImGui::CalcTextSize(Title).x;
			for (const FString& Line : Lines)
			{
				MaxTextWidth = (std::max)(MaxTextWidth, ImGui::CalcTextSize(Line.c_str()).x);
			}

			const float TextLineHeight = ImGui::GetTextLineHeightWithSpacing();
			const float SeparatorHeight = 6.0f;
			const float EstimatedHeight =
				InnerPaddingY * 2.0f +
				TextLineHeight +
				SeparatorHeight +
				TextLineHeight * static_cast<float>(Lines.size());
			const float EstimatedWidth = InnerPaddingX * 2.0f + MaxTextWidth;
			if (CurrentY > ViewportTop + PaddingY && CurrentY + EstimatedHeight > ViewportBottom - PaddingY)
			{
				CurrentX += CurrentColumnWidth + ColumnGap;
				CurrentY = ViewportTop + PaddingY;
				CurrentColumnWidth = 0.0f;
			}
			CurrentX = (std::max)(ViewportLeft + PaddingX, (std::min)(CurrentX, ViewportRight - PaddingX - EstimatedWidth));

			const ImVec2 RectMin(CurrentX, CurrentY);
			const ImVec2 RectMax(CurrentX + EstimatedWidth, CurrentY + EstimatedHeight);
			ForegroundDrawList->AddRectFilled(RectMin, RectMax, ImGui::ColorConvertFloat4ToU32(BgColor), 4.0f);

			float TextX = CurrentX + InnerPaddingX;
			float TextY = CurrentY + InnerPaddingY;
			ForegroundDrawList->AddText(ImVec2(TextX, TextY), ImGui::ColorConvertFloat4ToU32(TitleColor), Title);
			TextY += TextLineHeight;

			ForegroundDrawList->AddLine(
				ImVec2(TextX, TextY),
				ImVec2(CurrentX + EstimatedWidth - InnerPaddingX, TextY),
				SeparatorColor,
				1.0f);
			TextY += SeparatorHeight;

			for (const FString& Line : Lines)
			{
				ForegroundDrawList->AddText(
					ImVec2(TextX, TextY),
					ImGui::ColorConvertFloat4ToU32(TextColor),
					Line.c_str());
				TextY += TextLineHeight;
			}

			CurrentY += EstimatedHeight + WindowGap;
			CurrentColumnWidth = (std::max)(CurrentColumnWidth, EstimatedWidth);
		};

	TArray<FString> Lines;
	if (bShowFPS)
	{
		BuildFPSLines(Editor, Lines);
		RenderWindow("##StatFPSOverlay", "Stat FPS", ImVec4(0.05f, 0.09f, 0.12f, 0.62f), Lines);
	}

	if (bShowMemory)
	{
		Lines.clear();
		BuildMemoryLines(Lines);
		RenderWindow("##StatMemoryOverlay", "Stat Memory", ImVec4(0.10f, 0.07f, 0.04f, 0.62f), Lines);
	}

	if (bShowShadow)
	{
		Lines.clear();
		BuildShadowLines(Lines);
		RenderWindow("##StatShadowOverlay", "Stat Shadow", ImVec4(0.08f, 0.05f, 0.12f, 0.62f), Lines);
	}

    if (bShowSkinning)
    {
        Lines.clear();
        BuildSkinningLines(Lines);
        RenderWindow("##StatSkinningOverlay", "Stat Skinning", ImVec4(0.05f, 0.11f, 0.08f, 0.62f), Lines);
    }
}
