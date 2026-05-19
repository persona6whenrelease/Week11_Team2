#pragma once

class FFrameProfiler
{
public:
	static void BeginFrame();
	static void BeginRenderFrame();
	static void EndRenderFrame();
};
