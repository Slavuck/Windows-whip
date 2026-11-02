#include "core.hpp"
#include "platform.hpp"
#include "renderer.hpp"
#include "audio.hpp"
#include "flyout.hpp"
#include "design_preview.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <wtsapi32.h>
#include <mmsystem.h>
#include <magnification.h>
#include <commctrl.h>
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include <algorithm>
#include <cmath>
#include <memory>

namespace whip {
constexpr UINT WM_WAKE=WM_APP+1,WM_EXIT_APP=WM_APP+2,WM_PAUSE_APP=WM_APP+3,WM_PANEL=WM_APP+4,WM_TRAY=WM_APP+5,WM_HOOKS=WM_APP+6;
constexpr UINT_PTR frameTimer=1,monitorTimer=2;
constexpr wchar_t appClass[]=L"WindowsWhip.Controller",panelClass[]=L"WindowsWhip.Panel",manualClass[]=L"WindowsWhip.Manual";
constexpr wchar_t trainingUrl[]=L"https://test-whip.vercel.app/";
using Microsoft::WRL::ComPtr;

class Input {
public:
    explicit Input(HWND owner):owner_(owner) {
        POINT cursor{};GetCursorPos(&cursor);position_.store(pack(cursor));
        ready_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        thread_=std::thread([this]{run();});WaitForSingleObject(ready_,3000);
    }
    ~Input() {if(id_) PostThreadMessageW(id_,WM_QUIT,0,0);if(thread_.joinable()) thread_.join();CloseHandle(ready_);}
    void active(bool active) {PostThreadMessageW(id_,WM_HOOKS,active,0);}
    Vec2 position() const {const auto value=position_.load();return {static_cast<double>(static_cast<std::int32_t>(value&0xffffffff)),static_cast<double>(static_cast<std::int32_t>(value>>32))};}
    bool buttons() const {return buttons_.load()!=0;}
    bool hooksActive() const {return active_.load();}
    struct Sample {double time;Vec2 point;bool held,button;};
    static double time() {return GetTickCount64()*0.001;}
    bool take(Sample& sample) {
        const auto read=read_.load(std::memory_order_relaxed);
        if(read==write_.load(std::memory_order_acquire)) return false;
        sample=samples_[read];read_.store((read+1)%samples_.size(),std::memory_order_release);return true;
    }
    void clear() {read_.store(write_.load(std::memory_order_acquire),std::memory_order_release);overflow_=false;}
    bool overflowed() {return overflow_.exchange(false);}
    std::atomic<bool> frames{};
private:
    static std::uint64_t pack(POINT p) {return static_cast<std::uint32_t>(p.x)|(static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.y))<<32);}
    static LRESULT CALLBACK mouseProc(int code,WPARAM message,LPARAM data) {
        if(code>=0 && current_) {
            const auto* event=reinterpret_cast<MSLLHOOKSTRUCT*>(data);
            if(!(event->flags&LLMHF_INJECTED) && event->dwExtraInfo!=inputMarker) {
                current_->position_=pack(event->pt);
                unsigned mask=0;bool down=false,up=false;
                switch(message) {
                case WM_LBUTTONDOWN:mask=1;down=true;break;case WM_LBUTTONUP:mask=1;up=true;break;
                case WM_RBUTTONDOWN:mask=2;down=true;break;case WM_RBUTTONUP:mask=2;up=true;break;
                case WM_MBUTTONDOWN:mask=4;down=true;break;case WM_MBUTTONUP:mask=4;up=true;break;
                case WM_XBUTTONDOWN:mask=HIWORD(event->mouseData)==XBUTTON1?8:16;down=true;break;case WM_XBUTTONUP:mask=HIWORD(event->mouseData)==XBUTTON1?8:16;up=true;break;
                }
                if(down) current_->buttons_.fetch_or(mask);if(up) current_->buttons_.fetch_and(~mask);
                if(message==WM_MOUSEMOVE || down || up) {
                    const auto now=GetTickCount64();
                    const double time=(static_cast<std::int64_t>(now)+static_cast<std::int32_t>(event->time-static_cast<DWORD>(now)))*0.001;
                    const auto write=current_->write_.load(std::memory_order_relaxed),next=(write+1)%current_->samples_.size();
                    if(next==current_->read_.load(std::memory_order_acquire)) current_->overflow_=true;
                    else {current_->samples_[write]={time,{static_cast<double>(event->pt.x),static_cast<double>(event->pt.y)},current_->buttons_.load()!=0,down || up};current_->write_.store(next,std::memory_order_release);}
                }
                if(!current_->frames.exchange(true)) PostMessageW(current_->owner_,WM_WAKE,0,0);
            }
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
    static LRESULT CALLBACK keyProc(int code,WPARAM message,LPARAM data) {
        if(code>=0 && current_) {
            const auto* event=reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
            if(!(event->flags&LLKHF_INJECTED)) {
                const bool down=message==WM_KEYDOWN || message==WM_SYSKEYDOWN;
                if(event->vkCode==VK_ESCAPE && down) PostMessageW(current_->owner_,WM_EXIT_APP,0,0);
                if(event->vkCode==VK_F8) {if(down && !current_->f8_) PostMessageW(current_->owner_,WM_PAUSE_APP,0,0);current_->f8_=down;}
            }
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
    void setHooks(bool enabled) {
        if(mouse_) {UnhookWindowsHookEx(mouse_);mouse_=nullptr;}if(keyboard_) {UnhookWindowsHookEx(keyboard_);keyboard_=nullptr;}
        active_=false;buttons_=0;f8_=false;
        if(enabled) {
            f8_=(GetAsyncKeyState(VK_F8)&0x8000)!=0;
            buttons_=((GetAsyncKeyState(VK_LBUTTON)&0x8000)?1u:0u)|((GetAsyncKeyState(VK_RBUTTON)&0x8000)?2u:0u)|((GetAsyncKeyState(VK_MBUTTON)&0x8000)?4u:0u)|((GetAsyncKeyState(VK_XBUTTON1)&0x8000)?8u:0u)|((GetAsyncKeyState(VK_XBUTTON2)&0x8000)?16u:0u);
            POINT cursor{};GetCursorPos(&cursor);position_=pack(cursor);
            mouse_=SetWindowsHookExW(WH_MOUSE_LL,mouseProc,GetModuleHandleW(nullptr),0);
            keyboard_=SetWindowsHookExW(WH_KEYBOARD_LL,keyProc,GetModuleHandleW(nullptr),0);
            active_=mouse_ && keyboard_;
            if(!active_) {if(mouse_) UnhookWindowsHookEx(mouse_);if(keyboard_) UnhookWindowsHookEx(keyboard_);mouse_=keyboard_=nullptr;}
        }
    }
    void run() {
        current_=this;id_=GetCurrentThreadId();MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);SetEvent(ready_);
        while(GetMessageW(&message,nullptr,0,0)>0) {if(message.message==WM_HOOKS) setHooks(message.wParam!=0);else {TranslateMessage(&message);DispatchMessageW(&message);}}
        setHooks(false);current_=nullptr;
    }
    static inline Input* current_{};
    HWND owner_{};HANDLE ready_{};DWORD id_{};HHOOK mouse_{},keyboard_{};std::thread thread_;
    std::atomic<std::uint64_t> position_{};std::atomic<unsigned> buttons_{};std::atomic<bool> active_{};bool f8_{};
    std::array<Sample,2048> samples_{};
    std::atomic<std::size_t> read_{},write_{};
    std::atomic<bool> overflow_{};
};

class Application {
public:
    Application(HINSTANCE instance,bool preview,bool smoke,bool showPanel=true,bool animationProbe=false,bool trayTest=false):instance_(instance),preview_(preview),smoke_(smoke),showPanelOnStart_(showPanel),animationProbe_(animationProbe),trayTest_(trayTest) {}
    ~Application() {
        shuttingDown_=true;input_.reset();
        restoreCursor();if(magnification_) MagUninitialize();
        if(timerResolution_) timeEndPeriod(1);
        if(main_) {WTSUnRegisterSessionNotification(main_);Shell_NotifyIconW(NIM_DELETE,&icon_);}
        if(guardStop_) {SetEvent(guardStop_);CloseHandle(guardStop_);}if(heartbeat_) CloseHandle(heartbeat_);
    }
    int run();
private:
    static LRESULT CALLBACK controllerProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK panelProc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK manualProc(HWND,UINT,WPARAM,LPARAM);
    static Application* self(HWND window) {return reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));}
    LRESULT message(UINT,WPARAM,LPARAM);
    void tick();void monitor();void reconcile();void startFrames();void stopFrames();void resetMotion();
    bool fullscreen() const;
    void restoreCursor();void updateIcon(bool add=false);void showPanel();void hidePanel();void showManual();void updateLanguage();
    void paintPanel();void perform(UiAction action);void startWatchdog();void updateTooltip();void fitManualWindow();void styleFlyout(HWND window);
    UiState uiState() const;
    const wchar_t* tr(const wchar_t* en,const wchar_t* ru) const {return settings_.language==Language::Russian?ru:en;}
    HINSTANCE instance_{};HWND main_{},panel_{},manual_{},trainingTooltip_{};
    bool preview_{},smoke_{},showPanelOnStart_{true},manualPause_{},sessionPause_{},fullscreenPause_{},uiPause_{},paused_{true},shuttingDown_{},magnification_{},cursorHidden_{},frames_{},timerResolution_{},rawF8_{};
    int focus_=0,exitCode_{};UINT taskbarCreated_{};NOTIFYICONDATAW icon_{};
    HANDLE guardStop_{},heartbeat_{};
    Settings settings_;Whip whip_;GestureDetector gesture_;Renderer renderer_;std::unique_ptr<Audio> audio_;std::unique_ptr<Input> input_;
    ContactValidator contact_;double attachment_{},lastTick_{},lastMovement_{},started_{},flashUntil_{},animationStart_{},switchFrom_{1};Vec2 lastCursor_{},flash_{};
    ComPtr<ID2D1Factory> uiFactory_;ComPtr<IDWriteFactory> writeFactory_;
    FlyoutView flyout_;ManualLayout manualLayout_;UiAction hover_=UiAction::None,manualHover_=UiAction::None;
    bool openingPanel_{},contextMenu_{},keyboardFocus_{},coilVisible_{},displayValid_{},animationProbe_{},trayTest_{};int probeStage_{},coilingFrames_{},coiledFrames_{};
    CoilAnimation coil_;CoilAnimation::Points displayPoints_{};
    double lastPauseKey_=-1;
    double recognitionTime_{};
    std::vector<double> frameCosts_,clickDelays_;std::uint64_t clicks_{},failedClicks_{};
};

