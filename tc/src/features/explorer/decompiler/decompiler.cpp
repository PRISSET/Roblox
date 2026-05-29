#include "decompiler.h"
#include "../bytecode/bytecode.hpp"
#include <fstream>
#include <sstream>
#include "../../../memory/memory.h"
#include <sdk/offsets.h>
#include <iostream>
#include <features/explorer/globals.h>

void decompiler::decompiler_t::decompile_script(std::uint64_t script, script_type type)
{
	std::string bytecode = this->decompress_script(script, type);
	std::string decompiled = this->call_api(this->decompile_endpoint, bytecode.data(), bytecode.size());

	std::istringstream stream(decompiled);
	std::string line;
	std::string processed;
	
	for (int i = 0; i < 6; i++) {
		processed += "- cigar\n";
	}
	
	processed += "\n";
	int line_count = 0;
	while (std::getline(stream, line)) {
		line_count++;
		if (line_count > 6) {
			processed += line;
			processed += "\n";
		}
	}

	globals::decompiled_script_t new_script;
	new_script.title = "Script_" + std::to_string(script);
	new_script.code = processed;
	new_script.editor.SetText(processed);
	new_script.open = true;

	globals::decompiled_scripts.push_back(new_script);
}

void decompiler::decompiler_t::disassemble_script(std::uint64_t script, script_type type)
{
	std::string bytecode = this->decompress_script(script, type);
	std::cout << "%s" << this->call_api(this->disassemble_endpoint, bytecode.data(), bytecode.size()).c_str();
}

std::string decompiler::decompiler_t::decompress_script(std::uint64_t script, script_type type)
{
	std::uint64_t ptr{ 0 };
	std::uint64_t bytecode_ptr{ 0 };
	std::uint64_t bytecode_size{ 0 };

	if (type == LocalScript) {
		ptr = memory->read<std::uint64_t>(script + Offsets::LocalScript::ByteCode);
		bytecode_ptr = memory->read<std::uint64_t>(ptr + Offsets::ByteCode::Pointer);
		bytecode_size = memory->read<std::uint64_t>(ptr + Offsets::ByteCode::Size);
	}
	else {
		ptr = memory->read<std::uint64_t>(script + Offsets::ModuleScript::ByteCode);
		bytecode_ptr = memory->read<std::uint64_t>(ptr + Offsets::ByteCode::Pointer);
		bytecode_size = memory->read<std::uint64_t>(ptr + Offsets::ByteCode::Size);
	}

	if (ptr == 0 || bytecode_ptr == 0 || bytecode_size == 0) {
		return "";
	}

	std::vector<char> buf(bytecode_size);
	Luck_ReadVirtualMemory(
		memory->get_process_handle(),
		reinterpret_cast<void*>(bytecode_ptr),
		buf.data(),
		buf.size(),
		nullptr
	);

	return Bytecode::decompress(std::string(buf.data(), bytecode_size));
}

std::string decompiler::decompiler_t::call_api(const char* endpoint, const void* data, std::uint64_t size)
{
	URL_COMPONENTS uc{};
	uc.dwStructSize = sizeof(uc);

	wchar_t host[256];
	wchar_t path[1024];

	uc.lpszHostName = host;
	uc.dwHostNameLength = _countof(host);
	uc.lpszUrlPath = path;
	uc.dwUrlPathLength = _countof(path);

	std::wstring wendpoint = std::wstring(endpoint, endpoint + strlen(endpoint));

	if (!WinHttpCrackUrl(wendpoint.c_str(), 0, 0, &uc))
		return {};

	HINTERNET hSession = WinHttpOpen(L"KonstantClient/1.0",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		NULL, NULL, 0);
	if (!hSession) return {};

	HINTERNET hConnect = WinHttpConnect(hSession,
		std::wstring(uc.lpszHostName, uc.dwHostNameLength).c_str(),
		uc.nPort,
		0);
	if (!hConnect) {
		WinHttpCloseHandle(hSession);
		return {};
	}

	HINTERNET hRequest = WinHttpOpenRequest(
		hConnect,
		L"POST",
		std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength).c_str(),
		NULL,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0
	);

	if (!hRequest) {
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return {};
	}

	BOOL sent = WinHttpSendRequest(
		hRequest,
		L"Content-Type: text/plain\r\n",
		-1,
		(LPVOID)data,
		(DWORD)size,
		(DWORD)size,
		0
	);

	if (!sent) {
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return {};
	}

	if (!WinHttpReceiveResponse(hRequest, NULL)) {
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return {};
	}

	std::string result;
	DWORD bytesAvailable = 0;

	do {
		if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable))
			break;
		if (bytesAvailable == 0)
			break;

		std::string chunk;
		chunk.resize(bytesAvailable);

		DWORD read = 0;
		if (!WinHttpReadData(hRequest, chunk.data(), bytesAvailable, &read))
			break;

		result.append(chunk.data(), read);

	} while (bytesAvailable > 0);

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	return result;
}
