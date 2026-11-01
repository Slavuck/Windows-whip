#include "platform.hpp"
#include <shlobj.h>
#include <array>
#include <algorithm>
#include <system_error>

namespace whip {
void Settings::load(const std::filesystem::path& executable,const std::filesystem::path& localData) {
    std::error_code error;
    const auto adjacent=executable.parent_path()/L"Whip.ini";
    portable=std::filesystem::exists(adjacent,error);
    path=portable?adjacent:localData/L"WindowsWhip"/L"Whip.ini";
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Whip",L"Mode",L"Sadomaso",value,64,path.c_str());
    mode=_wcsicmp(value,L"Exact")==0?Mode::Exact:Mode::Sadomaso;
    GetPrivateProfileStringW(L"Whip",L"Language",L"EN",value,64,path.c_str());
    language=_wcsicmp(value,L"RU")==0?Language::Russian:Language::English;
}
bool Settings::save() {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(),error);
    if(error) {writeFailed=true;return false;}
    const wchar_t* modeName=mode==Mode::Exact?L"Exact":L"Sadomaso";
    const wchar_t* languageName=language==Language::Russian?L"RU":L"EN";
    writeFailed=!(WritePrivateProfileStringW(L"Whip",L"Mode",modeName,path.c_str()) && WritePrivateProfileStringW(L"Whip",L"Language",languageName,path.c_str()));
    return !writeFailed;
}
std::wstring executablePath() {
    std::wstring path(32768,L'\0');const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    path.resize(size);return path;
}
std::wstring localDataPath() {
    PWSTR path=nullptr;std::wstring result;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path))) {result=path;CoTaskMemFree(path);}
    return result;
}
std::wstring resourceText(int id) {
    const auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource) return {};
    const auto data=static_cast<const char*>(LockResource(LoadResource(nullptr,resource)));
    const auto size=static_cast<int>(SizeofResource(nullptr,resource));if(!data || !size) return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,data,size,nullptr,0);if(!count) return {};
    std::wstring text(static_cast<std::size_t>(count),L'\0');MultiByteToWideChar(CP_UTF8,0,data,size,text.data(),count);return text;
}
double clockSeconds() {
    static const double frequency=[]{LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return static_cast<double>(f.QuadPart);}();
    LARGE_INTEGER counter{};QueryPerformanceCounter(&counter);return static_cast<double>(counter.QuadPart)/frequency;
}
bool sendClick(Vec2 target,Vec2 restore,Rect desktop) {
    const auto point=normalizePoint(target,desktop),back=normalizePoint(restore,desktop);
    std::array<INPUT,4> inputs{};
    for(auto& input:inputs) {input.type=INPUT_MOUSE;input.mi.dwExtraInfo=inputMarker;}
    inputs[0].mi={point.x,point.y,0,MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK,0,inputMarker};
    inputs[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;inputs[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    inputs[3].mi={back.x,back.y,0,MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK,0,inputMarker};
    const auto sent=SendInput(static_cast<UINT>(inputs.size()),inputs.data(),sizeof(INPUT));
    if(sent!=inputs.size()) {
        // A partial batch must not leave a button held down.
        INPUT release{};release.type=INPUT_MOUSE;release.mi.dwFlags=MOUSEEVENTF_LEFTUP;release.mi.dwExtraInfo=inputMarker;
        SendInput(1,&release,sizeof(INPUT));return false;
    }
    return true;
}
}
