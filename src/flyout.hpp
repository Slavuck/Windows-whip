#pragma once
#include "core.hpp"
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>

namespace whip {
enum class UiAction { None, Mode, English, Russian, Manual, Training, Pause, Close };
struct UiState {
    Mode mode=Mode::Sadomaso;
    Language language=Language::English;
    bool paused{};
    UiAction hover=UiAction::None,focus=UiAction::None;
    double modePosition=1;
};
struct ManualBlock {
    enum class Kind { Lead, Body, Mode, Key, Note };
    Kind kind{};
    std::wstring title,text;
    float y{},height{},textHeight{};
};
struct ManualLayout {
    float width=460,height=800,scale=1;
    std::vector<ManualBlock> blocks;
};
class FlyoutView {
public:
    static constexpr float panelWidth=360,panelHeight=444;
    bool initialize(ID2D1Factory* factory,IDWriteFactory* write);
    static UiAction panelHit(float x,float y);
    static UiAction manualHit(float x,float y,float width);
    ManualLayout fitManual(const std::wstring& text,float availableWidth,float availableHeight) const;
    void paintPanel(HWND window,const UiState& state);
    void paintManual(HWND window,const UiState& state,const ManualLayout& layout);
    void resized(HWND window);
private:
    using Target=Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget>;
    Target panelTarget_,manualTarget_;
    HWND panelWindow_{},manualWindow_{};
    Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
    Microsoft::WRL::ComPtr<IDWriteFactory> write_;
    std::wstring font_=L"Segoe UI";
    Target& target(HWND window,bool manual);
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format(float size,bool strong=false) const;
    float textHeight(const std::wstring& text,float width,float size,bool strong=false) const;
    ManualLayout layoutManual(const std::wstring& text,float width,float scale) const;
};
}
