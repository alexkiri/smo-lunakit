#include "program/devgui/windows/WindowFPS.h"

#include "game/System/Application.h"

WindowFPS::WindowFPS(const char* winName, sead::Heap* heap)
    : WindowBase(winName, heap)
{
    // Setup window config
    mConfig.mTrans = ImVec2(0, 650);
    mConfig.mSize = ImVec2(mWindowWidth, 70);
    
    mConfig.mWindowFlags |= ImGuiWindowFlags_NoMove;
    mConfig.mWindowFlags |= ImGuiWindowFlags_NoNav;
    mConfig.mWindowFlags |= ImGuiWindowFlags_NoDecoration;
    mConfig.mWindowFlags |= ImGuiWindowFlags_NoInputs;
}

void WindowFPS::updateWin()
{
    WindowBase::updateWin();
}

void WindowFPS::updateWinDisplay()
{
    WindowBase::updateWinDisplay();

    float curFPS = Application::instance()->mFramework->calcFps();
    
    if (mRefreshTime == 0.0)
        mRefreshTime = ImGui::GetTime();

    while (mRefreshTime < ImGui::GetTime()) {
        mHistory[mHistoryOffset] = curFPS;
        mHistoryOffset = (mHistoryOffset + 1) % IM_ARRAYSIZE(mHistory);
        mRefreshTime += 1.0f / 10.0f;
    }

    char overlay[10];
    sprintf(overlay, "%.00f FPS", curFPS);
    ImGui::PlotLines("", mHistory, IM_ARRAYSIZE(mHistory), mHistoryOffset, overlay, 0.0f, 60.0f, ImVec2(mWindowWidth, 70));

    ImGui::End();
}

void WindowFPS::configImGuiStyle()
{
    WindowBase::configImGuiStyle();
}