#include "Profiling/SkinningStats.h"

#if STATS
double FSkinningStats::PoseSamplingSamples[WindowSize] = {};
double FSkinningStats::SkinningMatrixUpdateSamples[WindowSize] = {};
double FSkinningStats::CPUSkinningSamples[WindowSize] = {};
double FSkinningStats::GPUSkeletalPassSamples[WindowSize] = {};
double FSkinningStats::BoneUploadSamples[WindowSize] = {};

uint32 FSkinningStats::PoseSamplingHead = 0;
uint32 FSkinningStats::SkinningMatrixUpdateHead = 0;
uint32 FSkinningStats::CPUSkinningHead = 0;
uint32 FSkinningStats::GPUSkeletalPassHead = 0;
uint32 FSkinningStats::BoneUploadHead = 0;

uint32 FSkinningStats::PoseSamplingCount = 0;
uint32 FSkinningStats::SkinningMatrixUpdateCount = 0;
uint32 FSkinningStats::CPUSkinningCount = 0;
uint32 FSkinningStats::GPUSkeletalPassCount = 0;
uint32 FSkinningStats::BoneUploadCount = 0;

double FSkinningStats::PoseSamplingSum = 0.0;
double FSkinningStats::SkinningMatrixUpdateSum = 0.0;
double FSkinningStats::CPUSkinningSum = 0.0;
double FSkinningStats::GPUSkeletalPassSum = 0.0;
double FSkinningStats::BoneUploadSum = 0.0;

uint32 FSkinningStats::VertexCount = 0;
uint32 FSkinningStats::BoneCount = 0;
uint32 FSkinningStats::ComponentCount = 0;
uint32 FSkinningStats::DrawCallCount = 0;
uint32 FSkinningStats::CPUComponentCount = 0;
uint32 FSkinningStats::GPUComponentCount = 0;

void FSkinningStats::BeginFrame()
{
	PushFrameSample(PoseSamplingSamples, PoseSamplingHead, PoseSamplingCount, PoseSamplingSum, 0.0);
	PushFrameSample(SkinningMatrixUpdateSamples, SkinningMatrixUpdateHead, SkinningMatrixUpdateCount, SkinningMatrixUpdateSum, 0.0);
	PushFrameSample(CPUSkinningSamples, CPUSkinningHead, CPUSkinningCount, CPUSkinningSum, 0.0);
	PushFrameSample(GPUSkeletalPassSamples, GPUSkeletalPassHead, GPUSkeletalPassCount, GPUSkeletalPassSum, 0.0);
	PushFrameSample(BoneUploadSamples, BoneUploadHead, BoneUploadCount, BoneUploadSum, 0.0);
}

void FSkinningStats::AddPoseSamplingTime(double Seconds)
{
	AddToCurrentFrame(PoseSamplingSamples, PoseSamplingHead, PoseSamplingSum, Seconds);
}

void FSkinningStats::AddSkinningMatrixUpdateTime(double Seconds)
{
	AddToCurrentFrame(SkinningMatrixUpdateSamples, SkinningMatrixUpdateHead, SkinningMatrixUpdateSum, Seconds);
}

void FSkinningStats::AddCPUSkinningTime(double Seconds)
{
	AddToCurrentFrame(CPUSkinningSamples, CPUSkinningHead, CPUSkinningSum, Seconds);
}

void FSkinningStats::AddGPUSkeletalPassTime(double Seconds)
{
	AddToCurrentFrame(GPUSkeletalPassSamples, GPUSkeletalPassHead, GPUSkeletalPassSum, Seconds);
}

void FSkinningStats::AddBoneUploadTime(double Seconds)
{
	AddToCurrentFrame(BoneUploadSamples, BoneUploadHead, BoneUploadSum, Seconds);
}

void FSkinningStats::SetSceneCounts(uint32 InVertexCount, uint32 InBoneCount, uint32 InComponentCount, uint32 InDrawCallCount)
{
	VertexCount = InVertexCount;
	BoneCount = InBoneCount;
	ComponentCount = InComponentCount;
	DrawCallCount = InDrawCallCount;
}

void FSkinningStats::SetComponentModeCounts(uint32 InCPUComponentCount, uint32 InGPUComponentCount)
{
	CPUComponentCount = InCPUComponentCount;
	GPUComponentCount = InGPUComponentCount;
}

double FSkinningStats::GetPoseSamplingTimeMs()
{
	return GetAverageMs(PoseSamplingSum, PoseSamplingCount);
}

double FSkinningStats::GetSkinningMatrixUpdateTimeMs()
{
	return GetAverageMs(SkinningMatrixUpdateSum, SkinningMatrixUpdateCount);
}

double FSkinningStats::GetCPUSkinningTimeMs()
{
	return GetAverageMs(CPUSkinningSum, CPUSkinningCount);
}

double FSkinningStats::GetGPUSkeletalPassTimeMs()
{
	return GetAverageMs(GPUSkeletalPassSum, GPUSkeletalPassCount);
}

double FSkinningStats::GetBoneUploadTimeMs()
{
	return GetAverageMs(BoneUploadSum, BoneUploadCount);
}

double FSkinningStats::GetLastPoseSamplingTimeMs()
{
	return GetLastMs(PoseSamplingSamples, PoseSamplingHead, PoseSamplingCount);
}

double FSkinningStats::GetLastSkinningMatrixUpdateTimeMs()
{
	return GetLastMs(SkinningMatrixUpdateSamples, SkinningMatrixUpdateHead, SkinningMatrixUpdateCount);
}

double FSkinningStats::GetLastCPUSkinningTimeMs()
{
	return GetLastMs(CPUSkinningSamples, CPUSkinningHead, CPUSkinningCount);
}

double FSkinningStats::GetLastGPUSkeletalPassTimeMs()
{
	return GetLastMs(GPUSkeletalPassSamples, GPUSkeletalPassHead, GPUSkeletalPassCount);
}

double FSkinningStats::GetLastBoneUploadTimeMs()
{
	return GetLastMs(BoneUploadSamples, BoneUploadHead, BoneUploadCount);
}

const char* FSkinningStats::GetModeLabel()
{
	if (CPUComponentCount > 0 && GPUComponentCount > 0)
	{
		return "Mixed (CPU + GPU)";
	}
	if (GPUComponentCount > 0)
	{
		return "GPU Skinning";
	}
	if (CPUComponentCount > 0)
	{
		return "CPU Skinning";
	}
	return "None";
}

void FSkinningStats::PushFrameSample(double Samples[WindowSize], uint32& Head, uint32& Count, double& Sum, double Seconds)
{
	if (Count == WindowSize)
	{
		Sum -= Samples[Head];
	}
	else
	{
		++Count;
	}

	Samples[Head] = Seconds;
	Sum += Seconds;
	Head = (Head + 1) % WindowSize;
}

void FSkinningStats::AddToCurrentFrame(double Samples[WindowSize], uint32 Head, double& Sum, double Seconds)
{
	Samples[(Head + WindowSize - 1) % WindowSize] += Seconds;
	Sum += Seconds;
}

double FSkinningStats::GetAverageMs(double Sum, uint32 Count)
{
	return Count > 0 ? (Sum / static_cast<double>(Count)) * 1000.0 : 0.0;
}

double FSkinningStats::GetLastMs(const double Samples[WindowSize], uint32 Head, uint32 Count)
{
	if (Count == 0)
	{
		return 0.0;
	}

	return Samples[(Head + WindowSize - 1) % WindowSize] * 1000.0;
}
#endif
