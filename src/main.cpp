#include "core.hpp"
#include "platform.hpp"
#include "renderer.hpp"
#include "audio.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <wtsapi32.h>
#include <mmsystem.h>
#include <magnification.h>
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
                case WM_XBUTTONDOWN:mask=8;down=true;break;case WM_XBUTTONUP:mask=8;up=true;break;
                }
                if(down) current_->buttons_.fetch_or(mask);if(up) current_->buttons_.fetch_and(~mask);
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
};

class Application {
public:
    Application(HINSTANCE instance,bool preview,bool smoke,bool showPanel=true):instance_(instance),preview_(preview),smoke_(smoke),showPanelOnStart_(showPanel) {}
    ~Application() {
        shuttingDown_=true;input_.reset();
        restoreCursor();if(magnification_) MagUninitialize();
        if(timerResolution_) timeEndPeriod(1);
        if(main_) {WTSUnRegisterSessionNotification(main_);Shell_NotifyIconW(NIM_DELETE,&icon_);}
        if(guardStop_) {SetEvent(guardStop_);CloseHandle(guardStop_);}if(heartbeat_) CloseHandle(heartbeat_);
        if(manualFont_) DeleteObject(manualFont_);
        if(manualBackground_) DeleteObject(manualBackground_);
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
    void restoreCursor();void updateIcon(bool add=false);void togglePanel();void hidePanel();void showManual();void updateLanguage();
    void paintPanel();void panelAction(int action);void startWatchdog();
    const wchar_t* tr(const wchar_t* en,const wchar_t* ru) const {return settings_.language==Language::Russian?ru:en;}
    HINSTANCE instance_{};HWND main_{},panel_{},manual_{},manualText_{};HFONT manualFont_{};HBRUSH manualBackground_{};
    bool preview_{},smoke_{},showPanelOnStart_{true},manualPause_{},sessionPause_{},fullscreenPause_{},uiPause_{},paused_{true},shuttingDown_{},magnification_{},cursorHidden_{},frames_{},timerResolution_{},rawF8_{};
    int focus_=0,exitCode_{};UINT taskbarCreated_{};NOTIFYICONDATAW icon_{};
    HANDLE guardStop_{},heartbeat_{};
    Settings settings_;Whip whip_;GestureDetector gesture_;Renderer renderer_;std::unique_ptr<Audio> audio_;std::unique_ptr<Input> input_;
    std::optional<ImpactEvent> pending_;bool pendingAudio_{};double attachment_{},lastTick_{},lastMovement_{},started_{},flashUntil_{},animationStart_{},switchFrom_{1};Vec2 lastCursor_{},flash_{};
    ComPtr<ID2D1Factory> uiFactory_;ComPtr<IDWriteFactory> writeFactory_;ComPtr<ID2D1HwndRenderTarget> uiTarget_;
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
    lastTick_=lastMovement_=clockSeconds();gesture_.reset(lastTick_,lastCursor_);whip_.reset(lastCursor_);attachment_=0;
    if(pending_ && audio_) audio_->cancel(pending_->gestureId);pending_.reset();pendingAudio_=false;flashUntil_=0;
}
void Application::startFrames() {
    if(paused_ && !smoke_ && !preview_) return;
    if(!frames_) {frames_=true;if(input_) input_->frames=true;SetTimer(main_,frameTimer,7,nullptr);if(!timerResolution_) {timeBeginPeriod(1);timerResolution_=true;}}
}
void Application::stopFrames() {
    KillTimer(main_,frameTimer);frames_=false;if(input_) input_->frames=false;
    if(timerResolution_) {timeEndPeriod(1);timerResolution_=false;}
}
void Application::reconcile() {
    const bool next=manualPause_ || sessionPause_ || fullscreenPause_ || uiPause_;
    if(next==paused_) {updateIcon();return;}
    paused_=next;resetMotion();
    if(paused_) {stopFrames();renderer_.hide();restoreCursor();if(audio_) audio_->stopAll();if(input_) input_->active(false);}
    else {if(input_) input_->active(true);if(magnification_ && !preview_ && !smoke_) cursorHidden_=MagShowSystemCursor(FALSE)!=FALSE;startFrames();}
    updateIcon();
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
    if(smoke_ && clockSeconds()-started_>2) PostMessageW(main_,WM_EXIT_APP,0,0);
}
void Application::tick() {
    const double begin=clockSeconds(),dt=std::clamp(begin-lastTick_,0.0,0.035);lastTick_=begin;
    Vec2 cursor=lastCursor_;bool held=false;
    if(smoke_ || preview_) {const auto area=renderer_.secondaryMonitor();cursor=lastCursor_=Vec2{area.x+area.width*0.48+std::sin((begin-started_)*4.5)*160,area.y+area.height*0.12+std::sin((begin-started_)*5.0)*70};}
    else if(input_) {cursor=input_->position();held=input_->buttons();}
    if(length(cursor-lastCursor_)>0.2) lastMovement_=begin;
    lastCursor_=cursor;
    const auto event=gesture_.sample(begin,cursor,held);
    const double target=settings_.mode==Mode::Exact?(pending_?1.0:std::clamp(gesture_.speed()/1500.0,0.0,1.0)):0.0;
    attachment_+=(target-attachment_)*(1-std::exp(-dt*(pending_?90:14)));
    if(settings_.mode==Mode::Exact && pending_) attachment_=1;
    whip_.advance(dt,cursor,attachment_);
    if(event && !smoke_ && !preview_) {
        if(event->kind==GestureEvent::Kind::Swing) {if(pending_) audio_->cancel(pending_->gestureId);pending_.reset();audio_->play(false,event->id,event->speed);}
        else {pending_=ImpactEvent{event->id,begin+whip_.forecastPeak(cursor,settings_.mode==Mode::Exact?1:0),begin,event->speed,cursor};pendingAudio_=false;}
    }
    if(pending_ && (held || length(cursor-pending_->point)>8)) {audio_->cancel(pending_->gestureId);pending_.reset();pendingAudio_=false;}
    if(pending_ && !pendingAudio_ && begin>=pending_->time-audio_->latency()) {audio_->play(true,pending_->gestureId,std::max(pending_->speed,whip_.tipSpeed()));pendingAudio_=true;}
    if(pending_ && begin>=pending_->time) {
        const auto point=settings_.mode==Mode::Exact?pending_->point:whip_.tip();
        if(renderer_.contains(point)) {
            flash_=point;flashUntil_=begin+0.12;
            if(!smoke_ && !preview_ && !paused_) {if(sendClick(point,cursor,renderer_.desktop())) ++clicks_;else ++failedClicks_;clickDelays_.push_back((clockSeconds()-pending_->stopTime)*1000);}
        }
        pending_.reset();pendingAudio_=false;
    }
    if(!renderer_.draw(whip_,flash_,std::clamp((flashUntil_-begin)/0.12,0.0,1.0))) {
        if(smoke_) {exitCode_=2;PostMessageW(main_,WM_EXIT_APP,0,0);}
        else {manualPause_=true;reconcile();renderer_.rebuild();}
    }
    frameCosts_.push_back((clockSeconds()-begin)*1000);if(frameCosts_.size()>20000) frameCosts_.erase(frameCosts_.begin(),frameCosts_.begin()+10000);
    if(!preview_ && !smoke_ && !pending_ && begin-lastMovement_>3 && (whip_.motion()<8 || begin-lastMovement_>8)) stopFrames();
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
void Application::togglePanel() {
    if(IsWindowVisible(panel_)) {hidePanel();return;}
    uiPause_=true;reconcile();
    POINT point{};GetCursorPos(&point);NOTIFYICONIDENTIFIER identifier{sizeof(identifier),main_,1,{}};RECT tray{};
    if(preview_) {const auto area=renderer_.secondaryMonitor();point={area.x+area.width-220,area.y+400};}
    else if(SUCCEEDED(Shell_NotifyIconGetRect(&identifier,&tray))) point={tray.left,tray.top};
    const UINT dpi=GetDpiForWindow(panel_);RECT panelSize{0,0,MulDiv(340,dpi,96),MulDiv(224,dpi,96)};
    AdjustWindowRectExForDpi(&panelSize,static_cast<DWORD>(GetWindowLongPtrW(panel_,GWL_STYLE)),FALSE,static_cast<DWORD>(GetWindowLongPtrW(panel_,GWL_EXSTYLE)),dpi);
    const int width=panelSize.right-panelSize.left,height=panelSize.bottom-panelSize.top;
    MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&info);
    const int x=std::clamp(point.x-width/2,info.rcWork.left,info.rcWork.right-width),y=std::clamp(point.y-height-10,info.rcWork.top,info.rcWork.bottom-height);
    SetWindowPos(panel_,HWND_TOPMOST,x,y,width,height,SWP_SHOWWINDOW);SetForegroundWindow(panel_);SetFocus(panel_);InvalidateRect(panel_,nullptr,FALSE);
}
void Application::hidePanel() {ShowWindow(panel_,SW_HIDE);uiPause_=manual_ && IsWindowVisible(manual_);reconcile();}
void Application::updateLanguage() {
    if(manual_) {
        SetWindowTextW(manual_,tr(L"Windows Whip — Manual",L"Windows Whip — Руководство"));
        auto text=resourceText(settings_.language==Language::English?201:202);std::wstring lines;
        for(auto c:text) {if(c==L'\n') lines+=L'\r';lines+=c;}
        SetWindowTextW(manualText_,lines.c_str());
        SendMessageW(manualText_,EM_SETSEL,0,0);SendMessageW(manualText_,EM_SCROLLCARET,0,0);SendMessageW(manualText_,WM_VSCROLL,SB_TOP,0);
    }
    settings_.save();updateIcon();InvalidateRect(panel_,nullptr,FALSE);
}
void Application::showManual() {
    uiPause_=true;
    if(!manual_) {
        const UINT dpi=GetDpiForWindow(panel_);RECT size{0,0,MulDiv(560,dpi,96),MulDiv(400,dpi,96)};AdjustWindowRectExForDpi(&size,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_TOOLWINDOW,dpi);
        RECT location{};GetWindowRect(panel_,&location);
        const auto area=renderer_.secondaryMonitor();
        const int manualX=preview_?area.x+(area.width-(size.right-size.left))/2:location.left;
        const int manualY=preview_?area.y+(area.height-(size.bottom-size.top))/2:static_cast<int>(location.top-100);
        manual_=CreateWindowExW(preview_?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW,manualClass,tr(L"Windows Whip — Manual",L"Windows Whip — Руководство"),WS_OVERLAPPEDWINDOW,manualX,manualY,size.right-size.left,size.bottom-size.top,preview_?nullptr:main_,nullptr,instance_,this);
        const BOOL dark=TRUE;DwmSetWindowAttribute(manual_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    }
    ShowWindow(manual_,SW_SHOWNORMAL);ShowWindow(panel_,SW_HIDE);uiPause_=true;reconcile();updateLanguage();SetForegroundWindow(manual_);
}
void Application::panelAction(int action) {
    if(action==0) {switchFrom_=settings_.mode==Mode::Sadomaso?1:0;settings_.mode=settings_.mode==Mode::Sadomaso?Mode::Exact:Mode::Sadomaso;animationStart_=clockSeconds();SetTimer(panel_,901,16,nullptr);settings_.save();resetMotion();}
    if(action==1) {settings_.language=settings_.language==Language::English?Language::Russian:Language::English;updateLanguage();}
    if(action==2) showManual();
    updateIcon();InvalidateRect(panel_,nullptr,FALSE);
}
void Application::paintPanel() {
    PAINTSTRUCT paint{};BeginPaint(panel_,&paint);
    if(!uiTarget_) {
        RECT client{};GetClientRect(panel_,&client);
        uiFactory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(panel_,D2D1::SizeU(client.right,client.bottom)),&uiTarget_);
    }
    if(uiTarget_) {
        const float dpi=static_cast<float>(GetDpiForWindow(panel_));uiTarget_->SetDpi(dpi,dpi);auto* target=uiTarget_.Get();target->BeginDraw();target->Clear(D2D1::ColorF(0x1b2234));
        const D2D1_GRADIENT_STOP stops[]={{0,D2D1::ColorF(0x203653)},{1,D2D1::ColorF(0x1b2234)}};
        ComPtr<ID2D1GradientStopCollection> colors;ComPtr<ID2D1LinearGradientBrush> background;
        target->CreateGradientStopCollection(stops,2,&colors);
        target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0,0),D2D1::Point2F(340,224)),colors.Get(),&background);
        const auto size=target->GetSize();if(background) target->FillRectangle(D2D1::RectF(0,0,size.width,size.height),background.Get());
        ComPtr<ID2D1SolidColorBrush> brush;target->CreateSolidColorBrush(D2D1::ColorF(0x26344a),&brush);
        const auto text=[&](const wchar_t* value,D2D1_RECT_F box,float size,UINT32 color,bool bold=false){
            ComPtr<IDWriteTextFormat> format;writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&format);
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);brush->SetColor(D2D1::ColorF(color));target->DrawTextW(value,static_cast<UINT32>(wcslen(value)),format.Get(),box,brush.Get());
        };
        brush->SetColor(D2D1::ColorF(0x46546b));target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(0.75f,0.75f,size.width-0.75f,size.height-0.75f),13,13),brush.Get(),1);
        text(tr(L"Exact",L"Точный"),D2D1::RectF(6,25,111,85),15,0xf4f6fa);
        text(tr(L"Sadomaso",L"Садомазо"),D2D1::RectF(223,25,336,85),15,0xf4f6fa);
        brush->SetColor(D2D1::ColorF(0x37b34c));target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(116,27,220,85),29,29),brush.Get());
        const double elapsed=std::clamp((clockSeconds()-animationStart_)/0.16,0.0,1.0),eased=1-std::pow(1-elapsed,3);
        const double selected=switchFrom_+((settings_.mode==Mode::Sadomaso?1.0:0.0)-switchFrom_)*eased;
        brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<float>(145+46*selected),56),23,23),brush.Get());
        brush->SetColor(D2D1::ColorF(0x748ba9));target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(90,109,250,153),22,22),brush.Get(),1.5f);
        const float selectedX=settings_.language==Language::English?90.0f:170.0f;
        brush->SetColor(D2D1::ColorF(0x4cc2ff));
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(selectedX,109,selectedX+80,153),22,22),brush.Get());
        text(L"EN",D2D1::RectF(90,109,170,153),18,settings_.language==Language::English?0x10253b:0xe8eef7,true);
        text(L"RU",D2D1::RectF(170,109,250,153),18,settings_.language==Language::Russian?0x10253b:0xe8eef7,true);
        brush->SetColor(D2D1::ColorF(0x4cc2ff));target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(90,171,250,207),9,9),brush.Get());
        brush->SetColor(D2D1::ColorF(0x72d0ff));target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(90.5f,171.5f,249.5f,206.5f),9,9),brush.Get(),0.8f);
        text(tr(L"Manual",L"Руководство"),D2D1::RectF(90,171,250,207),15,0x10253b,true);
        if(GetFocus()==panel_) {brush->SetColor(D2D1::ColorF(0x7892ad));const auto box=focus_==0?D2D1::RectF(112,23,224,89):(focus_==1?D2D1::RectF(86,105,254,157):D2D1::RectF(86,167,254,211));target->DrawRoundedRectangle(D2D1::RoundedRect(box,10,10),brush.Get(),0.7f);}
        if(FAILED(target->EndDraw())) uiTarget_.Reset();
    }
    EndPaint(panel_,&paint);
}
LRESULT CALLBACK Application::panelProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* app=self(window);if(!app) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_PAINT:app->paintPanel();return 0;
    case WM_ERASEBKGND:return 1;
    case WM_ACTIVATE:if(LOWORD(wp)==WA_INACTIVE && !app->preview_) app->hidePanel();return 0;
    case WM_SIZE:if(app->uiTarget_) app->uiTarget_->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));return 0;
    case WM_DPICHANGED:{const auto* rect=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);app->uiTarget_.Reset();return 0;}
    case WM_TIMER:InvalidateRect(window,nullptr,FALSE);if(clockSeconds()-app->animationStart_>0.18) KillTimer(window,901);return 0;
    case WM_LBUTTONUP:{const double scale=GetDpiForWindow(window)/96.0,x=GET_X_LPARAM(lp)/scale,y=GET_Y_LPARAM(lp)/scale;if(y>=23 && y<=89) app->panelAction(0);else if(x>=90 && x<=250 && y>=109 && y<=153) {const auto lang=x<170?Language::English:Language::Russian;if(lang!=app->settings_.language) app->panelAction(1);}else if(x>=90 && x<=250 && y>=171 && y<=207) app->panelAction(2);return 0;}
    case WM_KEYDOWN:if(wp==VK_ESCAPE) PostMessageW(app->main_,WM_EXIT_APP,0,0);else if(wp==VK_TAB) {app->focus_=(app->focus_+1)%3;InvalidateRect(window,nullptr,FALSE);}else if(wp==VK_SPACE || wp==VK_RETURN) app->panelAction(app->focus_);else if(wp==VK_LEFT || wp==VK_RIGHT) {if(app->focus_<2) app->panelAction(app->focus_);}return 0;
    case WM_CLOSE:if(app->preview_) PostMessageW(app->main_,WM_EXIT_APP,0,0);else app->hidePanel();return 0;
    }
    return DefWindowProcW(window,message,wp,lp);
}
LRESULT CALLBACK Application::manualProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE) SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* app=self(window);if(!app) return DefWindowProcW(window,message,wp,lp);
    switch(message) {
    case WM_CREATE:{app->manualText_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,20,20,500,340,window,nullptr,app->instance_,nullptr);const UINT dpi=GetDpiForWindow(window);app->manualFont_=CreateFontW(-MulDiv(11,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");SendMessageW(app->manualText_,WM_SETFONT,reinterpret_cast<WPARAM>(app->manualFont_),TRUE);return 0;}
    case WM_SIZE:{const int margin=MulDiv(20,GetDpiForWindow(window),96);MoveWindow(app->manualText_,margin,margin,std::max(1,static_cast<int>(LOWORD(lp))-2*margin),std::max(1,static_cast<int>(HIWORD(lp))-2*margin),TRUE);return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(wp),RGB(241,245,250));SetBkColor(reinterpret_cast<HDC>(wp),RGB(27,34,52));return reinterpret_cast<LRESULT>(app->manualBackground_);
    case WM_DPICHANGED:{const auto* rect=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER);if(app->manualFont_) DeleteObject(app->manualFont_);app->manualFont_=CreateFontW(-MulDiv(11,HIWORD(wp),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");SendMessageW(app->manualText_,WM_SETFONT,reinterpret_cast<WPARAM>(app->manualFont_),TRUE);return 0;}
    case WM_CLOSE:ShowWindow(window,SW_HIDE);app->uiPause_=false;app->reconcile();return 0;
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
    case WM_WAKE:if(!paused_) startFrames();return 0;
    case WM_EXIT_APP:DestroyWindow(main_);return 0;
    case WM_DESTROY:shuttingDown_=true;restoreCursor();renderer_.hide();if(audio_) audio_->stopAll();PostQuitMessage(exitCode_);return 0;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(wp) {restoreCursor();PostMessageW(main_,WM_EXIT_APP,0,0);}return 0;
    case WM_PAUSE_APP:manualPause_=!manualPause_;reconcile();return 0;
    case WM_PANEL:togglePanel();return 0;
    case WM_DISPLAYCHANGE:restoreCursor();renderer_.rebuild();resetMotion();if(!paused_) {if(magnification_ && !preview_ && !smoke_) cursorHidden_=MagShowSystemCursor(FALSE)!=FALSE;startFrames();}return 0;
    case WM_DEVICECHANGE:if(audio_) audio_->recover();return 0;
    case WM_WTSSESSION_CHANGE:if(wp==WTS_SESSION_LOCK || wp==WTS_SESSION_LOGOFF) sessionPause_=true;else if(wp==WTS_SESSION_UNLOCK || wp==WTS_SESSION_LOGON) {sessionPause_=false;resetMotion();}reconcile();return 0;
    case WM_POWERBROADCAST:if(wp==PBT_APMSUSPEND) sessionPause_=true;else if(wp==PBT_APMRESUMEAUTOMATIC || wp==PBT_APMRESUMESUSPEND) {restoreCursor();sessionPause_=false;resetMotion();if(audio_) audio_->recover();}reconcile();return TRUE;
    case WM_INPUT:if(input_ && !input_->hooksActive()) {UINT size=sizeof(RAWINPUT);RAWINPUT raw{};if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))!=static_cast<UINT>(-1) && raw.header.dwType==RIM_TYPEKEYBOARD) {const auto& key=raw.data.keyboard;const bool down=!(key.Flags&RI_KEY_BREAK);if(down && key.VKey==VK_ESCAPE) PostMessageW(main_,WM_EXIT_APP,0,0);if(key.VKey==VK_F8) {if(down && !rawF8_) PostMessageW(main_,WM_PAUSE_APP,0,0);rawF8_=down;}}}return DefWindowProcW(main_,message,wp,lp);
    case WM_TRAY:{const UINT event=LOWORD(lp);if(event==NIN_SELECT || event==NIN_KEYSELECT || event==WM_LBUTTONUP) togglePanel();else if(event==WM_CONTEXTMENU || event==WM_RBUTTONUP) {uiPause_=true;reconcile();POINT point{};GetCursorPos(&point);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,manualPause_?tr(L"Resume",L"Продолжить"):tr(L"Pause",L"Пауза"));AppendMenuW(menu,MF_STRING,2,tr(L"Exit",L"Выход"));SetForegroundWindow(main_);const int selection=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,main_,nullptr);DestroyMenu(menu);uiPause_=manual_ && IsWindowVisible(manual_);if(selection==1) manualPause_=!manualPause_;if(selection==2) PostMessageW(main_,WM_EXIT_APP,0,0);reconcile();}return 0;}
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
    panel_=CreateWindowExW((preview_?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_TOPMOST,panelClass,preview_?L"Windows Whip Preview":L"Windows Whip",WS_POPUP|WS_BORDER|(preview_?(WS_CAPTION|WS_SYSMENU):0),0,0,340,224,preview_?nullptr:main_,nullptr,instance_,this);
    const BOOL darkTheme=TRUE;const DWORD roundCorner=2;const DWORD backdrop=3;const COLORREF borderColor=RGB(68,83,109);
    DwmSetWindowAttribute(panel_,DWMWA_USE_IMMERSIVE_DARK_MODE,&darkTheme,sizeof(darkTheme));
    DwmSetWindowAttribute(panel_,DWMWA_WINDOW_CORNER_PREFERENCE,&roundCorner,sizeof(roundCorner));
    DwmSetWindowAttribute(panel_,DWMWA_SYSTEMBACKDROP_TYPE,&backdrop,sizeof(backdrop));
    DwmSetWindowAttribute(panel_,DWMWA_BORDER_COLOR,&borderColor,sizeof(borderColor));
    manualBackground_=CreateSolidBrush(RGB(27,34,52));
    WNDCLASSW manual=panel;manual.lpfnWndProc=manualProc;manual.lpszClassName=manualClass;manual.hbrBackground=manualBackground_;RegisterClassW(&manual);
    audio_=std::make_unique<Audio>();
    if(!smoke_ && !preview_) {
        input_=std::make_unique<Input>(main_);
        RAWINPUTDEVICE keyboard{0x01,0x06,RIDEV_INPUTSINK,main_};RegisterRawInputDevices(&keyboard,1,sizeof(keyboard));
        startWatchdog();magnification_=guardStop_ && MagInitialize();
        WTSRegisterSessionNotification(main_,NOTIFY_FOR_THIS_SESSION);
    }
    taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");updateIcon(true);
    started_=clockSeconds();resetMotion();SetTimer(main_,monitorTimer,250,nullptr);
    if(smoke_ || preview_) {const auto area=renderer_.secondaryMonitor();lastCursor_={area.x+area.width*0.48,area.y+area.height*0.12};whip_.reset(lastCursor_);gesture_.reset(started_,lastCursor_);}
    paused_=true;fullscreenPause_=!smoke_ && !preview_ && fullscreen();reconcile();
    if(smoke_ || preview_) {paused_=false;startFrames();if(preview_ && showPanelOnStart_) togglePanel();}
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0) {TranslateMessage(&message);DispatchMessageW(&message);}
    if(smoke_) {
        std::sort(frameCosts_.begin(),frameCosts_.end());const double p95=frameCosts_.empty()?0:frameCosts_[static_cast<std::size_t>((frameCosts_.size()-1)*0.95)];
        const auto report=std::filesystem::path(executablePath()).parent_path()/L"smoke-report.json";
        std::ofstream output(report);output<<"{\"frames\":"<<frameCosts_.size()<<",\"frame_p95_ms\":"<<p95<<",\"monitors\":"<<renderer_.monitorCount()<<",\"audio_available\":"<<(audio_->available()?"true":"false")<<",\"injected_clicks\":"<<clicks_<<",\"exit_code\":"<<exitCode_<<"}\n";
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
    if(arguments.starts_with(L"--watchdog ")) return whip::watchdog(static_cast<DWORD>(wcstoul(arguments.c_str()+11,nullptr,10)));
    const bool smoke=arguments.find(L"--smoke-test")!=std::wstring::npos,preview=arguments.find(L"--preview")!=std::wstring::npos;
    HANDLE mutex=nullptr;
    if(!smoke && !preview) {mutex=CreateMutexW(nullptr,FALSE,L"Local\\WindowsWhip.SingleInstance");if(GetLastError()==ERROR_ALREADY_EXISTS) {if(auto window=FindWindowW(whip::appClass,L"Windows Whip")) PostMessageW(window,whip::WM_PANEL,0,0);CloseHandle(mutex);return 0;}}
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;{whip::Application app(instance,preview,smoke,arguments.find(L"--preview-lash")==std::wstring::npos);result=app.run();}
    if(SUCCEEDED(com)) CoUninitialize();if(mutex) CloseHandle(mutex);return result;
}
