#pragma once

#include "Profiling/Stats.h"

#if STATS
struct FSkinningStats
{
	static constexpr uint32 WindowSize = 60;

	static void BeginFrame();

	static void AddPoseSamplingTime(double Seconds);
	static void AddSkinningMatrixUpdateTime(double Seconds);
	static void AddCPUSkinningTime(double Seconds);
	static void AddGPUSkeletalPassTime(double Seconds);
	static void AddBoneUploadTime(double Seconds);

	static void SetSceneCounts(uint32 InVertexCount, uint32 InBoneCount, uint32 InComponentCount, uint32 InDrawCallCount);
	static void SetComponentModeCounts(uint32 InCPUComponentCount, uint32 InGPUComponentCount);

	static double GetPoseSamplingTimeMs();
	static double GetSkinningMatrixUpdateTimeMs();
	static double GetCPUSkinningTimeMs();
	static double GetGPUSkeletalPassTimeMs();
	static double GetBoneUploadTimeMs();

	static uint32 GetVertexCount() { return VertexCount; }
	static uint32 GetBoneCount() { return BoneCount; }
	static uint32 GetComponentCount() { return ComponentCount; }
	static uint32 GetDrawCallCount() { return DrawCallCount; }
	static uint32 GetCPUComponentCount() { return CPUComponentCount; }
	static uint32 GetGPUComponentCount() { return GPUComponentCount; }
	static const char* GetModeLabel();

private:
	static void PushFrameSample(double Samples[WindowSize], uint32& Head, uint32& Count, double& Sum, double Seconds);
	static void AddToCurrentFrame(double Samples[WindowSize], uint32 Head, double& Sum, double Seconds);
	static double GetAverageMs(double Sum, uint32 Count);

	static double PoseSamplingSamples[WindowSize];
	static double SkinningMatrixUpdateSamples[WindowSize];
	static double CPUSkinningSamples[WindowSize];
	static double GPUSkeletalPassSamples[WindowSize];
	static double BoneUploadSamples[WindowSize];

	static uint32 PoseSamplingHead;
	static uint32 SkinningMatrixUpdateHead;
	static uint32 CPUSkinningHead;
	static uint32 GPUSkeletalPassHead;
	static uint32 BoneUploadHead;

	static uint32 PoseSamplingCount;
	static uint32 SkinningMatrixUpdateCount;
	static uint32 CPUSkinningCount;
	static uint32 GPUSkeletalPassCount;
	static uint32 BoneUploadCount;

	static double PoseSamplingSum;
	static double SkinningMatrixUpdateSum;
	static double CPUSkinningSum;
	static double GPUSkeletalPassSum;
	static double BoneUploadSum;

	static uint32 VertexCount;
	static uint32 BoneCount;
	static uint32 ComponentCount;
	static uint32 DrawCallCount;
	static uint32 CPUComponentCount;
	static uint32 GPUComponentCount;
};
#endif
