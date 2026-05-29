#include "memory.h"
#include <cstring>
std::uint32_t memory_t::find_process_id(const std::string& process_name)
{
    std::uint64_t local_process_id = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return local_process_id;
    }

    PROCESSENTRY32 process_entry{};
    process_entry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &process_entry))
    {
        do
        {
            if (!_stricmp(process_name.c_str(), process_entry.szExeFile))
            {
                local_process_id = process_entry.th32ProcessID;
                process_id = local_process_id;
                break;
            }
        } while (Process32Next(snapshot, &process_entry));
    }

    CloseHandle(snapshot);
    return local_process_id;
}

std::uint64_t memory_t::find_module_address(const std::string& module_name)
{
    std::uint64_t module_address = 0;

    if (!process_handle)
    {
        return module_address;
    }

    DWORD process_id = GetProcessId(process_handle);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);

    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return module_address;
    }

    MODULEENTRY32 module_entry{};
    module_entry.dwSize = sizeof(MODULEENTRY32);

    if (Module32First(snapshot, &module_entry))
    {
        do
        {
            if (!_stricmp(module_name.c_str(), module_entry.szModule))
            {
                module_address = reinterpret_cast<uint64_t>(module_entry.modBaseAddr);
                base_address = module_address;
                break;
            }
        } while (Module32Next(snapshot, &module_entry));
    }

    CloseHandle(snapshot);
    return module_address;
}

bool memory_t::attach_to_process(const std::string& process_name)
{
    HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, false, find_process_id(process_name));

    if (process == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    process_handle = process;

    return true;
}

std::string memory_t::read_string(std::uint64_t address)
{
    std::int32_t string_length = read<std::int32_t>(address + 0x10);
    std::uint64_t string_address = (string_length >= 16) ? read<std::uint64_t>(address) : address;

    if (string_length == 0 || string_length > 255)
    {
        return "Unknown";
    }

    std::vector<char> buffer(string_length + 1, 0);
    Luck_ReadVirtualMemory(process_handle, reinterpret_cast<void*>(string_address), buffer.data(), buffer.size(), nullptr);

    return std::string(buffer.data(), string_length);
}

void memory_t::write_string(std::uint64_t address, const std::string& value)
{
    std::int32_t new_length = static_cast<std::int32_t>(value.length());
    std::int32_t current_length = read<std::int32_t>(address + 0x10);

    if (new_length >= 16)
    {
        std::uint64_t string_address = 0;
        std::uint64_t current_capacity = read<std::uint64_t>(address + 0x18);

        if (current_length >= 16)
        {
            string_address = read<std::uint64_t>(address);
        }

        if (string_address == 0 || current_capacity < static_cast<std::uint64_t>(new_length))
        {
            string_address = reinterpret_cast<std::uint64_t>(
                VirtualAllocEx(process_handle, nullptr, static_cast<SIZE_T>(new_length + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
            );
            if (string_address == 0)
            {
                return;
            }

            write<std::uint64_t>(address, string_address);
            write<std::uint64_t>(address + 0x18, static_cast<std::uint64_t>(new_length));
        }
        else
        {
            write<std::uint64_t>(address + 0x18, current_capacity);
        }

        write<std::int32_t>(address + 0x10, new_length);
        write_buffer(string_address, value.c_str(), static_cast<std::size_t>(new_length + 1));
        return;
    }

    char inline_buffer[16]{};
    if (new_length > 0)
    {
        std::memcpy(inline_buffer, value.data(), static_cast<std::size_t>(new_length));
    }

    write_buffer(address, inline_buffer, sizeof(inline_buffer));
    write<std::int32_t>(address + 0x10, new_length);
    write<std::uint64_t>(address + 0x18, 15);
}

void memory_t::read_buffer(std::uint64_t address, void* buffer, std::size_t size)
{
    Luck_ReadVirtualMemory(process_handle, reinterpret_cast<void*>(address), buffer, static_cast<ULONG>(size), nullptr);
}

void memory_t::write_buffer(std::uint64_t address, const void* buffer, std::size_t size)
{
    Luck_WriteVirtualMemory(process_handle, reinterpret_cast<void*>(address), const_cast<void*>(buffer), static_cast<ULONG>(size), nullptr);
}

std::uint32_t memory_t::get_process_id()
{
    return process_id;
}

std::uint64_t memory_t::get_module_address()
{
    return base_address;
}

HANDLE memory_t::get_process_handle()
{
    return process_handle;
}
