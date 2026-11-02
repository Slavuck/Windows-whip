#include "flyout.hpp"
#include <algorithm>
#include <cmath>

namespace whip {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT32 foreground=0xf3f6fa,secondary=0xb4c3cf;
const wchar_t* local(Language lang,const wchar_t* en,const wchar_t* ru) {return lang==Language::Russian?ru:en;}
bool inside(float x,float y,D2D1_RECT_F r) {return x>=r.left && x<=r.right && y>=r.top && y<=r.bottom;}
Microsoft::WRL::ComPtr<IDWriteTextLayout> manualText(IDWriteFactory* write,const std::wstring& font,const ManualBlock& block,float width,float size) {
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    write->CreateTextFormat(font.c_str(),nullptr,block.kind==ManualBlock::Kind::Note?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&format);
    if(!format) return layout;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,size*1.48f,size*1.12f);
    write->CreateTextLayout(block.text.c_str(),static_cast<UINT32>(block.text.size()),format.Get(),width,10000,&layout);
    if(layout) for(const auto& span:block.emphasis) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD,span);
    return layout;
}
ManualBlock manualBlock(const std::wstring& source) {
    ManualBlock block;block.kind=source.starts_with(L"Примечание") || source.starts_with(L"Note")?ManualBlock::Kind::Note:ManualBlock::Kind::Body;
    for(std::size_t i=0;i<source.size();) {
        if(source.compare(i,2,L"**")==0) {
            const auto end=source.find(L"**",i+2);
            if(end!=std::wstring::npos) {const auto start=block.text.size();block.text+=source.substr(i+2,end-i-2);block.emphasis.push_back({static_cast<UINT32>(start),static_cast<UINT32>(end-i-2)});i=end+2;continue;}
        }
        block.text+=source[i++];
    }
    return block;
}
struct Painter {
    ID2D1RenderTarget* target;IDWriteFactory* write;std::wstring font;
    ComPtr<ID2D1SolidColorBrush> brush;
    Painter(ID2D1RenderTarget* t,IDWriteFactory* w,std::wstring f):target(t),write(w),font(std::move(f)) {target->CreateSolidColorBrush(D2D1::ColorF(foreground),&brush);}
    void color(UINT32 c,float alpha=1) {brush->SetColor(D2D1::ColorF(c,alpha));}
    void line(float x,float y,float xx,float yy,UINT32 c,float thickness=1) {color(c);target->DrawLine({x,y},{xx,yy},brush.Get(),thickness);}
    void rounded(D2D1_RECT_F r,float radius,UINT32 c,float alpha=1) {color(c,alpha);target->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),brush.Get());}
    void ring(float x,float y,float r,UINT32 c,float thickness=1.5f) {color(c);target->DrawEllipse(D2D1::Ellipse({x,y},r,r),brush.Get(),thickness);}
    void text(const std::wstring& value,D2D1_RECT_F box,float size,UINT32 c,bool strong=false,bool center=false) {
        if(value.empty()) return;
        ComPtr<IDWriteTextFormat> f;write->CreateTextFormat(font.c_str(),nullptr,strong?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&f);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);f->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,size*1.48f,size*1.12f);
        if(center) {f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);}
        color(c);target->DrawTextW(value.c_str(),static_cast<UINT32>(value.size()),f.Get(),box,brush.Get());
    }
    void surface(float width,float height) {
        target->Clear(D2D1::ColorF(0x24262b));
        color(0x62656d,0.45f);target->DrawRoundedRectangle(D2D1::RoundedRect({0.6f,0.6f,width-0.6f,height-0.6f},12,12),brush.Get(),1);
    }
    void targetIcon(float x,float y,UINT32 c,float scale=1) {
        D2D1::Matrix3x2F previous;target->GetTransform(&previous);target->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale,{x,y})*previous);
        ring(x,y,9,c);ring(x,y,4.5f,c,1.4f);color(c);target->FillEllipse(D2D1::Ellipse({x,y},1.6f,1.6f),brush.Get());
        line(x-12,y,x-7,y,c);line(x+7,y,x+12,y,c);line(x,y-12,x,y-7,c);line(x,y+7,x,y+12,c);target->SetTransform(previous);
    }

};
std::vector<std::wstring> paragraphs(std::wstring text) {
    text.erase(std::remove(text.begin(),text.end(),L'\r'),text.end());
    std::vector<std::wstring> result;
    for(std::size_t p=0;p<text.size();) {const auto end=text.find(L"\n\n",p);auto item=text.substr(p,end==std::wstring::npos?end:end-p);while(!item.empty() && item.back()==L'\n') item.pop_back();if(!item.empty()) result.push_back(std::move(item));if(end==std::wstring::npos) break;p=end+2;}
    return result;
}

}
bool FlyoutView::initialize(ID2D1Factory* factory,IDWriteFactory* write) {
    factory_=factory;write_=write;if(!factory_ || !write_) return false;
    ComPtr<IDWriteFontCollection> fonts;if(SUCCEEDED(write_->GetSystemFontCollection(&fonts))) {UINT32 index{};BOOL exists{};fonts->FindFamilyName(L"Segoe UI Variable Text",&index,&exists);if(exists) font_=L"Segoe UI Variable Text";}
    return true;
}
float FlyoutView::textHeight(const ManualBlock& block,float width,float size) const {
    auto layout=manualText(write_.Get(),font_,block,width,size);if(!layout) return 0;
    DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);return std::ceil(metrics.height);
}
ManualLayout FlyoutView::layoutManual(const std::wstring& text,float width,float scale) const {
    ManualLayout result;result.width=width;result.scale=scale;float y=80;
    const auto content=paragraphs(text);
    for(std::size_t i=0;i<content.size();++i) {
        ManualBlock b=manualBlock(content[i]);b.y=y;
        b.textHeight=textHeight(b,width-56,19.5f*scale);
        b.height=b.textHeight+16*scale;result.blocks.push_back(b);y+=b.height;
    }
    result.height=std::ceil(y+12);return result;
}
ManualLayout FlyoutView::fitManual(const std::wstring& text,float availableWidth,float availableHeight) const {
    const float maxHeight=std::max(240.0f,availableHeight-32),maxWidth=std::max(260.0f,std::min(availableWidth-32,maxHeight-24));
    for(float scale=1.0f;scale>=0.50f;scale-=0.025f) {
        for(float width=std::min(520.0f,maxWidth);width<=std::min(640.0f,maxWidth);width+=20) {
            auto layout=layoutManual(text,width,scale);
            if(layout.height<=maxHeight && layout.height>width) return layout;
        }
    }
    return layoutManual(text,std::min(640.0f,maxWidth),0.50f);
}
UiAction FlyoutView::panelHit(float x,float y) {
    if(inside(x,y,{244,12,308,76})) return UiAction::Training;
    if(inside(x,y,{24,94,296,138})) return x<160?UiAction::ExactMode:UiAction::SadomasoMode;
    if(inside(x,y,{168,160,232,196})) return UiAction::English;
    if(inside(x,y,{232,160,296,196})) return UiAction::Russian;
    if(inside(x,y,{16,216,304,260})) return UiAction::Manual;
    if(inside(x,y,{16,280,304,328})) return UiAction::Pause;
    return UiAction::None;
}
UiAction FlyoutView::manualHit(float x,float y,float width) {return inside(x,y,{width-60,16,width-16,60})?UiAction::Close:UiAction::None;}
FlyoutView::Target& FlyoutView::target(HWND window,bool manual) {
    auto& result=manual?manualTarget_:panelTarget_;auto& saved=manual?manualWindow_:panelWindow_;
    if(saved!=window) {result.Reset();saved=window;}
    if(!result) {RECT r{};GetClientRect(window,&r);factory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),D2D1::HwndRenderTargetProperties(window,D2D1::SizeU(r.right,r.bottom)),&result);}
    if(result) {const float dpi=static_cast<float>(GetDpiForWindow(window));result->SetDpi(dpi,dpi);}
    return result;
}
void FlyoutView::resized(HWND window) {
    RECT r{};GetClientRect(window,&r);auto& render=window==manualWindow_?manualTarget_:panelTarget_;if(render) render->Resize(D2D1::SizeU(r.right,r.bottom));
}
void FlyoutView::paintPanel(HWND window,const UiState& state) {
    PAINTSTRUCT ps{};BeginPaint(window,&ps);auto& render=target(window,false);
    if(render) {
        render->BeginDraw();Painter p(render.Get(),write_.Get(),font_);const auto size=render->GetSize();p.surface(size.width,size.height);
        p.text(L"Windows Whip",{24,32,238,68},20,foreground,true);
        const bool training=state.hover==UiAction::Training || state.focus==UiAction::Training;
        p.rounded({244,12,308,76},8,0xffffff,training?0.085f:0.045f);p.targetIcon(276,44,secondary,1.75f);
        p.rounded({24,94,296,138},22,0x1b1d21);p.color(0x575b65);render->DrawRoundedRectangle(D2D1::RoundedRect({24,94,296,138},22,22),p.brush.Get(),1);
        const float selection=static_cast<float>(28+132*state.modePosition);p.rounded({selection,98,selection+132,134},18,0x2fa943);
        p.text(local(state.language,L"Exact",L"Точный"),{28,94,160,138},16,foreground,state.mode==Mode::Exact,true);
        p.text(local(state.language,L"Sadomaso",L"Садомазо"),{160,94,292,138},16,foreground,state.mode==Mode::Sadomaso,true);
        p.text(local(state.language,L"Language",L"Язык"),{24,164,160,196},16,foreground);
        p.rounded({168,160,296,196},18,0x1b1d21);p.color(0x575b65);render->DrawRoundedRectangle(D2D1::RoundedRect({168,160,296,196},18,18),p.brush.Get(),1);
        const float selected=state.language==Language::English?168.0f:232.0f;p.rounded({selected,160,selected+64,196},18,0x394c6b);
        p.text(L"EN",{168,160,232,196},14,foreground,true,true);p.text(L"RU",{232,160,296,196},14,foreground,true,true);
        if(state.hover==UiAction::Manual || state.focus==UiAction::Manual) p.rounded({16,216,304,260},8,0xffffff,0.04f);
        p.text(local(state.language,L"Manual",L"Мануал"),{24,224,296,258},17,foreground);
        p.line(24,276,296,276,0x3a3d44);
        if(state.hover==UiAction::Pause || state.focus==UiAction::Pause) p.rounded({16,280,304,328},8,0xffffff,0.04f);
        p.text(local(state.language,state.paused?L"Resume":L"Pause",state.paused?L"Продолжить":L"Пауза"),{24,293,260,327},16,foreground);
        p.text(L"F8",{265,295,296,326},13,secondary);
        if(FAILED(render->EndDraw())) render.Reset();
    }
    EndPaint(window,&ps);
}
void FlyoutView::paintManual(HWND window,const UiState& state,const ManualLayout& layout) {
    PAINTSTRUCT ps{};BeginPaint(window,&ps);auto& render=target(window,true);
    if(render) {
        render->BeginDraw();Painter p(render.Get(),write_.Get(),font_);const auto size=render->GetSize();p.surface(size.width,size.height);
        p.text(local(state.language,L"Manual",L"Мануал"),{28,24,size.width-70,62},24,foreground,true);
        if(state.hover==UiAction::Close) p.rounded({size.width-60,16,size.width-16,60},8,0xffffff,0.05f);
        p.line(size.width-44,33,size.width-34,43,secondary,1.4f);p.line(size.width-34,33,size.width-44,43,secondary,1.4f);
        for(const auto& b:layout.blocks) {
            auto text=manualText(write_.Get(),font_,b,layout.width-56,19.5f*layout.scale);p.color(foreground);
            if(text) render->DrawTextLayout({28,b.y},text.Get(),p.brush.Get());
        }
        if(FAILED(render->EndDraw())) render.Reset();
    }
    EndPaint(window,&ps);
}
}