LRESULT CALLBACK Application::controllerProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE) {const auto* create=reinterpret_cast<CREATESTRUCTW*>(lp);auto* app=static_cast<Application*>(create->lpCreateParams);app->main_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(auto* app=self(window)) return app->message(message,wp,lp);
    return DefWindowProcW(window,message,wp,lp);
}
void Application::restoreCursor() {if(magnification_) MagShowSystemCursor(TRUE);cursorHidden_=false;}
void Application::resetMotion() {
    POINT point{};GetCursorPos(&point);lastCursor_={static_cast<double>(point.x),static_cast<double>(point.y)};
    lastTick_=lastMovement_=clockSeconds();gesture_.reset(recognitionTime_=input_?Input::time():lastTick_,lastCursor_);if(input_) input_->clear();whip_.reset(lastCursor_);attachment_=0;
    contact_.cancel();flashUntil_=0;
}
void Application::startFrames() {
    if(paused_ && !coilVisible_) return;
    if(!frames_) {frames_=true;if(input_) input_->frames=true;SetTimer(main_,frameTimer,7,nullptr);if(!timerResolution_) {timeBeginPeriod(1);timerResolution_=true;}}
}
void Application::stopFrames() {
    KillTimer(main_,frameTimer);frames_=false;if(input_) input_->frames=false;
    if(timerResolution_) {timeEndPeriod(1);timerResolution_=false;}
}
void Application::reconcile() {
    const bool next=manualPause_ || sessionPause_ || fullscreenPause_ || uiPause_;
    const bool showCoil=next && manualPause_ && !sessionPause_ && !fullscreenPause_ && !uiPause_;
    if(next==paused_ && showCoil==coilVisible_) {updateIcon();return;}
    const auto visible=displayValid_?displayPoints_:whip_.points();
    const bool wasCoiled=coilVisible_ || coil_.coiled();
    paused_=next;coilVisible_=showCoil;
    contact_.cancel();flashUntil_=0;
    gesture_.reset(recognitionTime_=input_?Input::time():clockSeconds(),lastCursor_);if(input_) input_->clear();
    if(paused_) {
        restoreCursor();if(audio_) audio_->stopAll();if(input_) input_->active(false);
        if(showCoil) {
            Vec2 anchor=lastCursor_;
            coil_.begin(visible,anchor,true,clockSeconds());startFrames();
        } else {stopFrames();renderer_.hide();}
    } else {
        resetMotion();
        if(wasCoiled) coil_.begin(visible,lastCursor_,false,clockSeconds());else coil_={};
        if(input_) input_->active(true);
        if(magnification_ && !preview_ && !smoke_) cursorHidden_=MagShowSystemCursor(FALSE)!=FALSE;
        startFrames();
    }
    updateIcon();InvalidateRect(panel_,nullptr,FALSE);
}
bool Application::fullscreen() const {
    const HWND foreground=GetForegroundWindow();if(!foreground || foreground==panel_ || foreground==manual_ || IsIconic(foreground)) return false;
    wchar_t name[128]{};GetClassNameW(foreground,name,128);
    if(wcscmp(name,L"Progman")==0 || wcscmp(name,L"WorkerW")==0 || wcscmp(name,L"Shell_TrayWnd")==0 || wcscmp(name,L"Shell_SecondaryTrayWnd")==0) return false;
    QUERY_USER_NOTIFICATION_STATE state{};
    if(SUCCEEDED(SHQueryUserNotificationState(&state)) && state==QUNS_RUNNING_D3D_FULL_SCREEN) return true;
    RECT bounds{};if(FAILED(DwmGetWindowAttribute(foreground,DWMWA_EXTENDED_FRAME_BOUNDS,&bounds,sizeof(bounds)))) GetWindowRect(foreground,&bounds);
    MONITORINFO monitorInfo{sizeof(MONITORINFO)};if(!GetMonitorInfoW(MonitorFromWindow(foreground,MONITOR_DEFAULTTONEAREST),&monitorInfo)) return false;
    const auto& m=monitorInfo.rcMonitor;
    return bounds.left<=m.left+2 && bounds.top<=m.top+2 && bounds.right>=m.right-2 && bounds.bottom>=m.bottom-2;
}
void Application::monitor() {
    if(heartbeat_) SetEvent(heartbeat_);
    if(!preview_ && !smoke_) {fullscreenPause_=fullscreen();reconcile();}
    const double elapsed=clockSeconds()-started_;
    if(trayTest_) {
        if(probeStage_==0 && elapsed>0.25) {
            SendMessageW(main_,WM_TRAY,0,MAKELPARAM(WM_LBUTTONUP,1));SendMessageW(main_,WM_TRAY,0,MAKELPARAM(NIN_SELECT,1));probeStage_=1;
        } else if(probeStage_==1 && elapsed>0.70) {
            if(!IsWindowVisible(panel_) || !uiPause_ || !paused_) exitCode_=5;
            SendMessageW(main_,WM_TRAY,0,MAKELPARAM(NIN_SELECT,1));SendMessageW(main_,WM_TRAY,0,MAKELPARAM(WM_LBUTTONUP,1));
            if(!IsWindowVisible(panel_)) exitCode_=5;probeStage_=2;
        } else if(probeStage_==2 && elapsed>1.0) {showManual();probeStage_=3;}
        else if(probeStage_==3 && elapsed>1.35) {
            if(!IsWindowVisible(manual_) || !uiPause_ || !paused_) exitCode_=6;
            perform(UiAction::Close);showPanel();if(!IsWindowVisible(panel_) || !paused_) exitCode_=7;probeStage_=4;
        }
    }
    if(animationProbe_ && probeStage_==0 && elapsed>0.30) {manualPause_=true;reconcile();probeStage_=1;}
    if(animationProbe_ && probeStage_==1 && elapsed>1.65) {manualPause_=false;reconcile();probeStage_=2;}
    if(smoke_ && elapsed>(animationProbe_?3.2:2.0)) {
        if(animationProbe_ && (coilingFrames_==0 || coiledFrames_==0)) exitCode_=4;
        PostMessageW(main_,WM_EXIT_APP,0,0);
    }
}
void Application::tick() {
    const double begin=clockSeconds(),dt=std::clamp(begin-lastTick_,0.0,0.035);lastTick_=begin;
    if(paused_) {
        if(coilVisible_) {
            POINT point{};GetCursorPos(&point);Vec2 cursor{static_cast<double>(point.x),static_cast<double>(point.y)};
            if(smoke_) {const auto area=renderer_.secondaryMonitor();cursor={area.x+area.width*0.48+std::sin((begin-started_)*3)*100,area.y+area.height*0.30};}
            const bool moved=length(cursor-lastCursor_)>0.1;if(moved) lastMovement_=begin;lastCursor_=cursor;
            coil_.follow(cursor,dt);displayPoints_=coil_.sample(whip_.points(),begin);displayValid_=true;
            if(!renderer_.drawPoints(displayPoints_)) {renderer_.hide();stopFrames();return;}
            if(coil_.active(begin)) ++coilingFrames_;else {++coiledFrames_;if(coil_.motion()<1 && begin-lastMovement_>0.3) stopFrames();}
        } else stopFrames();
        return;
    }
    Vec2 cursor=lastCursor_;bool held=false;
    if(smoke_ || preview_) {const auto area=renderer_.secondaryMonitor();cursor=lastCursor_=Vec2{area.x+area.width*0.48+std::sin((begin-started_)*4.5)*160,area.y+area.height*0.12+std::sin((begin-started_)*5.0)*70};}
    else if(input_) {cursor=input_->position();held=input_->buttons();}
    if(length(cursor-lastCursor_)>0.2) lastMovement_=begin;
    lastCursor_=cursor;
    const auto consume=[&](double time,Vec2 point,bool buttons,bool buttonEvent=false) {
        if(buttonEvent) {gesture_.reset(recognitionTime_=std::max(recognitionTime_,time),point);contact_.cancel();return;}
        if(time<=recognitionTime_) return;
        recognitionTime_=time;auto event=gesture_.sample(time,point,buttons);
        if(event && !smoke_ && !preview_) {
            event->time=begin; // Contact deadlines use the confirmation clock.
            if(event->kind==GestureEvent::Kind::Swing) {contact_.cancel();audio_->play(false,event->id,event->speed);}
            else contact_.arm(*event,settings_.mode);
        }
    };
    if(coil_.active(begin)) {gesture_.reset(recognitionTime_=input_?Input::time():begin,cursor);if(input_) input_->clear();}
    else if(input_) {
        const double now=Input::time();Input::Sample sample{};bool stale=input_->overflowed();
        while(!stale && input_->take(sample)) {
            if(now-sample.time>0.050) stale=true;
            else consume(sample.time,sample.point,sample.held,sample.button);
        }
        if(stale) {input_->clear();gesture_.reset(recognitionTime_=now,cursor);contact_.cancel();}
        else if(now-recognitionTime_>=0.018) consume(now,input_->position(),input_->buttons());
    } else consume(begin,cursor,held);
    const double target=settings_.mode==Mode::Exact?(contact_.pending()?1.0:std::clamp(gesture_.speed()/GestureDetector::peakSpeed,0.0,1.0)):0.0;
    attachment_+=(target-attachment_)*(1-std::exp(-dt*(contact_.pending()?90:14)));
    if(settings_.mode==Mode::Exact && contact_.pending()) attachment_=1;
    const Vec2 control=settings_.mode==Mode::Exact && contact_.pending()?contact_.target():cursor;
    whip_.advance(dt,control,attachment_);
    if(input_) {cursor=input_->position();held=input_->buttons();}
    if(auto impact=contact_.update(begin,whip_,cursor,held)) {
        if(renderer_.contains(impact->point) && !smoke_ && !preview_ && !paused_) {
            flash_=impact->point;flashUntil_=begin+0.12;
            if(sendClick(impact->point,cursor,renderer_.desktop())) {++clicks_;audio_->play(true,impact->gestureId,impact->speed);}else ++failedClicks_;
            clickDelays_.push_back((clockSeconds()-impact->stopTime)*1000);
        }
    }
    displayPoints_=coil_.sample(whip_.points(),begin);displayValid_=true;
    if(!renderer_.drawPoints(displayPoints_,flash_,std::clamp((flashUntil_-begin)/0.12,0.0,1.0))) {
        if(smoke_) {exitCode_=2;PostMessageW(main_,WM_EXIT_APP,0,0);}
        else {manualPause_=true;reconcile();renderer_.rebuild();}
    }
    frameCosts_.push_back((clockSeconds()-begin)*1000);if(frameCosts_.size()>20000) frameCosts_.erase(frameCosts_.begin(),frameCosts_.begin()+10000);
    if(!preview_ && !smoke_ && !contact_.pending() && begin-lastMovement_>3 && (whip_.motion()<8 || begin-lastMovement_>8)) stopFrames();
}
void Application::updateIcon(bool add) {
    if(smoke_) return;
    icon_.cbSize=sizeof(icon_);icon_.hWnd=main_;icon_.uID=1;icon_.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;icon_.uCallbackMessage=WM_TRAY;
    icon_.hIcon=LoadIconW(instance_,MAKEINTRESOURCEW(1));
    std::wstring tip=L"Windows Whip — ";tip+=paused_?tr(L"Paused",L"Пауза"):(settings_.mode==Mode::Exact?tr(L"Exact",L"Точный"):tr(L"Sadomaso",L"Садомазо"));
    if(settings_.writeFailed) tip+=tr(L" (settings not saved)",L" (настройки не сохранены)");
    wcsncpy_s(icon_.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(add?NIM_ADD:NIM_MODIFY,&icon_);
    if(add) {icon_.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&icon_);}
}
void Application::styleFlyout(HWND window) {
    const BOOL dark=TRUE;const DWORD corner=2,backdrop=3;const COLORREF border=RGB(68,91,104);
    DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corner,sizeof(corner));
    DwmSetWindowAttribute(window,DWMWA_SYSTEMBACKDROP_TYPE,&backdrop,sizeof(backdrop));
    DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));
    SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED);
}
void Application::showPanel() {
    openingPanel_=true;uiPause_=true;reconcile();
    POINT point{};GetCursorPos(&point);NOTIFYICONIDENTIFIER identifier{sizeof(identifier),main_,1,{}};RECT tray{};
    if(preview_ || smoke_) {const auto area=renderer_.secondaryMonitor();point={area.x+area.width-215,area.y+area.height-24};}
    else if(SUCCEEDED(Shell_NotifyIconGetRect(&identifier,&tray))) point={tray.left,tray.top};
    const UINT dpi=GetDpiForWindow(panel_);
    const int width=MulDiv(static_cast<int>(FlyoutView::panelWidth),dpi,96),height=MulDiv(static_cast<int>(FlyoutView::panelHeight),dpi,96);
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&info);
    const int x=std::clamp(point.x-width/2,info.rcWork.left+8,info.rcWork.right-width-8),y=std::clamp(point.y-height-12,info.rcWork.top+8,info.rcWork.bottom-height-8);
    SetWindowPos(panel_,HWND_TOPMOST,x,y,width,height,SWP_SHOWWINDOW);SetForegroundWindow(panel_);SetFocus(panel_);openingPanel_=false;keyboardFocus_=false;updateTooltip();InvalidateRect(panel_,nullptr,FALSE);
}
void Application::hidePanel() {
    if(trainingTooltip_) SendMessageW(trainingTooltip_,TTM_POP,0,0);
    KillTimer(panel_,902);ShowWindow(panel_,SW_HIDE);uiPause_=contextMenu_ || (manual_ && IsWindowVisible(manual_));hover_=UiAction::None;reconcile();
}
void Application::updateTooltip() {
    if(!trainingTooltip_) return;
    const double scale=GetDpiForWindow(panel_)/96.0;
    TOOLINFOW tool{sizeof(tool)};tool.hwnd=panel_;tool.uId=1;
    tool.rect={static_cast<LONG>(244*scale),static_cast<LONG>(12*scale),static_cast<LONG>(308*scale),static_cast<LONG>(76*scale)};
    tool.lpszText=const_cast<wchar_t*>(tr(L"Training",L"Тренировка"));
    SendMessageW(trainingTooltip_,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&tool));
    SendMessageW(trainingTooltip_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));
}
void Application::fitManualWindow() {
    const HWND relative=IsWindowVisible(panel_)?panel_:manual_;
    MONITORINFO info{sizeof(info)};
    HMONITOR monitor=MonitorFromWindow(relative?relative:main_,MONITOR_DEFAULTTONEAREST);
    if(preview_ || smoke_) {const auto area=renderer_.secondaryMonitor();monitor=MonitorFromPoint({area.x+20,area.y+20},MONITOR_DEFAULTTONEAREST);}
    GetMonitorInfoW(monitor,&info);
    const float scale=GetDpiForWindow(relative?relative:panel_)/96.0f;
    manualLayout_=flyout_.fitManual(resourceText(settings_.language==Language::English?201:202),(info.rcWork.right-info.rcWork.left)/scale,(info.rcWork.bottom-info.rcWork.top)/scale);
    const int width=static_cast<int>(std::ceil(manualLayout_.width*scale)),height=static_cast<int>(std::ceil(manualLayout_.height*scale));
    RECT old{};GetWindowRect(panel_,&old);
    const int x=std::clamp(old.right-width,info.rcWork.left+8,info.rcWork.right-width-8);
    const int y=std::clamp(info.rcWork.bottom-height-16,info.rcWork.top+8,info.rcWork.bottom-height-8);
    SetWindowPos(manual_,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE);
}
void Application::updateLanguage() {
    settings_.save();updateIcon();updateTooltip();
    if(manual_) {SetWindowTextW(manual_,tr(L"Windows Whip — Manual",L"Windows Whip — Мануал"));fitManualWindow();InvalidateRect(manual_,nullptr,FALSE);}
    InvalidateRect(panel_,nullptr,FALSE);
}
void Application::showManual() {
    uiPause_=true;reconcile();
    if(!manual_) {
        manual_=CreateWindowExW((preview_?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_TOPMOST,manualClass,tr(L"Windows Whip — Manual",L"Windows Whip — Мануал"),WS_POPUP|WS_CAPTION|WS_THICKFRAME|WS_SYSMENU,0,0,460,800,preview_?nullptr:main_,nullptr,instance_,this);
        styleFlyout(manual_);
    }
    fitManualWindow();ShowWindow(manual_,SW_SHOWNORMAL);ShowWindow(panel_,SW_HIDE);uiPause_=true;reconcile();SetForegroundWindow(manual_);InvalidateRect(manual_,nullptr,FALSE);
}
UiState Application::uiState() const {
    const double t=std::clamp((clockSeconds()-animationStart_)/0.16,0.0,1.0),eased=1-std::pow(1-t,3);
    const double position=switchFrom_+((settings_.mode==Mode::Sadomaso?1.0:0.0)-switchFrom_)*eased;
    const UiAction order[]{UiAction::ExactMode,UiAction::SadomasoMode,UiAction::English,UiAction::Russian,UiAction::Manual,UiAction::Training,UiAction::Pause};
    return {settings_.mode,settings_.language,manualPause_,hover_,keyboardFocus_?order[focus_]:UiAction::None,position};
}
void Application::perform(UiAction action) {
    switch(action) {
    case UiAction::Mode:case UiAction::ExactMode:case UiAction::SadomasoMode:switchFrom_=uiState().modePosition;settings_.mode=action==UiAction::ExactMode?Mode::Exact:action==UiAction::SadomasoMode?Mode::Sadomaso:(settings_.mode==Mode::Sadomaso?Mode::Exact:Mode::Sadomaso);animationStart_=clockSeconds();SetTimer(panel_,901,16,nullptr);settings_.save();resetMotion();break;
    case UiAction::English:settings_.language=Language::English;updateLanguage();break;
    case UiAction::Russian:settings_.language=Language::Russian;updateLanguage();break;
    case UiAction::Manual:showManual();break;
    case UiAction::Training:hidePanel();ShellExecuteW(nullptr,L"open",trainingUrl,nullptr,nullptr,SW_SHOWNORMAL);break;
    case UiAction::Pause:manualPause_=!manualPause_;ShowWindow(panel_,SW_HIDE);if(manual_) ShowWindow(manual_,SW_HIDE);uiPause_=false;reconcile();break;
    case UiAction::Close:ShowWindow(manual_,SW_HIDE);manualHover_=UiAction::None;uiPause_=IsWindowVisible(panel_)!=FALSE;reconcile();break;
    default:break;
    }
    updateIcon();InvalidateRect(panel_,nullptr,FALSE);
}
void Application::paintPanel() {flyout_.paintPanel(panel_,uiState());}
LRESULT CALLBACK Application::panelProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* app=self(window);if(!app) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_NCCALCSIZE:if(wp) return 0;break;
    case WM_NCHITTEST:return HTCLIENT;
    case WM_PAINT:app->paintPanel();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE && !app->openingPanel_ && !app->preview_) SetTimer(window,902,60,nullptr);else KillTimer(window,902);return 0;
    case WM_SIZE:app->flyout_.resized(window);return 0;
    case WM_DPICHANGED:{const auto* rect=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);app->updateTooltip();return 0;}
    case WM_TIMER:
        if(wp==902) {KillTimer(window,902);const auto foreground=GetForegroundWindow();if(!app->openingPanel_ && foreground!=window && foreground!=app->manual_) app->hidePanel();}
        else {InvalidateRect(window,nullptr,FALSE);if(clockSeconds()-app->animationStart_>0.18) KillTimer(window,901);}return 0;
    case WM_MOUSEMOVE:{const float scale=GetDpiForWindow(window)/96.0f;const auto hit=FlyoutView::panelHit(GET_X_LPARAM(lp)/scale,GET_Y_LPARAM(lp)/scale);if(hit!=app->hover_ || app->keyboardFocus_) {app->hover_=hit;app->keyboardFocus_=false;InvalidateRect(window,nullptr,FALSE);}TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);SetCursor(LoadCursorW(nullptr,hit==UiAction::None?IDC_ARROW:IDC_HAND));return 0;}
    case WM_MOUSELEAVE:app->hover_=UiAction::None;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_LBUTTONUP:{const float scale=GetDpiForWindow(window)/96.0f;app->perform(FlyoutView::panelHit(GET_X_LPARAM(lp)/scale,GET_Y_LPARAM(lp)/scale));return 0;}
    case WM_KEYDOWN:{app->keyboardFocus_=true;const UiAction order[]{UiAction::ExactMode,UiAction::SadomasoMode,UiAction::English,UiAction::Russian,UiAction::Manual,UiAction::Training,UiAction::Pause};if(wp==VK_ESCAPE) PostMessageW(app->main_,WM_EXIT_APP,0,0);else if(wp==VK_F8 && app->preview_ && !(lp&(1LL<<30))) PostMessageW(app->main_,WM_PAUSE_APP,0,0);else if(wp==VK_TAB) app->focus_=(app->focus_+((GetKeyState(VK_SHIFT)&0x8000)?6:1))%7;else if(wp==VK_SPACE || wp==VK_RETURN) app->perform(order[app->focus_]);else if((wp==VK_LEFT || wp==VK_RIGHT) && (app->focus_==2 || app->focus_==3)) app->perform(wp==VK_LEFT?UiAction::English:UiAction::Russian);else if((wp==VK_LEFT || wp==VK_RIGHT) && (app->focus_==0 || app->focus_==1)) app->perform(wp==VK_LEFT?UiAction::ExactMode:UiAction::SadomasoMode);InvalidateRect(window,nullptr,FALSE);return 0;}
    case WM_CLOSE:if(app->preview_) PostMessageW(app->main_,WM_EXIT_APP,0,0);else app->hidePanel();return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK Application::manualProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* app=self(window);if(!app) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_NCCALCSIZE:if(wp) return 0;break;
    case WM_NCHITTEST:{POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&point);const float scale=GetDpiForWindow(window)/96.0f;if(FlyoutView::manualHit(point.x/scale,point.y/scale,app->manualLayout_.width)==UiAction::Close) return HTCLIENT;return point.y<72*scale?HTCAPTION:HTCLIENT;}
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{auto state=app->uiState();state.hover=app->manualHover_;state.focus=UiAction::None;app->flyout_.paintManual(window,state,app->manualLayout_);return 0;}
    case WM_SIZE:app->flyout_.resized(window);return 0;
    case WM_DPICHANGED:app->fitManualWindow();return 0;
    case WM_MOUSEMOVE:{const float scale=GetDpiForWindow(window)/96.0f;const auto hit=FlyoutView::manualHit(GET_X_LPARAM(lp)/scale,GET_Y_LPARAM(lp)/scale,app->manualLayout_.width);if(hit!=app->manualHover_) {app->manualHover_=hit;InvalidateRect(window,nullptr,FALSE);}TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);SetCursor(LoadCursorW(nullptr,hit==UiAction::Close?IDC_HAND:IDC_ARROW));return 0;}
    case WM_MOUSELEAVE:app->manualHover_=UiAction::None;InvalidateRect(window,nullptr,FALSE);return 0;
    case WM_LBUTTONUP:{const float scale=GetDpiForWindow(window)/96.0f;if(FlyoutView::manualHit(GET_X_LPARAM(lp)/scale,GET_Y_LPARAM(lp)/scale,app->manualLayout_.width)==UiAction::Close) app->perform(UiAction::Close);return 0;}
    case WM_KEYDOWN:if(wp==VK_ESCAPE) PostMessageW(app->main_,WM_EXIT_APP,0,0);else if(wp==VK_F8 && app->preview_ && !(lp&(1LL<<30))) PostMessageW(app->main_,WM_PAUSE_APP,0,0);return 0;
    case WM_CLOSE:app->perform(UiAction::Close);return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
