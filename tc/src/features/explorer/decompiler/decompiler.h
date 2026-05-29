#pragma once
#include <cstdint>
#include <string>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace decompiler
{
    enum script_type
    {
        LocalScript = 0,
        ModuleScript = 1
    };

    class decompiler_t final
    {
    public:
        void decompile_script(std::uint64_t script, script_type type);
        void disassemble_script(std::uint64_t script, script_type type);
    private:
        const char* decompile_endpoint = "http://api.plusgiant5.com/konstant/decompile";
        const char* disassemble_endpoint = "http://api.plusgiant5.com/konstant/disassemble";
        std::string call_api(const char* endpoint, const void* data, std::uint64_t size);
        std::string decompress_script(std::uint64_t script, script_type type);
    };
}