#include "Engine/Runtime/LoadingScreen.h"

#include <algorithm>
#include <chrono>
#include <cmath>

void FLoadingScreen::Begin(HWND InHWnd, bool bOverlay)
{
    HWnd = InHWnd;
    bOverlayMode = bOverlay;

    if (bOverlay && InHWnd)
    {
        // Create a separate topmost layered window so the spinner composites
        // above the D3D11 swap chain (GDI draws to the same HWND are hidden by DWM).
        static ATOM OverlayClass = 0;
        if (!OverlayClass)
        {
            WNDCLASSEXW Wc = {};
            Wc.cbSize        = sizeof(Wc);
            Wc.lpfnWndProc   = DefWindowProcW;
            Wc.hInstance     = GetModuleHandleW(nullptr);
            Wc.lpszClassName = L"FLoadingScreenOverlay";
            OverlayClass = RegisterClassExW(&Wc);
        }

        RECT R;
        GetWindowRect(InHWnd, &R);
        OverlayHWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            L"FLoadingScreenOverlay", nullptr,
            WS_POPUP | WS_VISIBLE,
            R.left, R.top, R.right - R.left, R.bottom - R.top,
            InHWnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        if (OverlayHWnd)
        {
            // Whole-window alpha: ~80% opaque dark overlay
            SetLayeredWindowAttributes(OverlayHWnd, 0, 200, LWA_ALPHA);
        }
    }

    bRunning.store(true);
    AnimThread = std::thread(&FLoadingScreen::AnimationLoop, this);
}

void FLoadingScreen::Update(const wchar_t* StatusText, int Percent)
{
    std::lock_guard<std::mutex> Lock(StatusMutex);
    CurrentStatus = StatusText;
    CurrentPercent.store(Percent);
}

void FLoadingScreen::End()
{
    bRunning.store(false);
    if (AnimThread.joinable())
    {
        AnimThread.join();
    }
    if (OverlayHWnd)
    {
        DestroyWindow(OverlayHWnd);
        OverlayHWnd = nullptr;
    }
    HWnd = nullptr;
}

