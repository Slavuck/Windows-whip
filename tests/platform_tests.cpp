#include "platform.hpp"
#include <filesystem>
#include <iostream>

static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    using namespace whip;
    const auto root=std::filesystem::temp_directory_path()/(L"WindowsWhip-test-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root/L"exe");
    try {
        Settings settings;settings.load(root/L"exe"/L"Whip.exe",root/L"profile");
        require(!settings.portable && settings.mode==Mode::Sadomaso && settings.language==Language::English,"first-run defaults");
        settings.mode=Mode::Exact;settings.language=Language::Russian;require(settings.save(),"save profile");
        Settings restored;restored.load(root/L"exe"/L"Whip.exe",root/L"profile");require(restored.mode==Mode::Exact && restored.language==Language::Russian,"mode and language round trip");
        WritePrivateProfileStringW(L"Whip",L"Mode",L"Sadomaso",(root/L"exe"/L"Whip.ini").c_str());
        restored.load(root/L"exe"/L"Whip.exe",root/L"profile");require(restored.portable && restored.mode==Mode::Sadomaso && restored.language==Language::English,"portable file overrides profile");
        const auto selected=restored.path;
        SetFileAttributesW(selected.c_str(),FILE_ATTRIBUTE_READONLY);restored.mode=Mode::Exact;require(!restored.save() && restored.path==selected,"read-only portable file never falls back");SetFileAttributesW(selected.c_str(),FILE_ATTRIBUTE_NORMAL);
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,selected.c_str());
        std::filesystem::remove_all(root);
        std::cout<<"Settings integration checks passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
