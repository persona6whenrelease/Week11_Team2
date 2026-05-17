#include "Engine/Runtime/Launch.h"

#include "Engine/Runtime/EngineLoop.h"
#include "Engine/Platform/CrashDump.h"

namespace
{
    int CrashFilter(EXCEPTION_POINTERS* ExceptionPointers)
    {
        WriteCrashDump(ExceptionPointers);

        // 예외는 여기서 처리하고, __except 블록으로 진입시킨다.
        return EXCEPTION_EXECUTE_HANDLER;
    }

	int GuardedMain(HINSTANCE hInstance, int nShowCmd)
	{
		FEngineLoop EngineLoop;
		if (!EngineLoop.Init(hInstance, nShowCmd))
		{
			return -1;
		}

		const int ExitCode = EngineLoop.Run();
		EngineLoop.Shutdown();
		return ExitCode;
	}
}

int Launch(HINSTANCE hInstance, int nShowCmd)
{
	__try
	{
		return GuardedMain(hInstance, nShowCmd);
	}
	__except (CrashFilter(GetExceptionInformation()))
	{
		ExitProcess(static_cast<int>(GetExceptionCode()));
	}
}
