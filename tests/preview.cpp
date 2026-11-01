#include <windows.h>
#include <string>
#include <filesystem>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
    const auto executable=std::filesystem::path(path).parent_path()/L"Whip.exe";
    std::wstring command=L"\""+executable.wstring()+L"\" --preview";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&process)) return 1;
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);CloseHandle(process.hProcess);return 0;
}
