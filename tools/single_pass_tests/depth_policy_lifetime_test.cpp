#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>

int wmain(int argc,wchar_t** argv){
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    if(argc!=5 || !SetEnvironmentVariableW(L"CVR_STEREO_REFLECTION_DXC",argv[2]))return 1;
    const bool exit=std::wstring(argv[4])==L"exit";
    if(!exit && std::wstring(argv[4])!=L"unload")return 2;
    std::ifstream file(std::filesystem::path(argv[3]),std::ios::binary);
    if(!file)return 3;
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};
    if(bytes.empty())return 4;
    for(unsigned generation=0;generation<(exit?1u:3u);++generation){
        const auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module)return 5;
        const auto read=reinterpret_cast<bool(*)(const void*,size_t)>(GetProcAddress(module,"DepthPolicyReadShader"));
        if(!read)return 6;
        for(unsigned i=0;i<16;++i)if(!read(bytes.data(),bytes.size()))return 7;
        if(!GetModuleHandleW(argv[2]))return 8;
        if(exit){
            std::puts("Checking process-detach after reflection in a dynamically loaded owner");
            std::fflush(stdout);ExitProcess(0);
        }
        if(!FreeLibrary(module))return 9;
    }
    std::puts("PASS reflection owner unload/reload without static COM releases");return 0;
}