void FLoadingScreen::AnimationLoop()
{
    int Frame = 0;
    while (bRunning.load())
    {
        std::wstring Status;
        int Percent;
        {
            std::lock_guard<std::mutex> Lock(StatusMutex);
            Status = CurrentStatus;
            Percent = CurrentPercent.load();
        }
        Render(Status.c_str(), Percent, Frame++);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
}

void FLoadingScreen::Render(const wchar_t* StatusText, int InPercent, int InFrame)
{
    // Overlay mode draws into a separate topmost window; non-overlay into the main HWND.
    HWND RenderHWnd = (bOverlayMode && OverlayHWnd) ? OverlayHWnd : HWnd;
    if (!RenderHWnd)
    {
        return;
    }

    RECT ClientRect;
    GetClientRect(RenderHWnd, &ClientRect);
    const int W = ClientRect.right;
    const int H = ClientRect.bottom;
    if (W <= 0 || H <= 0)
    {
        return;
    }

    int CX = W / 2;
    int CY = H / 2;

    if (!bOverlayMode)
    {
        // 실제 보이는 영역 기준으로 중앙 계산 (startup screen only)
        RECT WindowRect;
        GetWindowRect(RenderHWnd, &WindowRect);
        HMONITOR Monitor = MonitorFromWindow(RenderHWnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO MonInfo = { sizeof(MONITORINFO) };
        GetMonitorInfo(Monitor, &MonInfo);
        const RECT& Work = MonInfo.rcWork;

        const int VisLeft   = (std::max)(0, static_cast<int>(Work.left   - WindowRect.left));
        const int VisTop    = (std::max)(0, static_cast<int>(Work.top    - WindowRect.top));
        const int VisRight  = (std::min)(W, static_cast<int>(Work.right  - WindowRect.left));
        const int VisBottom = (std::min)(H, static_cast<int>(Work.bottom - WindowRect.top));

        CX = (VisLeft + VisRight) / 2;
        CY = (VisTop + VisBottom) / 2;
    }

    // 더블 버퍼링
    HDC Hdc = GetDC(RenderHWnd);
    HDC MemDC = CreateCompatibleDC(Hdc);
    HBITMAP MemBitmap = CreateCompatibleBitmap(Hdc, W, H);
    HBITMAP OldBitmap = static_cast<HBITMAP>(SelectObject(MemDC, MemBitmap));

    SetBkMode(MemDC, TRANSPARENT);

    // 배경 채우기
    HBRUSH BgBrush = CreateSolidBrush(bOverlayMode ? RGB(0, 0, 0) : RGB(20, 20, 28));
    FillRect(MemDC, &ClientRect, BgBrush);
    DeleteObject(BgBrush);

    if (!bOverlayMode)
    {
        // 제목 (startup screen only — overlay는 layered window가 투명도 처리)
        HFONT TitleFont = CreateFontW(
            52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SelectObject(MemDC, TitleFont);
        SetTextColor(MemDC, RGB(240, 240, 250));
        RECT TitleRect = { 0, CY - 100, W, CY - 40 };
        DrawTextW(MemDC, L"Game Tech Lab", -1, &TitleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(TitleFont);
    }

    // 스피너: 8개 점이 원을 따라 회전
    const int NumDots    = 8;
    const int SpinRadius = 22;
    const int DotR       = 4;
    const int SpinCY     = CY + 10;
    const int ActiveDot  = InFrame % NumDots;

    HPEN NullPen = CreatePen(PS_NULL, 0, 0);
    HPEN OldPen  = static_cast<HPEN>(SelectObject(MemDC, NullPen));

    for (int i = 0; i < NumDots; i++)
    {
        // 12시 방향에서 시작
        float Angle = static_cast<float>(i) / NumDots * 6.28318f - 1.5708f;
        int DotX = CX      + static_cast<int>(cosf(Angle) * SpinRadius);
        int DotY = SpinCY  + static_cast<int>(sinf(Angle) * SpinRadius);

        // 활성 점에서 멀수록 어두워짐
        int Dist = (ActiveDot - i + NumDots) % NumDots;
        float T  = 1.0f - static_cast<float>(Dist) / NumDots;

        int R = 35 + static_cast<int>((79  - 35) * T);
        int G = 50 + static_cast<int>((193 - 50) * T);
        int B = 65 + static_cast<int>((233 - 65) * T);

        HBRUSH DotBrush = CreateSolidBrush(RGB(R, G, B));
        HBRUSH OldBrush = static_cast<HBRUSH>(SelectObject(MemDC, DotBrush));
        Ellipse(MemDC, DotX - DotR, DotY - DotR, DotX + DotR, DotY + DotR);
        SelectObject(MemDC, OldBrush);
        DeleteObject(DotBrush);
    }

    SelectObject(MemDC, OldPen);
    DeleteObject(NullPen);

    // 상태 텍스트
    HFONT StatusFont = CreateFontW(
        20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    SelectObject(MemDC, StatusFont);
    SetTextColor(MemDC, RGB(130, 140, 165));
    RECT StatusRect = { 0, SpinCY + SpinRadius + 16, W, SpinCY + SpinRadius + 48 };
    DrawTextW(MemDC, StatusText, -1, &StatusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(StatusFont);

    // 진행률 바 (0-100일 때만 표시)
    if (InPercent >= 0 && InPercent <= 100)
    {
        const int BarY    = SpinCY + SpinRadius + 58;
        const int BarH    = 6;
        const int BarW    = (std::min)(W - 120, 420);
        const int BarX    = (W - BarW) / 2;

        // 배경 트랙
        HBRUSH TrackBrush = CreateSolidBrush(RGB(45, 45, 58));
        RECT TrackRect = { BarX, BarY, BarX + BarW, BarY + BarH };
        FillRect(MemDC, &TrackRect, TrackBrush);
        DeleteObject(TrackBrush);

        // 채워진 부분
        const int FillW = BarW * InPercent / 100;
        if (FillW > 0)
        {
            HBRUSH FillBrush = CreateSolidBrush(RGB(79, 193, 233));
            RECT FillBarRect = { BarX, BarY, BarX + FillW, BarY + BarH };
            FillRect(MemDC, &FillBarRect, FillBrush);
            DeleteObject(FillBrush);
        }

        // 퍼센트 텍스트
        wchar_t PercentText[16];
        swprintf_s(PercentText, L"%d%%", InPercent);
        HFONT PercentFont = CreateFontW(
            16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        SelectObject(MemDC, PercentFont);
        SetTextColor(MemDC, RGB(79, 193, 233));
        RECT PercentRect = { 0, BarY + BarH + 4, W, BarY + BarH + 24 };
        DrawTextW(MemDC, PercentText, -1, &PercentRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(PercentFont);
    }

    BitBlt(Hdc, 0, 0, W, H, MemDC, 0, 0, SRCCOPY);

    SelectObject(MemDC, OldBitmap);
    DeleteObject(MemBitmap);
    DeleteDC(MemDC);
    ReleaseDC(RenderHWnd, Hdc);
}
