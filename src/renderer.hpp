#pragma once
#include "core.hpp"
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <memory>

namespace whip {
class Renderer {
public:
    bool initialize(HINSTANCE instance,bool inspectable=false);
    bool rebuild();
    bool draw(const Whip& whip,Vec2 flash,double flashAmount);
    bool drawPoints(const std::array<Vec2,Whip::segments+1>& points,Vec2 flash={},double flashAmount=0);
    void hide();
    bool contains(Vec2 point) const;
    Rect desktop() const;
    Rect secondaryMonitor() const;
    std::size_t monitorCount() const {return windows_.size();}
    void style(WhipStyle value) {style_=value;}
private:
    using Factory=Microsoft::WRL::ComPtr<ID2D1Factory1>;
    struct Surface {
        HWND window{};RECT monitor{};
        Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> raster;
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swap;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
        Microsoft::WRL::ComPtr<IDCompositionTarget> target;
        Microsoft::WRL::ComPtr<IDCompositionVisual> visual;
        ~Surface(){if(window) DestroyWindow(window);}
    };
    bool addMonitor(const RECT& monitor);
    HINSTANCE instance_{};
    bool inspectable_{};
    WhipStyle style_=WhipStyle::Spring;
    Factory factory_;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
    Microsoft::WRL::ComPtr<ID2D1Device> device_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> composition_;
    std::vector<std::unique_ptr<Surface>> windows_;
};
}
