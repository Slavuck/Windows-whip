#include "renderer.hpp"
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
    const auto& points=whip.points();double minX=points[0].x,minY=points[0].y,maxX=minX,maxY=minY;
    for(auto p:points) {minX=std::min(minX,p.x);minY=std::min(minY,p.y);maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);}
    if(amount>0) {minX=std::min(minX,flash.x-24);minY=std::min(minY,flash.y-24);maxX=std::max(maxX,flash.x+24);maxY=std::max(maxY,flash.y+24);}
    minX=std::floor(minX-12);minY=std::floor(minY-12);maxX+=12;maxY+=12;
    const UINT requiredWidth=static_cast<UINT>(std::ceil((maxX-minX)/64)*64),requiredHeight=static_cast<UINT>(std::ceil((maxY-minY)/64)*64);
    if(requiredWidth>8192 || requiredHeight>8192) return false;
    std::vector<Vec2> curve;curve.reserve(257);
    for(std::size_t i=0;i<Whip::segments;++i) {
        const auto p0=points[i?i-1:0],p1=points[i],p2=points[i+1],p3=points[std::min(i+2,Whip::segments)];
        for(int j=0;j<4;++j) {const double t=j/4.0,t2=t*t,t3=t2*t;curve.push_back((p1*2+(p2-p0)*t+(p0*2-p1*5+p2*4-p3)*t2+(p3-p0+(p1-p2)*3)*t3)*0.5);}
    }
    curve.push_back(points.back());std::vector<D2D1_POINT_2F> left,right;
    for(std::size_t i=0;i<curve.size();++i) {
        auto direction=curve[std::min(i+1,curve.size()-1)]-curve[i?i-1:0];const double d=std::max(length(direction),1e-6);
        const double s=static_cast<double>(i)/(curve.size()-1),width=0.45+4.3*std::pow(1-s,1.45);
        const auto normal=Vec2{-direction.y/d,direction.x/d}*width;
        left.push_back(D2D1::Point2F(static_cast<float>(curve[i].x+normal.x-minX),static_cast<float>(curve[i].y+normal.y-minY)));
        right.push_back(D2D1::Point2F(static_cast<float>(curve[i].x-normal.x-minX),static_cast<float>(curve[i].y-normal.y-minY)));
    }
    ComPtr<ID2D1PathGeometry> geometry;factory_->CreatePathGeometry(&geometry);ComPtr<ID2D1GeometrySink> sink;geometry->Open(&sink);
    sink->BeginFigure(left.front(),D2D1_FIGURE_BEGIN_FILLED);sink->AddLines(left.data()+1,static_cast<UINT32>(left.size()-1));
    std::reverse(right.begin(),right.end());sink->AddLines(right.data(),static_cast<UINT32>(right.size()));sink->EndFigure(D2D1_FIGURE_END_CLOSED);sink->Close();
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
        context->BeginDraw();context->Clear(D2D1::ColorF(0,0,0,0));context->FillGeometry(geometry.Get(),dark.Get());context->DrawGeometry(geometry.Get(),light.Get(),1.35f);
        // A distinct short grip, with a rim and fine wraps, stays integrated
        // into the same live vector curve. The long lash keeps a clean taper.
        for(std::size_t i=0;i<3;++i) {
            const auto a=D2D1::Point2F(static_cast<float>(points[i].x-minX),static_cast<float>(points[i].y-minY));
            const auto b=D2D1::Point2F(static_cast<float>(points[i+1].x-minX),static_cast<float>(points[i+1].y-minY));
            context->DrawLine(a,b,light.Get(),13);context->DrawLine(a,b,dark.Get(),10.5f);
            const auto delta=points[i+1]-points[i];const double distance=std::max(length(delta),0.001);const Vec2 normal{-delta.y/distance*4.8,delta.x/distance*4.8};
            for(double t:{0.2,0.65}) {const auto center=mix(points[i],points[i+1],t);light->SetOpacity(0.55f);context->DrawLine(D2D1::Point2F(static_cast<float>(center.x-normal.x-minX),static_cast<float>(center.y-normal.y-minY)),D2D1::Point2F(static_cast<float>(center.x+normal.x-minX),static_cast<float>(center.y+normal.y-minY)),light.Get(),0.7f);light->SetOpacity(1);}
        }
        const auto cap=D2D1::Ellipse(D2D1::Point2F(static_cast<float>(points.front().x-minX),static_cast<float>(points.front().y-minY)),6,6);
        context->FillEllipse(cap,dark.Get());context->DrawEllipse(cap,light.Get(),1.2f);
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
