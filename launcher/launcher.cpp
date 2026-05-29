#include <Windows.h>
#include <urlmon.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <iostream>
#include <cstdio>

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")

// ============================================================
// Turtle.Club Launcher
// Downloads tc1.exe + runtime DLLs from GitHub, then launches.
// ============================================================

static const char* GITHUB_RAW_BASE = "https://raw.githubusercontent.com/PRISSET/Roblox/main/bin/";

struct download_entry {
    const char* filename;
    bool required; // if false, failure is non-fatal
};

static const std::vector<download_entry> FILES_TO_DOWNLOAD = {
    { "tc1.exe",          true  },
    { "libcurl.dll",      true  },
    { "freetype.dll",     true  },
    { "libpng16.dll",     true  },
    { "zlib1.dll",        true  },
    { "zstd.dll",         true  },
    { "xxhash.dll",       true  },
    { "brotlicommon.dll", true  },
    { "brotlidec.dll",    true  },
    { "bz2.dll",          true  },
};

// Progress callback for URLDownloadToFile
class DownloadProgress : public IBindStatusCallback {
public:
    std::string current_file;

    HRESULT STDMETHODCALLTYPE OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG, LPCWSTR) override {
        if (ulProgressMax > 0) {
            int pct = (int)((float)ulProgress / (float)ulProgressMax * 100.0f);
            printf("\r  [%-20.*s] %3d%%  %s", pct / 5, "====================", pct, current_file.c_str());
            fflush(stdout);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IBindStatusCallback || riid == IID_IUnknown) { *ppv = this; return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*, BINDINFO*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }
};

static std::filesystem::path get_install_dir()
{
    char exe_path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    return std::filesystem::path(exe_path).parent_path() / "TurtleClub";
}

static bool download_file(const std::string& url, const std::string& dest, DownloadProgress& cb)
{
    std::wstring wurl(url.begin(), url.end());
    std::wstring wdest(dest.begin(), dest.end());
    HRESULT hr = URLDownloadToFileW(nullptr, wurl.c_str(), wdest.c_str(), 0, &cb);
    return SUCCEEDED(hr);
}

int main()
{
    SetConsoleTitleA("Turtle.Club Launcher");
    
    printf("\n");
    printf("  ========================================\n");
    printf("       Turtle.Club Launcher v1.0.0\n");
    printf("  ========================================\n\n");

    auto install_dir = get_install_dir();
    std::error_code ec;
    std::filesystem::create_directories(install_dir, ec);

    printf("  Install directory: %s\n\n", install_dir.string().c_str());

    DownloadProgress progress;
    int downloaded = 0, skipped = 0, failed = 0;

    for (const auto& entry : FILES_TO_DOWNLOAD) {
        std::filesystem::path dest = install_dir / entry.filename;

        // Skip if already exists
        if (std::filesystem::exists(dest, ec)) {
            printf("  [SKIP] %s (already exists)\n", entry.filename);
            skipped++;
            continue;
        }

        std::string url = std::string(GITHUB_RAW_BASE) + entry.filename;
        progress.current_file = entry.filename;

        printf("  Downloading %s...\n", entry.filename);
        bool ok = download_file(url, dest.string(), progress);
        printf("\n");

        if (ok && std::filesystem::exists(dest, ec)) {
            printf("  [OK] %s\n", entry.filename);
            downloaded++;
        } else {
            printf("  [FAIL] %s\n", entry.filename);
            failed++;
            if (entry.required) {
                printf("\n  ERROR: Required file failed to download. Cannot continue.\n");
                printf("  Press any key to exit...\n");
                system("pause >nul");
                return 1;
            }
        }
    }

    printf("\n  ----------------------------------------\n");
    printf("  Downloaded: %d | Skipped: %d | Failed: %d\n", downloaded, skipped, failed);
    printf("  ----------------------------------------\n\n");

    // Launch tc1.exe
    std::filesystem::path target = install_dir / "tc1.exe";
    if (!std::filesystem::exists(target, ec)) {
        printf("  ERROR: tc1.exe not found!\n");
        system("pause >nul");
        return 1;
    }

    printf("  Launching tc1.exe...\n");

    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas";  // request admin
    std::string target_str = target.string();
    std::string dir_str = install_dir.string();
    sei.lpFile = target_str.c_str();
    sei.lpDirectory = dir_str.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExA(&sei)) {
        if (sei.hProcess)
            CloseHandle(sei.hProcess);
        printf("  Started successfully!\n\n");
    } else {
        printf("  ERROR: Failed to launch tc1.exe (error %lu)\n", GetLastError());
        system("pause >nul");
        return 1;
    }

    return 0;
}
