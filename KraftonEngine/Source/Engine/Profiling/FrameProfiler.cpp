#include "Profiling/FrameProfiler.h"

#include "Profiling/GPUProfiler.h"
#include "Profiling/SkinningStats.h"
#include "Profiling/Stats.h"

void FFrameProfiler::BeginFrame()
{
#if STATS
	FSkinningStats::BeginFrame();
#endif
}

void FFrameProfiler::BeginRenderFrame()
{
#if STATS
	FStatManager::Get().TakeSnapshot();
	FGPUProfiler::Get().TakeSnapshot();
	FGPUProfiler::Get().BeginFrame();
#endif
}

void FFrameProfiler::EndRenderFrame()
{
#if STATS
	FGPUProfiler::Get().EndFrame();
#endif
}