void Application::startWatchdog() {
    const auto suffix=std::to_wstring(GetCurrentProcessId());
    guardStop_=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\WindowsWhip.Stop."+suffix).c_str());heartbeat_=CreateEventW(nullptr,FALSE,FALSE,(L"Local\\WindowsWhip.Heartbeat."+suffix).c_str());
    std::wstring command=L"\""+executablePath()+L"\" --watchdog "+suffix;
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) {CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    else {CloseHandle(guardStop_);CloseHandle(heartbeat_);guardStop_=heartbeat_=nullptr;}
}
LRESULT Application::message(UINT message,WPARAM wp,LPARAM lp) {
    if(message==taskbarCreated_ && taskbarCreated_) {updateIcon(true);return 0;}
    switch(message) {
    case WM_TIMER:if(wp==frameTimer) tick();else if(wp==monitorTimer) monitor();return 0;
    case WM_WAKE:if(!paused_ || coilVisible_) startFrames();return 0;
    case WM_EXIT_APP:DestroyWindow(main_);return 0;
    case WM_DESTROY:shuttingDown_=true;restoreCursor();renderer_.hide();if(audio_) audio_->stopAll();PostQuitMessage(exitCode_);return 0;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(wp) {restoreCursor();PostMessageW(main_,WM_EXIT_APP,0,0);}return 0;
    case WM_PAUSE_APP:{const double now=clockSeconds();if(now-lastPauseKey_>0.18) {lastPauseKey_=now;perform(UiAction::Pause);}return 0;}
    case WM_PANEL:showPanel();return 0;
    case WM_DISPLAYCHANGE:restoreCursor();renderer_.rebuild();resetMotion();if(!paused_) {if(magnification_ && !preview_ && !smoke_) cursorHidden_=MagShowSystemCursor(FALSE)!=FALSE;startFrames();}return 0;
    case WM_DEVICECHANGE:if(audio_) audio_->recover();return 0;
    case WM_WTSSESSION_CHANGE:if(wp==WTS_SESSION_LOCK || wp==WTS_SESSION_LOGOFF) sessionPause_=true;else if(wp==WTS_SESSION_UNLOCK || wp==WTS_SESSION_LOGON) {sessionPause_=false;resetMotion();}reconcile();return 0;
    case WM_POWERBROADCAST:if(wp==PBT_APMSUSPEND) sessionPause_=true;else if(wp==PBT_APMRESUMEAUTOMATIC || wp==PBT_APMRESUMESUSPEND) {restoreCursor();sessionPause_=false;resetMotion();if(audio_) audio_->recover();}reconcile();return TRUE;
    case WM_INPUT:{
        UINT size=sizeof(RAWINPUT);RAWINPUT raw{};
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))!=static_cast<UINT>(-1)) {
            if(raw.header.dwType==RIM_TYPEMOUSE && coilVisible_) startFrames();
            if(raw.header.dwType==RIM_TYPEKEYBOARD && (input_ || preview_)) {
                const auto& key=raw.data.keyboard;const bool down=!(key.Flags&RI_KEY_BREAK);
                if(down && key.VKey==VK_ESCAPE && (!input_ || !input_->hooksActive())) PostMessageW(main_,WM_EXIT_APP,0,0);
                if(key.VKey==VK_F8) {if(down && !rawF8_ && (!input_ || !input_->hooksActive())) PostMessageW(main_,WM_PAUSE_APP,0,0);rawF8_=down;}
            }
        }
        return DefWindowProcW(main_,message,wp,lp);
    }
    case WM_TRAY:{const UINT event=LOWORD(lp);if(event==NIN_SELECT || event==NIN_KEYSELECT) showPanel();else if(event==WM_CONTEXTMENU) {contextMenu_=true;uiPause_=true;reconcile();POINT point{};GetCursorPos(&point);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,manualPause_?tr(L"Resume",L"Продолжить"):tr(L"Pause",L"Пауза"));AppendMenuW(menu,MF_STRING,2,tr(L"Exit",L"Выход"));SetForegroundWindow(main_);const int selection=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,main_,nullptr);DestroyMenu(menu);contextMenu_=false;uiPause_=(manual_ && IsWindowVisible(manual_)) || IsWindowVisible(panel_);if(selection==1) perform(UiAction::Pause);if(selection==2) PostMessageW(main_,WM_EXIT_APP,0,0);reconcile();}return 0;}
    }
    return DefWindowProcW(main_,message,wp,lp);
}
int Application::run() {
    settings_.load(executablePath(),localDataPath());switchFrom_=settings_.mode==Mode::Sadomaso?1:0;
    WNDCLASSW controller{};controller.lpfnWndProc=controllerProc;controller.hInstance=instance_;controller.lpszClassName=appClass;RegisterClassW(&controller);
    main_=CreateWindowExW(WS_EX_TOOLWINDOW,appClass,L"Windows Whip",WS_POPUP,0,0,1,1,nullptr,nullptr,instance_,this);if(!main_) return 1;
    if(!renderer_.initialize(instance_,preview_ || smoke_)) {MessageBoxW(nullptr,L"Windows Whip could not start its graphics device.",L"Windows Whip",MB_ICONERROR);return 2;}
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,uiFactory_.GetAddressOf());DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
    if(!uiFactory_ || !writeFactory_) return 3;
    WNDCLASSW panel{};panel.lpfnWndProc=panelProc;panel.hInstance=instance_;panel.lpszClassName=panelClass;panel.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&panel);
    panel_=CreateWindowExW((preview_?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_TOPMOST,panelClass,preview_?L"Windows Whip Preview":L"Windows Whip",WS_POPUP|WS_CAPTION|WS_THICKFRAME|WS_SYSMENU,0,0,360,444,preview_?nullptr:main_,nullptr,instance_,this);
    styleFlyout(panel_);
    if(!flyout_.initialize(uiFactory_.Get(),writeFactory_.Get())) return 3;
    WNDCLASSW manual=panel;manual.lpfnWndProc=manualProc;manual.lpszClassName=manualClass;RegisterClassW(&manual);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    trainingTooltip_=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,panel_,nullptr,instance_,nullptr);
    TOOLINFOW tool{sizeof(tool)};tool.uFlags=TTF_SUBCLASS;tool.hwnd=panel_;tool.uId=1;tool.lpszText=const_cast<wchar_t*>(tr(L"Training",L"Тренировка"));SendMessageW(trainingTooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
    SendMessageW(trainingTooltip_,TTM_SETDELAYTIME,TTDT_INITIAL,450);updateTooltip();
    audio_=std::make_unique<Audio>();
    if(!smoke_ && !preview_) {
        input_=std::make_unique<Input>(main_);
        RAWINPUTDEVICE keyboard{0x01,0x06,RIDEV_INPUTSINK,main_};RegisterRawInputDevices(&keyboard,1,sizeof(keyboard));
        startWatchdog();magnification_=guardStop_ && MagInitialize();
        WTSRegisterSessionNotification(main_,NOTIFY_FOR_THIS_SESSION);
    }
    if(!smoke_) {RAWINPUTDEVICE mouse{0x01,0x02,RIDEV_INPUTSINK,main_};RegisterRawInputDevices(&mouse,1,sizeof(mouse));if(preview_) {RAWINPUTDEVICE keyboard{0x01,0x06,RIDEV_INPUTSINK,main_};RegisterRawInputDevices(&keyboard,1,sizeof(keyboard));}}
    taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");updateIcon(true);
    started_=clockSeconds();resetMotion();SetTimer(main_,monitorTimer,250,nullptr);
    if(smoke_ || preview_) {const auto area=renderer_.secondaryMonitor();lastCursor_={area.x+area.width*0.48,area.y+area.height*0.12};whip_.reset(lastCursor_);gesture_.reset(started_,lastCursor_);}
    paused_=true;fullscreenPause_=!smoke_ && !preview_ && fullscreen();reconcile();
    if(smoke_ || preview_) {paused_=false;startFrames();if(preview_ && showPanelOnStart_) showPanel();}
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0) {TranslateMessage(&message);DispatchMessageW(&message);}
    if(smoke_) {
        std::sort(frameCosts_.begin(),frameCosts_.end());const double p95=frameCosts_.empty()?0:frameCosts_[static_cast<std::size_t>((frameCosts_.size()-1)*0.95)];
        const auto report=std::filesystem::path(executablePath()).parent_path()/(trayTest_?L"tray-report.json":(animationProbe_?L"animation-report.json":L"smoke-report.json"));
        std::ofstream output(report);output<<"{\"frames\":"<<frameCosts_.size()<<",\"frame_p95_ms\":"<<p95<<",\"monitors\":"<<renderer_.monitorCount()<<",\"audio_available\":"<<(audio_->available()?"true":"false")<<",\"coiling_frames\":"<<coilingFrames_<<",\"coiled_frames\":"<<coiledFrames_<<",\"injected_clicks\":"<<clicks_<<",\"exit_code\":"<<exitCode_<<"}\n";
    }
    return static_cast<int>(message.wParam);
}
int watchdog(DWORD parent) {
    const auto suffix=std::to_wstring(parent);
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,parent);
    HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,(L"Local\\WindowsWhip.Stop."+suffix).c_str());
    HANDLE beat=OpenEventW(SYNCHRONIZE,FALSE,(L"Local\\WindowsWhip.Heartbeat."+suffix).c_str());
    if(!process || !stop || !beat) {if(process) CloseHandle(process);if(stop) CloseHandle(stop);if(beat) CloseHandle(beat);return 1;}
    HANDLE handles[]{stop,process,beat};
    for(;;) {const DWORD result=WaitForMultipleObjects(3,handles,FALSE,4000);if(result==WAIT_OBJECT_0) break;if(result==WAIT_OBJECT_0+2) continue;if(MagInitialize()) {MagShowSystemCursor(TRUE);MagUninitialize();}if(result==WAIT_TIMEOUT) TerminateProcess(process,75);break;}
    CloseHandle(process);CloseHandle(stop);CloseHandle(beat);return 0;
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR commandLine,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring arguments=commandLine;
    if(arguments.find(L"--design-preview")!=std::wstring::npos || arguments.find(L"--design-snapshot")!=std::wstring::npos) return whip::runDesignPreview(instance,arguments.find(L"--design-snapshot")!=std::wstring::npos);
    if(arguments.find(L"--audio-test")!=std::wstring::npos) return whip::runAudioTest();
    if(arguments.starts_with(L"--watchdog ")) return whip::watchdog(static_cast<DWORD>(wcstoul(arguments.c_str()+11,nullptr,10)));
    const bool trayTest=arguments.find(L"--tray-test")!=std::wstring::npos;
    const bool animationProbe=arguments.find(L"--animation-test")!=std::wstring::npos;
    const bool smoke=trayTest || animationProbe || arguments.find(L"--smoke-test")!=std::wstring::npos,preview=arguments.find(L"--preview")!=std::wstring::npos;
    HANDLE mutex=nullptr;
    if(!smoke && !preview) {mutex=CreateMutexW(nullptr,FALSE,L"Local\\WindowsWhip.SingleInstance");if(GetLastError()==ERROR_ALREADY_EXISTS) {if(auto window=FindWindowW(whip::appClass,L"Windows Whip")) PostMessageW(window,whip::WM_PANEL,0,0);CloseHandle(mutex);return 0;}}
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;{whip::Application app(instance,preview,smoke,arguments.find(L"--preview-lash")==std::wstring::npos,animationProbe,trayTest);result=app.run();}
    if(SUCCEEDED(com)) CoUninitialize();if(mutex) CloseHandle(mutex);return result;
}
