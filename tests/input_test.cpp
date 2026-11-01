#include "platform.hpp"
#include <windowsx.h>
#include <iostream>
#include <vector>
#include <fstream>
static int downs=0,ups=0;
static POINT received{};
static LRESULT CALLBACK targetProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_LBUTTONDOWN){++downs;received={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};return 0;}
    if(message==WM_LBUTTONUP){++ups;return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
int runTest() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW type{};type.lpfnWndProc=targetProc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"WindowsWhip.IsolatedInputTest";type.hbrBackground=GetSysColorBrush(COLOR_WINDOW);RegisterClassW(&type);
    std::vector<RECT> monitors;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT rect,LPARAM data)->BOOL {MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);if(!(info.dwFlags&MONITORINFOF_PRIMARY)) reinterpret_cast<std::vector<RECT>*>(data)->push_back(*rect);return TRUE;},reinterpret_cast<LPARAM>(&monitors));
    if(monitors.empty()){std::cerr<<"A second monitor is required for this interactive test.\n";return 6;}
    POINT original{};GetCursorPos(&original);
    const whip::Rect desktop{GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    for(auto monitor:monitors) {
        HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_APPWINDOW,type.lpszClassName,L"Whip isolated click test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,monitor.left+60,monitor.top+60,320,220,nullptr,nullptr,type.hInstance,nullptr);
        if(!window) return 2;
        SetForegroundWindow(window);UpdateWindow(window);
        const DWORD focusDeadline=GetTickCount()+120000;MSG focusMessage{};
        while(GetForegroundWindow()!=window && static_cast<LONG>(GetTickCount()-focusDeadline)<0) {while(PeekMessageW(&focusMessage,nullptr,0,0,PM_REMOVE)){TranslateMessage(&focusMessage);DispatchMessageW(&focusMessage);}Sleep(5);}
        if(GetForegroundWindow()!=window){DestroyWindow(window);std::cerr<<"Could not focus the isolated target; no click was sent.\n";return 3;}
        downs=ups=0;
        POINT screen{110,80};ClientToScreen(window,&screen);const whip::Vec2 point{static_cast<double>(screen.x),static_cast<double>(screen.y)};
        const bool sent=whip::sendClick(point,{static_cast<double>(original.x),static_cast<double>(original.y)},desktop);
        const DWORD until=GetTickCount()+150;MSG message{};
        while(static_cast<LONG>(GetTickCount()-until)<0){while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(1);}
        POINT restored{};GetCursorPos(&restored);DestroyWindow(window);
        if(!sent || received.x!=110 || received.y!=80 || std::abs(restored.x-original.x)>1 || std::abs(restored.y-original.y)>1) {std::cerr<<"Click coordinate/restore check failed: sent="<<sent<<" down="<<downs<<" up="<<ups<<" client="<<received.x<<","<<received.y<<" restored="<<restored.x<<","<<restored.y<<" original="<<original.x<<","<<original.y<<" target="<<point.x<<","<<point.y<<" desktop="<<desktop.x<<","<<desktop.y<<","<<desktop.width<<","<<desktop.height<<"\n";return 4;}
    }
    if(downs!=static_cast<int>(monitors.size()) || ups!=downs) return 5;
    std::cout<<"Isolated input test passed on "<<monitors.size()<<" monitors; pointer restored.\n";
    return 0;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    const auto report=std::filesystem::path(whip::executablePath()).parent_path()/L"input-test.log";
    std::ofstream output(report);auto* originalOut=std::cout.rdbuf(output.rdbuf());auto* originalError=std::cerr.rdbuf(output.rdbuf());
    const int code=runTest();output<<"Exit code: "<<code<<'\n';std::cout.rdbuf(originalOut);std::cerr.rdbuf(originalError);return code;
}
