#include "design_preview.hpp"
#include "core.hpp"
#include "platform.hpp"
#include "audio.hpp"
#include "lash_visual.hpp"
#include <dwrite.h>
#include <wincodec.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>

namespace whip {
using Microsoft::WRL::ComPtr;
namespace {
struct Gallery {
    ComPtr<ID2D1Factory> factory;ComPtr<IDWriteFactory> write;ComPtr<ID2D1HwndRenderTarget> target;
    std::array<Whip,3> whips;double last{},elapsed{};bool frozen{};HWND window{};
    void reset() {for(int i=0;i<3;++i) {whips[i].style(static_cast<WhipStyle>(i));whips[i].reset({0,0});}elapsed=0;}
    void advance(double dt) {
        if(frozen) return;
        elapsed+=dt;const double t=std::fmod(elapsed,6.0);
        // Identical gesture for all profiles: a broad swing, then a full stop.
        const double phase=std::min(t,1.2);const Vec2 cursor{std::sin(phase*5.0)*150,std::sin(phase*4.0)*75};
        if(t<dt) reset();for(auto& whip:whips) whip.advance(dt,cursor,0);
    }
    void label(ID2D1RenderTarget* render,const wchar_t* text,D2D1_RECT_F box,float size,bool strong=false,UINT32 color=0xf2f2f3) {
        ComPtr<IDWriteTextFormat> format;ComPtr<ID2D1SolidColorBrush> brush;
        write->CreateTextFormat(L"Segoe UI",nullptr,strong?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&format);
        render->CreateSolidColorBrush(D2D1::ColorF(color),&brush);render->DrawTextW(text,static_cast<UINT32>(wcslen(text)),format.Get(),box,brush.Get());
    }
    void draw(ID2D1RenderTarget* render) {
        const auto size=render->GetSize();const float sx=size.width/1200,sy=size.height/880;
        render->SetTransform(D2D1::Matrix3x2F::Scale(sx,sy));render->Clear(D2D1::ColorF(0x17181b));
        label(render,L"Choose the feel",{32,24,1000,62},25,true);
        label(render,L"Same movement. Six physical links. One seamless vector stroke.",{32,65,1168,100},16,false,0xaeb0b7);
        const wchar_t* names[]{L"A · Contour",L"B · Heavy",L"C · Pixel spring"};
        const wchar_t* descriptions[]{L"Closest to the reference\nClean, steady, balanced",L"Thicker and heavier\nStronger downward pull",L"Pixel edges · Wider handle\nMore elastic recovery"};
        for(int i=0;i<3;++i) {
            const float x=static_cast<float>(i*400);ComPtr<ID2D1SolidColorBrush> line;render->CreateSolidColorBrush(D2D1::ColorF(0x35363b),&line);
            if(i) render->DrawLine({x,124},{x,810},line.Get(),1);
            label(render,names[i],{x+28,124,x+380,164},22,true);label(render,descriptions[i],{x+28,166,x+380,222},15,false,0xaeb0b7);
            render->PushAxisAlignedClip({x+12,230,x+388,810},D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            render->SetTransform(D2D1::Matrix3x2F::Translation(x+190,278)*D2D1::Matrix3x2F::Scale(sx,sy));
            drawLash(render,factory.Get(),whips[i].points(),static_cast<WhipStyle>(i),{},0.52f,false);
            render->PopAxisAlignedClip();render->SetTransform(D2D1::Matrix3x2F::Scale(sx,sy));
        }
        label(render,frozen?L"Space: play   ·   Close the window to leave":L"Space: freeze   ·   Close the window to leave",{32,834,1160,868},14,false,0xaeb0b7);
        render->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    bool snapshot() {
        ComPtr<IWICImagingFactory> imaging;ComPtr<IWICBitmap> bitmap;ComPtr<ID2D1RenderTarget> render;
        if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&imaging)))) return false;
        imaging->CreateBitmap(1440,1056,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap);
        if(!bitmap || FAILED(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(),&render))) return false;
        render->BeginDraw();draw(render.Get());if(FAILED(render->EndDraw())) return false;
        const auto file=std::filesystem::path(executablePath()).parent_path()/L"Whip-variants.png";
        ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> options;
        imaging->CreateStream(&stream);if(!stream || FAILED(stream->InitializeFromFilename(file.c_str(),GENERIC_WRITE))) return false;
        imaging->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder);if(!encoder || FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))) return false;
        encoder->CreateNewFrame(&frame,&options);if(!frame || FAILED(frame->Initialize(options.Get()))) return false;
        frame->SetSize(1440,1056);WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;frame->SetPixelFormat(&format);
        return SUCCEEDED(frame->WriteSource(bitmap.Get(),nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
    }
};
LRESULT CALLBACK galleryProc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCCREATE) SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* gallery=reinterpret_cast<Gallery*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(!gallery) return DefWindowProcW(window,msg,wp,lp);
    switch(msg) {
    case WM_TIMER:{const double now=clockSeconds();gallery->advance(std::clamp(now-gallery->last,0.0,0.030));gallery->last=now;InvalidateRect(window,nullptr,FALSE);return 0;}
    case WM_KEYDOWN:if(wp==VK_SPACE && !(lp&(1LL<<30))) gallery->frozen=!gallery->frozen;return 0;
    case WM_ERASEBKGND:return 1;
    case WM_SIZE:if(gallery->target) gallery->target->Resize(D2D1::SizeU(LOWORD(lp),HIWORD(lp)));return 0;
    case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(window,&ps);RECT r{};GetClientRect(window,&r);if(!gallery->target) gallery->factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(r.right,r.bottom)),&gallery->target);if(gallery->target) {gallery->target->BeginDraw();gallery->draw(gallery->target.Get());if(FAILED(gallery->target->EndDraw())) gallery->target.Reset();}EndPaint(window,&ps);return 0;}
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
}
int runDesignPreview(HINSTANCE instance,bool snapshotOnly) {
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int result=0;
    {
        Gallery gallery;D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,gallery.factory.GetAddressOf());DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(gallery.write.GetAddressOf()));
        if(!gallery.factory || !gallery.write) result=2;
        else {
            gallery.reset();for(int i=0;i<470;++i) gallery.advance(Whip::step);gallery.frozen=true;
            if(!gallery.snapshot()) result=3;
            if(!snapshotOnly && !result) {
                RECT work{};EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL {MONITORINFO info{sizeof(info)};GetMonitorInfoW(monitor,&info);auto* area=reinterpret_cast<RECT*>(data);if(!(info.dwFlags&MONITORINFOF_PRIMARY)) {*area=info.rcWork;return FALSE;}if(IsRectEmpty(area)) *area=info.rcWork;return TRUE;},reinterpret_cast<LPARAM>(&work));
                const int width=std::min(1240,static_cast<int>(work.right-work.left-48)),height=std::min(940,static_cast<int>(work.bottom-work.top-48));
                WNDCLASSW type{};type.lpfnWndProc=galleryProc;type.hInstance=instance;type.lpszClassName=L"WindowsWhip.DesignPreview";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&type);
                gallery.window=CreateWindowExW(WS_EX_APPWINDOW,type.lpszClassName,L"Windows Whip — A / B / C",WS_OVERLAPPEDWINDOW,(work.left+work.right-width)/2,(work.top+work.bottom-height)/2,width,height,nullptr,nullptr,instance,&gallery);
                ShowWindow(gallery.window,SW_SHOWNORMAL);gallery.last=clockSeconds();SetTimer(gallery.window,1,16,nullptr);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0) {TranslateMessage(&msg);DispatchMessageW(&msg);}
            }
        }
    }
    if(SUCCEEDED(com)) CoUninitialize();return result;
}
int runAudioTest() {
    Audio audio;const double deadline=clockSeconds()+4;
    while(!audio.available() && clockSeconds()<deadline) Sleep(20);
    if(!audio.available()) return 2;
    audio.play(false,101,2000);Sleep(950);audio.play(false,102,2000);Sleep(950);audio.play(true,103,2000);Sleep(800);
    const auto stats=audio.stats();std::ofstream output(std::filesystem::path(executablePath()).parent_path()/L"audio-report.json");
    output<<"{\"loaded_bytes\":["<<stats.loaded[0]<<','<<stats.loaded[1]<<','<<stats.loaded[2]<<"],\"submitted\":["<<stats.submitted[0]<<','<<stats.submitted[1]<<','<<stats.submitted[2]<<"],\"observed\":["<<stats.observed[0]<<','<<stats.observed[1]<<','<<stats.observed[2]<<"]}\n";
    for(std::size_t i=0;i<3;++i) if(!stats.loaded[i] || !stats.submitted[i] || !stats.observed[i]) return 3;
    return 0;
}
}
