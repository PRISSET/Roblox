#pragma once
#define _CRT_SECURE_NO_WARNINGS

#include <string>
#include <cstdarg>
#include <cstdio>
#include <Windows.h>
#include <mutex>
#include <ctime>

namespace logger {

    enum level {
        success,
        info,
        debug,
        warn,
        error
    };
    static std::mutex io_mutex;

    namespace ansi {
        constexpr const char* reset = "\x1b[0m";
        constexpr const char* red = "\x1b[91m";
        constexpr const char* yellow = "\x1b[93m";
        constexpr const char* green = "\x1b[92m";
        constexpr const char* magenta = "\x1b[95m";
        constexpr const char* white = "\x1b[90m";
    }

    inline void enable_ansi_colors() {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE) return;

        DWORD dwMode = 0;
        if (!GetConsoleMode(hOut, &dwMode)) return;

        // Try to enable ANSI colors
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        dwMode |= ENABLE_PROCESSED_OUTPUT;
        dwMode |= ENABLE_WRAP_AT_EOL_OUTPUT;

        if (!SetConsoleMode(hOut, dwMode)) {
            // If ANSI fails, try alternative approach
            dwMode &= ~ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            dwMode |= ENABLE_PROCESSED_OUTPUT;
            SetConsoleMode(hOut, dwMode);
        }
    }


    inline void setup_console_font() {
        CONSOLE_FONT_INFOEX font{};
        font.cbSize = sizeof(font);
        font.dwFontSize.Y = 17;
        font.FontWeight = FW_NORMAL;
        wcscpy_s(font.FaceName, L"Consolas");
        SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), FALSE, &font);
    }

    inline void setup() {
        // Set console title
        SetConsoleTitleA("resource");

        // Enable virtual terminal processing for ANSI colors
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hConsole != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hConsole, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                dwMode |= ENABLE_PROCESSED_OUTPUT;
                dwMode |= ENABLE_WRAP_AT_EOL_OUTPUT;
                SetConsoleMode(hConsole, dwMode);
            }
        }

        setup_console_font();
    }

    inline std::string current_time() {
        std::time_t t = std::time(nullptr);
        std::tm tm;
        localtime_s(&tm, &t);

        char buf[20];
        std::strftime(buf, sizeof(buf), "%m-%d-%Y_%H-%M-%S", &tm);
        return buf;
    }

    template<level T>
    void print(const char* fmt, ...) {
        std::lock_guard<std::mutex> lk(io_mutex);

        const char* ansi_color;
        const char* tag;

        switch (T) {
        case success: ansi_color = "\033[32;1m"; tag = "WELCOME"; break; // Bright Green
        case info:    ansi_color = "\033[37;1m"; tag = "INFO";    break; // Bright White
        case debug:   ansi_color = "\033[37m";   tag = "DEBUG";   break; // White
        case warn:    ansi_color = "\033[33;1m"; tag = "WARN";    break; // Bright Yellow
        case error:   ansi_color = "\033[31;1m"; tag = "ERROR";   break; // Bright Red
        default:      ansi_color = "\033[37m";   tag = "LOG";     break; // White
        }

        const char* reset = "\033[0m";

        char msg[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        std::string time = current_time();

        // Try ANSI colors first
        std::printf(
            "%s[%s] [%s]: %s%s\n",
            ansi_color, time.c_str(), tag, msg, reset
        );

        std::fflush(stdout);
    }
}

#define LOG_SUCCESS(...) logger::print<logger::success>(__VA_ARGS__)
#define LOG_INFO(...)    logger::print<logger::info>(__VA_ARGS__)
#define LOG_DEBUG(...)   logger::print<logger::debug>(__VA_ARGS__)
#define LOG_WARN(...)    logger::print<logger::warn>(__VA_ARGS__)
#define LOG_ERROR(...)   logger::print<logger::error>(__VA_ARGS__)
