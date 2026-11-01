#include "renderer.hpp"
#include "lash_visual.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace whip {
using Microsoft::WRL::ComPtr;
static LRESULT CALLBACK overlayProc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCHITTEST) return HTTRANSPARENT;
    if(message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_PAINT) {PAINTSTRUCT paint{};BeginPaint(window,&paint);EndPaint(window,&paint);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
bool Renderer::initialize(HINSTANCE instance,bool inspectable) {
    instance_=instance;inspectable_=inspectable;
    WNDCLASSW type{};type.lpfnWndProc=overlayProc;type.hInstance=instance;type.lpszClassName=L"WindowsWhip.Overlay";RegisterClassW(&type);
    D3D_FEATURE_LEVEL level{};
    HRESULT result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d_,&level,nullptr);
    if(FAILED(result)) result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&d3d_,&level,nullptr);
    if(FAILED(result)) return false;
    ComPtr<IDXGIDevice> dxgi;d3d_.As(&dxgi);
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,IID_PPV_ARGS(&factory_)))) return false;
    if(FAILED(factory_->CreateDevice(dxgi.Get(),&device_))) return false;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory_)))) return false;
    if(FAILED(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&composition_)))) return false;
    return rebuild();
}
bool Renderer::rebuild() {
    windows_.clear();
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR,HDC,LPRECT rect,LPARAM data)->BOOL {return reinterpret_cast<Renderer*>(data)->addMonitor(*rect)?TRUE:FALSE;},reinterpret_cast<LPARAM>(this));
    return !windows_.empty();
}
bool Renderer::addMonitor(const RECT& monitor) {
    MONITORINFO information{sizeof(information)};GetMonitorInfoW(MonitorFromRect(&monitor,MONITOR_DEFAULTTONEAREST),&information);
    if(inspectable_ && (information.dwFlags&MONITORINFOF_PRIMARY) && GetSystemMetrics(SM_CMONITORS)>1) return true;
    auto surface=std::make_unique<Surface>();surface->monitor=monitor;
    surface->window=CreateWindowExW(WS_EX_TOPMOST|(inspectable_?WS_EX_APPWINDOW:WS_EX_TOOLWINDOW)|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,L"WindowsWhip.Overlay",inspectable_?L"Windows Whip Lash Preview":L"Windows Whip overlay",WS_POPUP|(inspectable_?(WS_CAPTION|WS_SYSMENU):0),monitor.left,monitor.top,monitor.right-monitor.left,monitor.bottom-monitor.top,nullptr,nullptr,instance_,nullptr);
    if(!surface->window) return false;
    SetLayeredWindowAttributes(surface->window,0,255,LWA_ALPHA);
    DXGI_SWAP_CHAIN_DESC1 description{};description.Width=512;description.Height=512;description.Format=DXGI_FORMAT_B8G8R8A8_UNORM;description.SampleDesc.Count=1;
    description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;description.BufferCount=2;description.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;description.AlphaMode=DXGI_ALPHA_MODE_PREMULTIPLIED;
    if(FAILED(dxgiFactory_->CreateSwapChainForComposition(d3d_.Get(),&description,nullptr,&surface->swap))) return false;
    if(FAILED(device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&surface->context))) return false;
    ComPtr<IDXGISurface> buffer;surface->swap->GetBuffer(0,IID_PPV_ARGS(&buffer));
    const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    if(FAILED(surface->context->CreateBitmapFromDxgiSurface(buffer.Get(),properties,&surface->bitmap))) return false;
    surface->context->SetTarget(surface->bitmap.Get());
    if(FAILED(composition_->CreateTargetForHwnd(surface->window,TRUE,&surface->target))) return false;
    if(FAILED(composition_->CreateVisual(&surface->visual))) return false;
    surface->visual->SetContent(surface->swap.Get());surface->target->SetRoot(surface->visual.Get());
    windows_.push_back(std::move(surface));return true;
}
void Renderer::hide() {for(auto& surface:windows_) ShowWindow(surface->window,SW_HIDE);}
Rect Renderer::desktop() const {return {GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN),GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN)};}
Rect Renderer::secondaryMonitor() const {
    for(const auto& surface:windows_) {MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromRect(&surface->monitor,MONITOR_DEFAULTTONEAREST),&info);if(!(info.dwFlags&MONITORINFOF_PRIMARY)) return {info.rcWork.left,info.rcWork.top,info.rcWork.right-info.rcWork.left,info.rcWork.bottom-info.rcWork.top};}
    if(!windows_.empty()) {const auto& r=windows_.front()->monitor;return {r.left,r.top,r.right-r.left,r.bottom-r.top};}return desktop();
}
bool Renderer::contains(Vec2 point) const {
    return std::any_of(windows_.begin(),windows_.end(),[point](const auto& s){return point.x>=s->monitor.left && point.x<s->monitor.right && point.y>=s->monitor.top && point.y<s->monitor.bottom;});
}
bool Renderer::draw(const Whip& whip,Vec2 flash,double amount) {
    return drawPoints(whip.points(),flash,amount);
}
bool Renderer::drawPoints(const std::array<Vec2,Whip::segments+1>& points,Vec2 flash,double amount) {
    double minX=points[0].x,minY=points[0].y,maxX=minX,maxY=minY;
    for(auto p:points) {minX=std::min(minX,p.x);minY=std::min(minY,p.y);maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);}
    if(amount>0) {minX=std::min(minX,flash.x-24);minY=std::min(minY,flash.y-24);maxX=std::max(maxX,flash.x+24);maxY=std::max(maxY,flash.y+24);}
    minX=std::floor(minX-12);minY=std::floor(minY-12);maxX+=12;maxY+=12;
    const UINT requiredWidth=static_cast<UINT>(std::ceil((maxX-minX)/64)*64),requiredHeight=static_cast<UINT>(std::ceil((maxY-minY)/64)*64);
    if(requiredWidth>8192 || requiredHeight>8192) return false;
    for(auto& surface:windows_) {
        const bool visible=maxX>=surface->monitor.left && minX<surface->monitor.right && maxY>=surface->monitor.top && minY<surface->monitor.bottom;
        if(!visible) {ShowWindow(surface->window,SW_HIDE);continue;}
        DXGI_SWAP_CHAIN_DESC1 current{};surface->swap->GetDesc1(&current);
        if(requiredWidth>current.Width || requiredHeight>current.Height) {
            surface->context->SetTarget(nullptr);surface->bitmap.Reset();
            if(FAILED(surface->swap->ResizeBuffers(2,std::max(current.Width,requiredWidth),std::max(current.Height,requiredHeight),DXGI_FORMAT_B8G8R8A8_UNORM,0))) return false;
            ComPtr<IDXGISurface> buffer;surface->swap->GetBuffer(0,IID_PPV_ARGS(&buffer));
            const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
            if(FAILED(surface->context->CreateBitmapFromDxgiSurface(buffer.Get(),properties,&surface->bitmap))) return false;
            surface->context->SetTarget(surface->bitmap.Get());
        }
        auto* context=surface->context.Get();ComPtr<ID2D1SolidColorBrush> dark,light;
        context->CreateSolidColorBrush(D2D1::ColorF(0.035f,0.04f,0.05f),&dark);context->CreateSolidColorBrush(D2D1::ColorF(0.98f,0.98f,0.98f),&light);
        context->BeginDraw();context->Clear(D2D1::ColorF(0,0,0,0));
        drawLash(context,factory_.Get(),points,style_,{minX,minY},1,true,&surface->raster);
        if(amount>0) {
            light->SetOpacity(static_cast<float>(amount));dark->SetOpacity(static_cast<float>(amount));
            const auto ring=D2D1::Ellipse(D2D1::Point2F(static_cast<float>(flash.x-minX),static_cast<float>(flash.y-minY)),static_cast<float>(4+(1-amount)*16),static_cast<float>(4+(1-amount)*16));
            context->DrawEllipse(ring,dark.Get(),3);context->DrawEllipse(ring,light.Get(),1.2f);
        }
        if(FAILED(context->EndDraw())) return false;
        if(FAILED(surface->swap->Present(0,0))) return false;
        surface->visual->SetOffsetX(static_cast<float>(minX-surface->monitor.left));surface->visual->SetOffsetY(static_cast<float>(minY-surface->monitor.top));
        ShowWindow(surface->window,SW_SHOWNOACTIVATE);
    }
    return SUCCEEDED(composition_->Commit());
}
}
