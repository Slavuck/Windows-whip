#include "flyout.hpp"
#include <algorithm>
#include <cmath>

namespace whip {
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT32 foreground=0xf3f6fa,secondary=0xb4c3cf,muted=0x879da9,accent=0x50b7ed;
const wchar_t* local(Language lang,const wchar_t* en,const wchar_t* ru) {return lang==Language::Russian?ru:en;}
bool inside(float x,float y,D2D1_RECT_F r) {return x>=r.left && x<=r.right && y>=r.top && y<=r.bottom;}
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
        target->Clear(D2D1::ColorF(0x182c3a));
        const D2D1_GRADIENT_STOP stops[]={{0,D2D1::ColorF(0x1d394b)},{1,D2D1::ColorF(0x192a36)}};
        ComPtr<ID2D1GradientStopCollection> collection;ComPtr<ID2D1LinearGradientBrush> gradient;
        target->CreateGradientStopCollection(stops,2,&collection);target->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({0,0},{width,height}),collection.Get(),&gradient);
        if(gradient) target->FillRectangle({0,0,width,height},gradient.Get());
        color(0x7d929d,0.35f);target->DrawRoundedRectangle(D2D1::RoundedRect({0.6f,0.6f,width-0.6f,height-0.6f},13,13),brush.Get(),1);
        line(15,1.5f,width-15,1.5f,0x425966,0.7f);
    }
    void targetIcon(float x,float y,UINT32 c) {
        ring(x,y,9,c);ring(x,y,4.5f,c,1.4f);color(c);target->FillEllipse(D2D1::Ellipse({x,y},1.6f,1.6f),brush.Get());
        line(x-12,y,x-7,y,c);line(x+7,y,x+12,y,c);line(x,y-12,x,y-7,c);line(x,y+7,x,y+12,c);
    }
    void book(float x,float y,UINT32 c) {
        line(x,y-9,x,y+10,c,1.4f);line(x-11,y-8,x,y-5,c,1.4f);line(x,y-5,x+11,y-8,c,1.4f);
        line(x-11,y-8,x-11,y+8,c,1.4f);line(x+11,y-8,x+11,y+8,c,1.4f);line(x-11,y+8,x,y+11,c,1.4f);line(x,y+11,x+11,y+8,c,1.4f);
    }
};
std::vector<std::wstring> paragraphs(std::wstring text) {
    text.erase(std::remove(text.begin(),text.end(),L'\r'),text.end());
    std::vector<std::wstring> result;
    for(std::size_t p=0;p<text.size();) {const auto end=text.find(L"\n\n",p);auto item=text.substr(p,end==std::wstring::npos?end:end-p);while(!item.empty() && item.back()==L'\n') item.pop_back();if(!item.empty()) result.push_back(std::move(item));if(end==std::wstring::npos) break;p=end+2;}
    return result;
}
std::wstring afterDash(const std::wstring& text) {const auto dash=text.find(L" — ");return dash==std::wstring::npos?text:text.substr(dash+3);}
}
bool FlyoutView::initialize(ID2D1Factory* factory,IDWriteFactory* write) {
    factory_=factory;write_=write;if(!factory_ || !write_) return false;
    ComPtr<IDWriteFontCollection> fonts;if(SUCCEEDED(write_->GetSystemFontCollection(&fonts))) {UINT32 index{};BOOL exists{};fonts->FindFamilyName(L"Segoe UI Variable Text",&index,&exists);if(exists) font_=L"Segoe UI Variable Text";}
    return true;
}
ComPtr<IDWriteTextFormat> FlyoutView::format(float size,bool strong) const {
    ComPtr<IDWriteTextFormat> result;write_->CreateTextFormat(font_.c_str(),nullptr,strong?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"en-us",&result);
    result->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);result->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,size*1.48f,size*1.12f);return result;
}
float FlyoutView::textHeight(const std::wstring& text,float width,float size,bool strong) const {
    auto f=format(size,strong);ComPtr<IDWriteTextLayout> layout;
    if(FAILED(write_->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),f.Get(),width,10000,&layout))) return 0;
    DWRITE_TEXT_METRICS metrics{};layout->GetMetrics(&metrics);return std::ceil(metrics.height);
}
ManualLayout FlyoutView::layoutManual(const std::wstring& text,float width,float scale) const {
    ManualLayout result;result.width=width;result.scale=scale;
    float y=88;const float body=width-56;
    const auto content=paragraphs(text);
    for(std::size_t i=0;i<content.size();++i) {
        ManualBlock b;b.text=content[i];b.y=y;
        if(i==0) {b.kind=ManualBlock::Kind::Lead;b.textHeight=textHeight(b.text,body,21*scale,true);b.height=b.textHeight+20*scale;}
        else if(i==2 || i==3) {
            b.kind=ManualBlock::Kind::Mode;b.title=i==2?L"Exact":L"Sadomaso";b.text=afterDash(b.text);
            b.textHeight=textHeight(b.text,body-36,15*scale);b.height=b.textHeight+54*scale;
        } else if(i==4 || i==5) {
            b.kind=ManualBlock::Kind::Key;b.title=i==4?L"F8":L"Esc";b.text=afterDash(b.text);
            b.textHeight=textHeight(b.text,body-64,14.5f*scale);b.height=std::max(32.0f*scale,b.textHeight)+16*scale;
        } else if(i>=6) {b.kind=ManualBlock::Kind::Note;b.textHeight=textHeight(b.text,body,12.8f*scale);b.height=b.textHeight+9*scale;}
        else {b.kind=ManualBlock::Kind::Body;b.textHeight=textHeight(b.text,body,15.5f*scale);b.height=b.textHeight+22*scale;}
        result.blocks.push_back(b);y+=b.height;
        if(i==3) y+=16*scale;
        if(i==5) y+=14*scale;
    }
    result.height=std::ceil(y+24);return result;
}
ManualLayout FlyoutView::fitManual(const std::wstring& text,float availableWidth,float availableHeight) const {
    const float maxHeight=std::max(240.0f,availableHeight-32),maxWidth=std::max(260.0f,std::min(availableWidth-32,maxHeight-24));
    float width=std::min(460.0f,maxWidth);
    for(float scale=1.0f;scale>=0.55f;scale-=0.025f) {
        auto layout=layoutManual(text,width,scale);
        if(layout.height<=maxHeight && layout.height>width) return layout;
        if(scale<0.90f) width=std::min(maxWidth,width+12);
    }
    return layoutManual(text,width,0.55f);
}
UiAction FlyoutView::panelHit(float x,float y) {
    if(inside(x,y,{288,24,332,68})) return UiAction::Training;
    if(inside(x,y,{24,112,336,184})) return UiAction::Mode;
    if(inside(x,y,{180,228,256,268})) return UiAction::English;
    if(inside(x,y,{256,228,332,268})) return UiAction::Russian;
    if(inside(x,y,{20,300,340,372})) return UiAction::Manual;
    if(inside(x,y,{20,393,340,435})) return UiAction::Pause;
    return UiAction::None;
}
UiAction FlyoutView::manualHit(float x,float y,float width) {return inside(x,y,{width-60,20,width-20,60})?UiAction::Close:UiAction::None;}
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
        p.text(L"Windows Whip",{28,24,280,59},23,foreground,true);
        p.rounded({28,70,34,76},3,state.paused?muted:0x56d58c);
        p.text(local(state.language,state.paused?L"Taking a break":L"Ready to strike",state.paused?L"На перерыве":L"Готов к удару"),{42,61,280,86},12.5f,secondary);
        if(state.hover==UiAction::Training || state.focus==UiAction::Training) p.rounded({288,24,332,68},9,0xffffff,0.07f);
        p.targetIcon(310,46,secondary);p.line(24,96,336,96,0x314a59);
        p.text(local(state.language,L"Whip mode",L"Режим хлыста"),{28,112,330,139},13,secondary);
        if(state.hover==UiAction::Mode || state.focus==UiAction::Mode) p.rounded({20,139,340,187},9,0xffffff,0.025f);
        p.text(local(state.language,L"Exact",L"Точный"),{28,145,128,182},17,state.mode==Mode::Exact?foreground:secondary,state.mode==Mode::Exact);
        p.text(local(state.language,L"Sadomaso",L"Садомазо"),{228,145,340,182},17,state.mode==Mode::Sadomaso?foreground:secondary,state.mode==Mode::Sadomaso);
        p.rounded({140,144,220,184},20,0x37b34c);p.color(0xffffff);render->FillEllipse(D2D1::Ellipse({static_cast<float>(160+40*state.modePosition),164},16,16),p.brush.Get());
        p.line(24,207,336,207,0x314a59);
        p.text(local(state.language,L"Language",L"Язык"),{28,232,170,267},17,foreground);
        p.rounded({180,228,332,268},20,0x101f2b,0.75f);p.color(0x536d7d);render->DrawRoundedRectangle(D2D1::RoundedRect({180,228,332,268},20,20),p.brush.Get(),1);
        const float selected=state.language==Language::English?180.0f:256.0f;p.rounded({selected,228,selected+76,268},20,0x376b90);
        p.text(L"EN",{180,228,256,268},15,state.language==Language::English?foreground:secondary,true,true);p.text(L"RU",{256,228,332,268},15,state.language==Language::Russian?foreground:secondary,true,true);
        const bool manualHover=state.hover==UiAction::Manual || state.focus==UiAction::Manual;
        p.rounded({20,300,340,372},10,0xffffff,manualHover?0.075f:0.035f);p.book(48,335,secondary);
        p.text(local(state.language,L"Manual",L"Руководство"),{78,311,300,338},17,foreground,true);
        p.text(local(state.language,L"A little guidance",L"Немного наставлений"),{78,339,300,361},12,secondary);
        p.line(315,329,321,335,secondary,1.5f);p.line(321,335,315,341,secondary,1.5f);
        p.line(0,389,size.width,389,0x314a59);
        if(state.hover==UiAction::Pause || state.focus==UiAction::Pause) p.rounded({12,396,348,436},8,0xffffff,0.045f);
        if(state.paused) {p.line(34,405,45,414,secondary,1.8f);p.line(45,414,34,423,secondary,1.8f);p.line(34,405,34,423,secondary,1.8f);}
        else {p.line(35,406,35,422,secondary,2);p.line(43,406,43,422,secondary,2);}
        p.text(local(state.language,state.paused?L"Resume":L"Pause",state.paused?L"Продолжить":L"Передохнуть"),{63,401,280,432},15,foreground);
        p.rounded({292,402,331,428},6,0xffffff,0.055f);p.text(L"F8",{292,402,331,428},12,secondary,true,true);
        if(FAILED(render->EndDraw())) render.Reset();
    }
    EndPaint(window,&ps);
}
void FlyoutView::paintManual(HWND window,const UiState& state,const ManualLayout& layout) {
    PAINTSTRUCT ps{};BeginPaint(window,&ps);auto& render=target(window,true);
    if(render) {
        render->BeginDraw();Painter p(render.Get(),write_.Get(),font_);const auto size=render->GetSize();p.surface(size.width,size.height);
        p.book(40,44,secondary);p.text(local(state.language,L"Manual",L"Руководство"),{66,25,size.width-70,62},24,foreground,true);
        if(state.hover==UiAction::Close || state.focus==UiAction::Close) p.rounded({size.width-60,20,size.width-20,60},8,0xffffff,0.07f);
        p.line(size.width-45,35,size.width-35,45,secondary,1.4f);p.line(size.width-35,35,size.width-45,45,secondary,1.4f);
        const float scale=layout.scale;
        for(const auto& b:layout.blocks) {
            const float bottom=b.y+b.textHeight+5;
            switch(b.kind) {
            case ManualBlock::Kind::Lead:p.text(b.text,{28,b.y,layout.width-28,bottom},21*scale,foreground,true);break;
            case ManualBlock::Kind::Body:p.text(b.text,{28,b.y,layout.width-28,bottom},15.5f*scale,secondary);break;
            case ManualBlock::Kind::Mode:{
                p.rounded({24,b.y,layout.width-24,b.y+b.height-10*scale},9,0xffffff,0.035f);
                p.rounded({24,b.y+15*scale,26,b.y+b.height-25*scale},1,accent);
                p.text(b.title,{42,b.y+12*scale,layout.width-42,b.y+38*scale},16*scale,foreground,true);
                const float top=b.y+38*scale;p.text(b.text,{42,top,layout.width-42,top+b.textHeight+4},15*scale,secondary);break;
            }
            case ManualBlock::Kind::Key:p.rounded({28,b.y+2,73,b.y+29*scale},5,0xffffff,0.065f);p.text(b.title,{28,b.y+2,73,b.y+29*scale},12.5f*scale,foreground,true,true);p.text(b.text,{92,b.y,layout.width-28,bottom},14.5f*scale,secondary);break;
            case ManualBlock::Kind::Note:p.text(b.text,{28,b.y,layout.width-28,bottom},12.8f*scale,muted);break;
            }
        }
        if(FAILED(render->EndDraw())) render.Reset();
    }
    EndPaint(window,&ps);
}
}
