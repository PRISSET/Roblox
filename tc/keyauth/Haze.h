/*
 * Copyright (c) 2024 HAZE Software Protection. All rights reserved.
 *
 * This code is proprietary and confidential. Unauthorized copying, use,
 * distribution, or modification of this code, via any medium, is strictly prohibited.
*/

#ifndef HAZE_PROTECT
#define HAZE_PROTECT

#include <windows.h>
#include <string>
#include <vector> 
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "wintrust.lib")

#define HAZE_Export extern __declspec(noinline)

#define Hide_Function() try { int* __INVALID_ALLOC__ = new int[(std::size_t)std::numeric_limits<std::size_t>::max]; } catch ( const std::bad_alloc& except ) { 
#define Hide_Function_End() } 

typedef void(*HAZEFunction)();

extern "C"
{
	HAZE_Export void HAZE_Protect(std::string access_key, std::string discord_webhook, bool selfdelete, bool imgui, bool allowban, bool bsod, bool check_process_integrity, bool troll, bool Process_Injection, HWND imgui_handle);
	HAZE_Export void HAZE_UnBan();
	HAZE_Export void HAZE_Check();
	HAZE_Export void HAZE_Print(const std::string text);
	HAZE_Export void HAZE_Sleep(int milliseconds);
	HAZE_Export void HAZE_Thread(HAZEFunction func);
	HAZE_Export void HAZE_Exit();
	HAZE_Export void HAZE_Protect_Threads();
	HAZE_Export void HAZE_WriteToDisk(const std::vector<uint8_t>& peBytes, const std::wstring& filePath);
	HAZE_Export std::string HAZE_Cin();
}
#endif
