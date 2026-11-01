#include "flyout.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
static std::wstring readText(const char* file) {
    std::ifstream input(file,std::ios::binary);std::string data((std::istreambuf_iterator<char>(input)),{});
    const int size=MultiByteToWideChar(CP_UTF8,0,data.data(),static_cast<int>(data.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(size),L'\0');MultiByteToWideChar(CP_UTF8,0,data.data(),static_cast<int>(data.size()),result.data(),size);return result;
}
int main() {
    using namespace whip;using Microsoft::WRL::ComPtr;
    ComPtr<ID2D1Factory> factory;ComPtr<IDWriteFactory> write;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf());DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(write.GetAddressOf()));
    try {
        FlyoutView view;require(view.initialize(factory.Get(),write.Get()),"DirectWrite initialization");
        require(FlyoutView::panelHeight>FlyoutView::panelWidth,"settings flyout is portrait");
        require(FlyoutView::panelHit(245,14)==UiAction::Training && FlyoutView::panelHit(306,74)==UiAction::Training,"training button owns the enlarged right header area");
        require(FlyoutView::panelHit(80,116)==UiAction::ExactMode && FlyoutView::panelHit(240,116)==UiAction::SadomasoMode,"mode labels select their own capsule half");
        require(FlyoutView::panelHit(200,178)==UiAction::English && FlyoutView::panelHit(264,178)==UiAction::Russian,"language selector hit areas");
        require(FlyoutView::panelHit(150,240)==UiAction::Manual && FlyoutView::panelHit(150,305)==UiAction::Pause,"manual and pause rows");
        for(const auto* name:{"test_manual_en.txt","test_manual_ru.txt"}) {
            const auto text=readText(name);require(text.size()>400,"full author-provided Manual loaded");
            for(float height:{1080.0f,900.0f,720.0f,600.0f}) {
                const auto layout=view.fitManual(text,1000,height);
                require(layout.height<=height-32,"all Manual content fits without scrolling");
                require(layout.height>layout.width,"Manual remains portrait");
                require(layout.blocks.size()==8,"all eight author paragraphs are visible");
                for(std::size_t i=0;i<layout.blocks.size();++i) require(layout.blocks[i].kind==(i>=6?ManualBlock::Kind::Note:ManualBlock::Kind::Body),"uniform body with bold notes only");
                for(const auto& block:layout.blocks) require(block.y+block.height<=layout.height,"no Manual block is clipped");
            }
        }
        std::cout<<"Portrait EN/RU layouts fit at 600, 720, 900 and 1080 DIP; hit targets passed.\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
