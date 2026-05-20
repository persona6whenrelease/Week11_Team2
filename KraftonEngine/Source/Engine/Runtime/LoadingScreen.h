#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class FLoadingScreen
{
public:
    // bOverlay: dims existing window content instead of filling with solid black
    void Begin(HWND InHWnd, bool bOverlay = false);
    // Percent: 0-100 shows a progress bar; -1 = indeterminate (spinner only)
    void Update(const wchar_t* StatusText, int Percent = -1);
    void End();

private:
    void AnimationLoop();
    void Render(const wchar_t* StatusText, int Percent, int Frame);

    HWND HWnd = nullptr;
    HWND OverlayHWnd = nullptr;
    bool bOverlayMode = false;
    std::atomic<bool> bRunning{ false };
    std::atomic<int> CurrentPercent{ -1 };
    std::mutex StatusMutex;
    std::wstring CurrentStatus{ L"Initializing..." };
    std::thread AnimThread;
};
