#pragma once
#include "core.hpp"
#include <windows.h>
#include <filesystem>
#include <string>

namespace whip {
struct Settings {
    Mode mode=Mode::Sadomaso;
    Language language=Language::English;
    std::filesystem::path path;
    bool portable{},writeFailed{};
    void load(const std::filesystem::path& executable,const std::filesystem::path& localData);
    bool save();
};
std::wstring executablePath();
std::wstring localDataPath();
std::wstring resourceText(int id);
double clockSeconds();
bool sendClick(Vec2 target,Vec2 restore,Rect desktop);
constexpr ULONG_PTR inputMarker=0x57485039;
}
