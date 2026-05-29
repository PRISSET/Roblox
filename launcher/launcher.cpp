#include <Windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <urlmon.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cmath>
#include <ctime>
#include <cstdio>

#include "../tc/ext/imgui/imgui.h"
#include "../tc/ext/imgui/backends/imgui_impl_dx11.h"
#include "../tc/ext/imgui/backends/imgui_impl_win32.h"
#include "../tc/ext/font/verdana_bold.h"
#include "../tc/ext/font/tahoma_bold.h"
#include "../tc/src/features/installer/roblox_logo.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../tc/ext/imgui/stb_image.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "Advapi32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static const char* GITHUB_RAW = "https://raw.githubusercontent.com/PRISSET/Roblox/main/bin/";
static const char* kProductName = "Turtle.Club Loader";

static constexpr int kInstallW = 520;
static constexpr int kInstallH = 540;
static constexpr int kMenuW   = 720;
static constexpr int kMenuH   = 540;
static constexpr float kTitleBarH = 32.0f;

// Reference accent (CS2 loader) - lime/olive green used for product name + active status line
static const ImVec4 kAccentLime = ImVec4(0.64f, 0.83f, 0.24f, 1.0f);

enum class dep_kind {
    installer,
    raw_file,
    raw_group,
};

enum class check_kind {
    file,
    load_library,
    registry_dword,
    dotnet_runtime,
};

struct dependency_t {
    std::string name;
    std::string download_url;
    std::string install_args;

    dep_kind    kind = dep_kind::installer;
    std::string dest_path;

    check_kind  check = check_kind::file;
    std::string check_path;
    std::string reg_subkey;
    std::string reg_value;
    std::string version_prefix;

    std::vector<std::string> group_files;

    bool installed = false;
    bool downloading = false;
    bool installing = false;
    float progress = 0.0f;
    int  group_done = 0;
    int  group_total = 0;
    bool failed = false;
    std::string status;
};

enum class app_view { install, menu };

static HWND g_hwnd = nullptr;
static WNDCLASSEX g_wc = {};
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static IDXGISwapChain* g_swapchain = nullptr;
static ImFont* g_font_regular = nullptr;
static ImFont* g_font_title = nullptr;
static ID3D11ShaderResourceView* g_logo_texture = nullptr;
static app_view g_window_sized_for = app_view::install;

static std::vector<dependency_t> g_deps;
static std::atomic<bool> g_installing{false};
static std::atomic<bool> g_done{false};
static std::atomic<bool> g_should_close{false};
static std::mutex g_mutex;

static std::atomic<app_view> g_view{app_view::install};

static std::vector<std::string> g_log;
static std::mutex g_log_mutex;
static bool g_log_scroll_pending = false;

static std::vector<std::pair<std::string,bool>> g_menu_status;
static std::string g_product_updated;

static bool g_dragging = false;
static POINT g_drag_offset = {};

static std::filesystem::path get_install_dir() {
    char p[MAX_PATH] = {}; GetModuleFileNameA(nullptr, p, MAX_PATH);
    return std::filesystem::path(p).parent_path() / "TurtleClub";
}

static std::string resolve_in_install_dir(const std::string& rel) {
    return (get_install_dir() / rel).string();
}

static std::filesystem::path get_marker_path() {
    return get_install_dir() / "installed.flag";
}

static void write_install_marker() {
    std::error_code ec;
    std::filesystem::create_directories(get_install_dir(), ec);
    auto path = get_marker_path();
    FILE* f = nullptr; fopen_s(&f, path.string().c_str(), "w");
    if (f) { fputs("ok", f); fclose(f); }
}

static bool has_install_marker() {
    std::error_code ec;
    return std::filesystem::exists(get_marker_path(), ec);
}

static bool is_roblox_running() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "RobloxPlayerBeta.exe") == 0) { found = true; break; }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

static void add_log(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_log_mutex);
    g_log.push_back(s);
    g_log_scroll_pending = true;
}

class DownloadProgress : public IBindStatusCallback {
public:
    float* pp = nullptr;
    DownloadProgress(float* p) : pp(p) {}
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG d, ULONG t, ULONG, LPCWSTR) override {
        if (t > 0 && pp) *pp = (float)d / (float)t;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** p) override {
        if (r == IID_IBindStatusCallback || r == IID_IUnknown) { *p = this; return S_OK; }
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

static bool download_file(const std::string& url, const std::string& dest, float& progress) {
    progress = 0.0f;
    DownloadProgress cb(&progress);
    std::wstring wurl(url.begin(), url.end());
    std::wstring wdest(dest.begin(), dest.end());
    HRESULT hr = URLDownloadToFileW(nullptr, wurl.c_str(), wdest.c_str(), 0, &cb);
    progress = 1.0f;
    return SUCCEEDED(hr);
}

static void init_dependencies() {
    g_deps.clear();
    dependency_t d;

    d = {};
    d.name = "VC++ Redist 2015-2022 (x64)";
    d.kind = dep_kind::installer;
    d.download_url = "https://aka.ms/vs/17/release/vc_redist.x64.exe";
    d.install_args = "/install /quiet /norestart";
    d.check = check_kind::registry_dword;
    d.reg_subkey = "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64";
    d.reg_value  = "Installed";
    g_deps.push_back(d);

    d = {};
    d.name = "VC++ Redist 2015-2022 (x86)";
    d.kind = dep_kind::installer;
    d.download_url = "https://aka.ms/vs/17/release/vc_redist.x86.exe";
    d.install_args = "/install /quiet /norestart";
    d.check = check_kind::registry_dword;
    d.reg_subkey = "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x86";
    d.reg_value  = "Installed";
    d.version_prefix = "x86";
    g_deps.push_back(d);

    d = {};
    d.name = "DirectX End-User Runtime";
    d.kind = dep_kind::installer;
    d.download_url = "https://download.microsoft.com/download/1/7/1/1718CCC4-6315-4D8E-9543-8E28A4E18C4C/dxwebsetup.exe";
    d.install_args = "/Q";
    d.check = check_kind::load_library;
    d.check_path = "d3dx9_43.dll";
    g_deps.push_back(d);

    d = {};
    d.name = ".NET Desktop Runtime 8.0";
    d.kind = dep_kind::installer;
    d.download_url = "https://aka.ms/dotnet/8.0/windowsdesktop-runtime-win-x64.exe";
    d.install_args = "/install /quiet /norestart";
    d.check = check_kind::dotnet_runtime;
    d.check_path = "C:\\Program Files\\dotnet\\shared\\Microsoft.WindowsDesktop.App";
    d.version_prefix = "8.";
    g_deps.push_back(d);

    d = {};
    d.name = "Loader Components";
    d.kind = dep_kind::raw_group;
    d.group_files = {
        "tc1.exe",
        "libcurl.dll",
        "freetype.dll",
        "libpng16.dll",
        "zlib1.dll",
        "zstd.dll",
        "xxhash.dll",
        "brotlicommon.dll",
        "brotlidec.dll",
        "bz2.dll",
    };
    d.group_total = (int)d.group_files.size();
    g_deps.push_back(d);
}

static bool is_dependency_installed(const dependency_t& dep) {
    if (dep.kind == dep_kind::raw_group) {
        for (const auto& f : dep.group_files) {
            if (!std::filesystem::exists(resolve_in_install_dir(f))) return false;
        }
        return !dep.group_files.empty();
    }
    switch (dep.check) {
    case check_kind::file:
        return std::filesystem::exists(dep.check_path);

    case check_kind::load_library: {
        HMODULE m = GetModuleHandleA(dep.check_path.c_str());
        if (m) return true;
        m = LoadLibraryExA(dep.check_path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!m) m = LoadLibraryA(dep.check_path.c_str());
        if (m) { FreeLibrary(m); return true; }
        return false;
    }

    case check_kind::registry_dword: {
        REGSAM views[2];
        int view_count;
        if (dep.version_prefix == "x86") {
            views[0] = KEY_READ | KEY_WOW64_32KEY;
            views[1] = KEY_READ | KEY_WOW64_64KEY;
            view_count = 2;
        } else {
            views[0] = KEY_READ | KEY_WOW64_64KEY;
            view_count = 1;
        }
        for (int i = 0; i < view_count; i++) {
            HKEY h = nullptr;
            LONG rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, dep.reg_subkey.c_str(), 0, views[i], &h);
            if (rc != ERROR_SUCCESS) continue;
            DWORD v = 0, sz = sizeof(v), t = 0;
            rc = RegQueryValueExA(h, dep.reg_value.c_str(), nullptr, &t,
                reinterpret_cast<LPBYTE>(&v), &sz);
            RegCloseKey(h);
            if (rc == ERROR_SUCCESS && t == REG_DWORD && v != 0) return true;
        }
        return false;
    }

    case check_kind::dotnet_runtime: {
        std::error_code ec;
        if (!std::filesystem::exists(dep.check_path, ec)) return false;
        for (const auto& e : std::filesystem::directory_iterator(dep.check_path, ec)) {
            if (ec) break;
            if (!e.is_directory()) continue;
            const std::string folder = e.path().filename().string();
            if (folder.rfind(dep.version_prefix, 0) == 0) return true;
        }
        return false;
    }
    }
    return false;
}

static void check_all_dependencies() {
    for (auto& dep : g_deps) {
        dep.installed = is_dependency_installed(dep);
        dep.status = dep.installed ? "Installed" : "Not found";
        if (dep.kind == dep_kind::raw_group) {
            int done = 0;
            for (const auto& f : dep.group_files)
                if (std::filesystem::exists(resolve_in_install_dir(f))) done++;
            dep.group_done = done;
        }
    }
}

static void build_menu_status() {
    g_menu_status.clear();
    g_menu_status.push_back({"Connected", false});
    g_menu_status.push_back({"Welcome back", false});
    int present = 0; for (auto& d : g_deps) if (d.installed) present++;
    char line[128];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "Dependencies ready (%d/%d)", present, (int)g_deps.size());
    g_menu_status.push_back({line, false});
    g_menu_status.push_back({"All runtime components verified", false});
    g_menu_status.push_back({present == (int)g_deps.size() ? "Ready to launch" : "Some components missing", true});
}

static void install_dependency(dependency_t& dep) {
    if (dep.kind == dep_kind::raw_group) {
        auto dir = get_install_dir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        { std::lock_guard<std::mutex> lk(g_mutex);
          dep.downloading = true; dep.failed = false;
          dep.status = "Downloading..."; dep.progress = 0;
          dep.group_done = 0; }

        int total = (int)dep.group_files.size();
        int ok_count = 0;

        for (int i = 0; i < total; i++) {
            const auto& fname = dep.group_files[i];
            auto dest = (dir / fname).string();

            if (std::filesystem::exists(dest, ec)) {
                std::lock_guard<std::mutex> lk(g_mutex);
                ok_count++;
                dep.group_done = ok_count;
                dep.progress = (float)ok_count / (float)total;
                continue;
            }

            { std::lock_guard<std::mutex> lk(g_mutex);
              dep.status = std::string("Downloading ") + fname; }

            float file_progress = 0;
            std::string url = std::string(GITHUB_RAW) + fname;
            bool ok = download_file(url, dest, file_progress);

            std::lock_guard<std::mutex> lk(g_mutex);
            if (ok && std::filesystem::exists(dest, ec)) {
                ok_count++;
                dep.group_done = ok_count;
                dep.progress = (float)ok_count / (float)total;
            } else {
                add_log("Failed: " + fname);
            }
        }

        std::lock_guard<std::mutex> lk(g_mutex);
        dep.downloading = false;
        dep.progress = 1.0f;
        dep.group_done = ok_count;
        if (ok_count == total) {
            dep.installed = true; dep.failed = false;
            dep.status = "Downloaded";
        } else {
            dep.installed = false; dep.failed = true;
            char buf[64];
            _snprintf_s(buf, sizeof(buf), _TRUNCATE, "Failed (%d/%d)", ok_count, total);
            dep.status = buf;
        }
        return;
    }

    if (dep.kind == dep_kind::raw_file) {
        std::filesystem::path dest(dep.dest_path);
        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);

        { std::lock_guard<std::mutex> lk(g_mutex);
          dep.downloading = true; dep.failed = false;
          dep.status = "Downloading..."; dep.progress = 0; }

        bool ok = download_file(dep.download_url, dep.dest_path, dep.progress);

        std::lock_guard<std::mutex> lk(g_mutex);
        dep.downloading = false;
        dep.progress = 1.0f;
        if (ok && std::filesystem::exists(dep.dest_path, ec)) {
            dep.installed = true; dep.failed = false;
            dep.status = "Downloaded";
        } else {
            dep.installed = false; dep.failed = true;
            dep.status = "Download failed!";
        }
        return;
    }

    char temp_path[MAX_PATH]; GetTempPathA(MAX_PATH, temp_path);
    std::string filename = dep.name;
    for (auto& c : filename) if (c == ' ' || c == '+' || c == '(' || c == ')') c = '_';
    filename += ".exe";
    std::string full_path = std::string(temp_path) + filename;

    add_log("  [" + dep.name + "] downloading from " + dep.download_url);

    { std::lock_guard<std::mutex> lk(g_mutex);
      dep.downloading = true; dep.failed = false;
      dep.status = "Downloading..."; }

    bool ok = download_file(dep.download_url, full_path, dep.progress);

    if (!ok) {
        std::lock_guard<std::mutex> lk(g_mutex);
        dep.downloading = false; dep.failed = true;
        dep.status = "Download failed!";
        add_log("  [" + dep.name + "] DOWNLOAD FAILED");
        return;
    }

    add_log("  [" + dep.name + "] downloaded, running installer...");

    { std::lock_guard<std::mutex> lk(g_mutex);
      dep.downloading = false; dep.installing = true;
      dep.status = "Installing..."; dep.progress = 0; }

    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas";
    sei.lpFile = full_path.c_str();
    sei.lpParameters = dep.install_args.c_str();
    sei.nShow = SW_HIDE;

    DWORD exit_code = 0;
    bool launched = ShellExecuteExA(&sei) != FALSE;
    if (!launched) {
        DWORD err = GetLastError();
        char buf[64];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "ShellExecute failed (err=%lu)", err);
        add_log("  [" + dep.name + "] " + buf);
    } else if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        GetExitCodeProcess(sei.hProcess, &exit_code);
        CloseHandle(sei.hProcess);
        char buf[64];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "exit code %lu", exit_code);
        add_log("  [" + dep.name + "] installer finished: " + buf);
    }
    DeleteFileA(full_path.c_str());

    bool now = is_dependency_installed(dep);
    std::lock_guard<std::mutex> lk(g_mutex);
    dep.installing = false;
    dep.installed = now;
    dep.failed = !now;
    dep.progress = 1.0f;
    dep.status = now ? "Installed" : "Install failed";
}

static void install_all() {
    g_installing = true;
    add_log("Starting installation...");
    for (auto& dep : g_deps) {
        if (g_should_close) break;
        if (dep.installed) {
            add_log(dep.name + " - already installed");
            continue;
        }
        add_log("Processing " + dep.name + "...");
        install_dependency(dep);
        std::lock_guard<std::mutex> lk(g_mutex);
        add_log(dep.installed ? (dep.name + " - OK") : (dep.name + " - failed"));
    }
    g_installing = false;
    g_done = true;
    add_log("All dependencies processed.");

    bool all_ok = true;
    for (const auto& d : g_deps) if (!d.installed) { all_ok = false; break; }
    if (all_ok) write_install_marker();

    build_menu_status();
    g_view = app_view::menu;
}

static bool launch_target() {
    if (!is_roblox_running()) {
        add_log("ERROR: launch Roblox first");
        MessageBoxA(g_hwnd, "Launch Roblox first!", "Turtle.Club Launcher",
            MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return false;
    }
    auto target = (get_install_dir() / "tc1.exe").string();
    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) { add_log("ERROR: tc1.exe not found"); return false; }
    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei); sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas"; sei.lpFile = target.c_str();
    auto dir_str = get_install_dir().string(); sei.lpDirectory = dir_str.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExA(&sei)) { if (sei.hProcess) CloseHandle(sei.hProcess); return true; }
    return false;
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
    switch (m) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_CLOSE: g_should_close = true; return 0;
    case WM_LBUTTONUP: g_dragging = false; ReleaseCapture(); return 0;
    case WM_MOUSEMOVE:
        if (g_dragging && (w & MK_LBUTTON)) {
            POINT cur; GetCursorPos(&cur);
            SetWindowPos(h, nullptr, cur.x - g_drag_offset.x, cur.y - g_drag_offset.y, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void start_window_drag() {
    g_dragging = true;
    SetCapture(g_hwnd);
    POINT cur; GetCursorPos(&cur);
    RECT rc; GetWindowRect(g_hwnd, &rc);
    g_drag_offset.x = cur.x - rc.left;
    g_drag_offset.y = cur.y - rc.top;
}

static void resize_window_for_view(app_view v) {
    if (v == g_window_sized_for) return;
    int w = (v == app_view::menu) ? kMenuW : kInstallW;
    int h = ((v == app_view::menu) ? kMenuH : kInstallH) + (int)kTitleBarH;
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    SetWindowPos(g_hwnd, nullptr, (sx - w) / 2, (sy - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_swapchain) {
        g_swapchain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* bb = nullptr; g_swapchain->GetBuffer(0, IID_PPV_ARGS(&bb));
        if (bb) { g_device->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); }
    }
    g_window_sized_for = v;
}


static void apply_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 4; s.GrabRounding = 4; s.ChildRounding = 0;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.ChildBorderSize = 1;
    s.WindowPadding = ImVec2(18, 16); s.ItemSpacing = ImVec2(10, 10); s.FramePadding = ImVec2(10, 6);
    s.ScrollbarSize = 8.0f;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.075f, 0.075f, 1);
    s.Colors[ImGuiCol_ChildBg]  = ImVec4(0.075f, 0.075f, 0.075f, 1);
    s.Colors[ImGuiCol_Border]   = ImVec4(0.145f, 0.145f, 0.145f, 1.0f);
    s.Colors[ImGuiCol_Button]   = ImVec4(0.16f, 0.52f, 0.32f, 1);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.64f, 0.40f, 1);
    s.Colors[ImGuiCol_ButtonActive]  = ImVec4(0.13f, 0.44f, 0.27f, 1);
    s.Colors[ImGuiCol_FrameBg]   = ImVec4(0.13f, 0.13f, 0.15f, 1);
    s.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.30f, 0.75f, 0.50f, 1);
    s.Colors[ImGuiCol_Text]      = ImVec4(0.92f, 0.92f, 0.94f, 1);
    s.Colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.23f, 1);
}

static void draw_rgb_glow_line(ImDrawList* draw, ImVec2 pos, float width, float thickness) {
    const int segments = 96; const float seg_w = width / segments;
    const float time = (float)ImGui::GetTime(), speed = 0.08f, glow_h = 22.0f;
    const int glow_layers = 6;
    auto color_at = [&](float t) -> ImVec4 {
        float hue = fmodf(t * 0.9f + time * speed, 1.0f); float r, g, b;
        ImGui::ColorConvertHSVtoRGB(hue, 0.75f, 1.0f, r, g, b); return ImVec4(r, g, b, 1);
    };
    for (int s = 0; s < segments; s++) {
        float t0 = (float)s / segments, t1 = (float)(s + 1) / segments;
        ImVec4 c0 = color_at(t0), c1 = color_at(t1);
        float x0 = pos.x + s * seg_w, x1 = pos.x + (s + 1) * seg_w;
        for (int g = 0; g < glow_layers; g++) {
            float layer = (float)(g + 1) / glow_layers, h = glow_h * layer, alpha = 0.10f * (1.0f - layer);
            draw->AddRectFilledMultiColor(ImVec2(x0, pos.y), ImVec2(x1, pos.y + h),
                ImGui::GetColorU32(ImVec4(c0.x, c0.y, c0.z, alpha)),
                ImGui::GetColorU32(ImVec4(c1.x, c1.y, c1.z, alpha)),
                ImGui::GetColorU32(ImVec4(c1.x, c1.y, c1.z, alpha)),
                ImGui::GetColorU32(ImVec4(c0.x, c0.y, c0.z, alpha)));
        }
    }
    for (int s = 0; s < segments; s++) {
        float t0 = (float)s / segments, t1 = (float)(s + 1) / segments;
        float x0 = pos.x + s * seg_w, x1 = pos.x + (s + 1) * seg_w;
        draw->AddRectFilledMultiColor(ImVec2(x0, pos.y), ImVec2(x1, pos.y + thickness),
            ImGui::GetColorU32(color_at(t0)), ImGui::GetColorU32(color_at(t1)),
            ImGui::GetColorU32(color_at(t1)), ImGui::GetColorU32(color_at(t0)));
    }
}

static void draw_outer_frame(ImVec2 p0, ImVec2 p1) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 dark_rim  = ImGui::GetColorU32(ImVec4(0.020f, 0.020f, 0.020f, 1.0f));
    ImU32 light_rim = ImGui::GetColorU32(ImVec4(0.145f, 0.145f, 0.145f, 1.0f));
    dl->AddRect(p0, p1, dark_rim, 0.0f, 0, 1.0f);
    dl->AddRect(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p1.y - 1), light_rim, 0.0f, 0, 1.0f);
}

static void draw_status_badge(const char* text, ImVec4 bg, ImVec4 fg) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 ts = ImGui::CalcTextSize(text); float px = 9, py = 3;
    ImVec2 bs(ts.x + px * 2, ts.y + py * 2);
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImVec2 p0(right - bs.x, ImGui::GetCursorScreenPos().y), p1(right, p0.y + bs.y);
    draw->AddRectFilled(p0, p1, ImGui::GetColorU32(bg), 5.0f);
    draw->AddText(ImVec2(p0.x + px, p0.y + py), ImGui::GetColorU32(fg), text);
    ImGui::Dummy(bs);
}

static void draw_title_bar() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    draw->AddRectFilled(ImVec2(0, 0), ImVec2(w, kTitleBarH),
        ImGui::GetColorU32(ImVec4(0.10f, 0.10f, 0.12f, 1.0f)));

    ImVec2 text_pos(14.0f, (kTitleBarH - ImGui::GetTextLineHeight()) * 0.5f);
    draw->AddText(text_pos, ImGui::GetColorU32(ImVec4(0.85f, 0.88f, 0.92f, 1.0f)),
        "Turtle.Club Launcher");

    const float btn_w = 46.0f;
    ImVec2 mouse = io.MousePos;
    bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    {
        ImVec2 p0(w - btn_w, 0), p1(w, kTitleBarH);
        bool hovered = mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= 0 && mouse.y <= kTitleBarH;
        if (hovered)
            draw->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(0.78f, 0.20f, 0.22f, 1.0f)));
        ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        float s = 5.0f;
        ImU32 col = ImGui::GetColorU32(ImVec4(0.95f, 0.95f, 0.97f, 1.0f));
        draw->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, 1.6f);
        draw->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s), col, 1.6f);
        if (hovered && clicked) g_should_close = true;
    }

    {
        ImVec2 p0(w - btn_w * 2, 0), p1(w - btn_w, kTitleBarH);
        bool hovered = mouse.x >= p0.x && mouse.x <= p1.x && mouse.y >= 0 && mouse.y <= kTitleBarH;
        if (hovered)
            draw->AddRectFilled(p0, p1, ImGui::GetColorU32(ImVec4(0.22f, 0.22f, 0.26f, 1.0f)));
        ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
        draw->AddLine(ImVec2(c.x - 5, c.y + 4), ImVec2(c.x + 5, c.y + 4),
            ImGui::GetColorU32(ImVec4(0.95f, 0.95f, 0.97f, 1.0f)), 1.6f);
        if (hovered && clicked) ShowWindow(g_hwnd, SW_MINIMIZE);
    }

    bool over_buttons = mouse.x >= (w - btn_w * 2);
    bool in_titlebar = mouse.y >= 0 && mouse.y <= kTitleBarH;
    if (in_titlebar && !over_buttons && clicked && !g_dragging) {
        start_window_drag();
    }
}

static void draw_install_view() {
    ImGuiStyle& style = ImGui::GetStyle();
    float avail_w = ImGui::GetContentRegionAvail().x;

    if (g_font_title) ImGui::PushFont(g_font_title);
    float tw = ImGui::CalcTextSize("Dependency Installer").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - tw) * 0.5f);
    ImGui::TextColored(ImVec4(0.55f, 0.92f, 0.62f, 1), "Dependency Installer");
    if (g_font_title) ImGui::PopFont();

    const char* sub = "Required runtime components";
    float sw = ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sw) * 0.5f);
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.60f, 1), "%s", sub);
    ImGui::Spacing();

    const float row_text_h = ImGui::GetTextLineHeight();
    const float bar_h = 5.0f;
    const float row_h = row_text_h + bar_h + style.ItemSpacing.y;
    const float sep_h = 1.0f + style.ItemSpacing.y * 2.0f;
    const int n = (int)g_deps.size();
    float list_inner = row_h * n + sep_h * (n > 0 ? (n - 1) : 0);
    float list_h = list_inner + style.WindowPadding.y * 2.0f;

    const float btn_h = 40.0f;
    const float status_label_h = ImGui::GetTextLineHeightWithSpacing();
    const float region_h = ImGui::GetContentRegionAvail().y;
    float log_h = region_h - list_h - status_label_h - btn_h - style.ItemSpacing.y * 3.0f;
    float min_log = ImGui::GetTextLineHeightWithSpacing() * 3 + style.WindowPadding.y * 2;
    if (log_h < min_log) {
        log_h = min_log;
        list_h = region_h - log_h - status_label_h - btn_h - style.ItemSpacing.y * 3.0f;
    }

    ImGui::BeginChild("##dep_list", ImVec2(0, list_h), ImGuiChildFlags_Borders, 0);
    for (size_t i = 0; i < g_deps.size(); i++) {
        auto& dep = g_deps[i];
        ImGui::PushID((int)i);
        ImGui::AlignTextToFramePadding();

        if (dep.kind == dep_kind::raw_group && dep.group_total > 0) {
            char label[160];
            _snprintf_s(label, sizeof(label), _TRUNCATE, "%s  (%d/%d files)",
                dep.name.c_str(), dep.group_done, dep.group_total);
            ImGui::TextUnformatted(label);
        } else {
            ImGui::TextUnformatted(dep.name.c_str());
        }

        ImVec4 bg, fg; const char* badge;
        if (dep.installed)        { badge = "READY";    bg = ImVec4(0.15f, 0.40f, 0.26f, 1); fg = ImVec4(0.65f, 1, 0.78f, 1); }
        else if (dep.downloading) { badge = "DOWNLOAD"; bg = ImVec4(0.40f, 0.34f, 0.12f, 1); fg = ImVec4(1, 0.90f, 0.55f, 1); }
        else if (dep.installing)  { badge = "INSTALL";  bg = ImVec4(0.40f, 0.34f, 0.12f, 1); fg = ImVec4(1, 0.90f, 0.55f, 1); }
        else if (dep.failed)      { badge = "FAILED";   bg = ImVec4(0.50f, 0.16f, 0.16f, 1); fg = ImVec4(1, 0.65f, 0.65f, 1); }
        else                      { badge = "MISSING";  bg = ImVec4(0.42f, 0.18f, 0.18f, 1); fg = ImVec4(1, 0.70f, 0.70f, 1); }
        ImGui::SameLine(); draw_status_badge(badge, bg, fg);
        if (dep.downloading || dep.installing)
            ImGui::ProgressBar(dep.progress, ImVec2(-1, bar_h), "");
        ImGui::PopID();
        if (i + 1 < g_deps.size()) ImGui::Separator();
    }
    ImGui::EndChild();

    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.60f, 1), "Status");
    ImGui::BeginChild("##log", ImVec2(0, log_h), ImGuiChildFlags_Borders, 0);
    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        if (g_log.empty())
            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1), "Idle - press Install to begin.");
        else
            for (auto& l : g_log)
                ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.82f, 1), "%s", l.c_str());
        if (g_log_scroll_pending) { ImGui::SetScrollHereY(1.0f); g_log_scroll_pending = false; }
    }
    ImGui::EndChild();

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - btn_h - style.WindowPadding.y);
    if (g_installing.load()) {
        ImGui::BeginDisabled();
        ImGui::Button("Installing...", ImVec2(-1, btn_h));
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Install", ImVec2(-1, btn_h)))
            std::thread(install_all).detach();
    }
}

static bool launcher_button(const char* label, const ImVec2& size) {
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##btn", size);
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = cursor;
    ImVec2 p1 = ImVec2(cursor.x + size.x, cursor.y + size.y);

    ImU32 base, top, bot;
    if (active) {
        top = ImGui::GetColorU32(ImVec4(0.135f, 0.135f, 0.140f, 1.0f));
        bot = ImGui::GetColorU32(ImVec4(0.105f, 0.105f, 0.110f, 1.0f));
    } else if (hovered) {
        top = ImGui::GetColorU32(ImVec4(0.255f, 0.255f, 0.265f, 1.0f));
        bot = ImGui::GetColorU32(ImVec4(0.165f, 0.165f, 0.175f, 1.0f));
    } else {
        top = ImGui::GetColorU32(ImVec4(0.215f, 0.215f, 0.225f, 1.0f));
        bot = ImGui::GetColorU32(ImVec4(0.140f, 0.140f, 0.150f, 1.0f));
    }

    dl->AddRectFilledMultiColor(p0, p1, top, top, bot, bot);

    // subtle top highlight (blik) like the reference buttons
    ImU32 hi0 = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, hovered ? 0.10f : 0.06f));
    ImU32 hi1 = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
    dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, p0.y + size.y * 0.5f), hi0, hi0, hi1, hi1);

    ImU32 dark_rim = ImGui::GetColorU32(ImVec4(0.020f, 0.020f, 0.020f, 1.0f));
    ImU32 light_rim = ImGui::GetColorU32(ImVec4(0.32f, 0.32f, 0.34f, 1.0f));

    dl->AddRect(p0, p1, dark_rim, 0.0f, 0, 1.0f);
    dl->AddRect(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p1.x - 1, p1.y - 1), light_rim, 0.0f, 0, 1.0f);

    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 tp(p0.x + (size.x - ts.x) * 0.5f, p0.y + (size.y - ts.y) * 0.5f);
    dl->AddText(tp, ImGui::GetColorU32(ImVec4(0.95f, 0.95f, 0.97f, 1.0f)), label);

    return clicked;
}

static void draw_menu_view() {
    ImGuiStyle& style = ImGui::GetStyle();
    float full_w = ImGui::GetContentRegionAvail().x;
    float full_h = ImGui::GetContentRegionAvail().y;

    ImVec2 outer_p0 = ImGui::GetCursorScreenPos();
    ImVec2 outer_p1 = ImVec2(outer_p0.x + full_w, outer_p0.y + full_h);
    draw_outer_frame(outer_p0, outer_p1);

    const float pad = 14.0f;
    const float gap = 10.0f;
    const float options_w = 200.0f;
    const float top_h = 165.0f;
    const float lbl_h = ImGui::GetTextLineHeight() + 4.0f;

    ImGui::SetCursorScreenPos(ImVec2(outer_p0.x + pad, outer_p0.y + pad));

    float card_w = full_w - pad * 2 - options_w - gap;

    ImVec2 row_origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddText(ImVec2(row_origin.x + card_w + gap + 2, row_origin.y),
        ImGui::GetColorU32(ImVec4(0.85f, 0.85f, 0.88f, 1.0f)), "Options");

    ImGui::SetCursorScreenPos(ImVec2(row_origin.x, row_origin.y + lbl_h));
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + card_w, p0.y + top_h);

        ImGui::BeginChild("##card", ImVec2(card_w, top_h), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImGui::Dummy(ImVec2(0, 6));
            float icon = 48.0f;
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (g_logo_texture)
                ImGui::GetWindowDrawList()->AddImage((ImTextureID)g_logo_texture, p, ImVec2(p.x + icon, p.y + icon));
            else
                ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + icon, p.y + icon),
                    ImGui::GetColorU32(ImVec4(0.18f, 0.22f, 0.28f, 1)), 4.0f);

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon + 12);
            ImGui::BeginGroup();
            ImGui::TextColored(kAccentLime, "%s", kProductName);
            ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.75f, 1), "Updated %s", g_product_updated.c_str());
            ImGui::EndGroup();
        }
        ImGui::EndChild();
        draw_outer_frame(p0, p1);
    }

    ImGui::SameLine(0, gap);
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + options_w, p0.y + top_h);

        ImGui::BeginChild("##opts", ImVec2(options_w, top_h), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            float bw = ImGui::GetContentRegionAvail().x;
            float avail_h = ImGui::GetContentRegionAvail().y;
            const float bgap = 12.0f;
            float bh = (avail_h - bgap) * 0.5f;

            float start_x = ImGui::GetCursorPosX();
            float start_y = ImGui::GetCursorPosY();

            ImGui::SetCursorPos(ImVec2(start_x, start_y));
            if (launcher_button("Inject", ImVec2(bw, bh))) {
                if (launch_target()) g_should_close = true;
            }
            ImGui::SetCursorPos(ImVec2(start_x, start_y + bh + bgap));
            if (launcher_button("Exit", ImVec2(bw, bh))) { g_should_close = true; }
        }
        ImGui::EndChild();
        draw_outer_frame(p0, p1);
    }

    ImGui::SetCursorScreenPos(ImVec2(outer_p0.x + pad, outer_p0.y + pad + lbl_h + top_h + 6));

    {
        ImVec2 lbl_pos = ImGui::GetCursorScreenPos();
        dl->AddText(lbl_pos, ImGui::GetColorU32(ImVec4(0.85f, 0.85f, 0.88f, 1.0f)), "Status");
    }

    ImGui::SetCursorScreenPos(ImVec2(outer_p0.x + pad,
        outer_p0.y + pad + lbl_h + top_h + 6 + lbl_h));
    {
        float status_w = full_w - pad * 2;
        float status_h = full_h - (pad * 2 + lbl_h + top_h + 6 + lbl_h);
        if (status_h < 80) status_h = 80;
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + status_w, p0.y + status_h);

        ImGui::BeginChild("##mstatus", ImVec2(status_w, status_h), ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImDrawList* sdl = ImGui::GetWindowDrawList();
            const float row_h = ImGui::GetTextLineHeight() + 12.0f;
            const float text_indent = 18.0f;
            ImVec2 base = ImGui::GetCursorScreenPos();
            base.y += 6.0f;
            float content_w = ImGui::GetContentRegionAvail().x;

            for (size_t i = 0; i < g_menu_status.size(); i++) {
                auto& e = g_menu_status[i];
                float ry = base.y + row_h * (float)i;
                if (e.second) {
                    // highlight bar behind the active line (matches CS2 reference)
                    sdl->AddRectFilled(
                        ImVec2(base.x - 6.0f, ry - 2.0f),
                        ImVec2(base.x - 6.0f + content_w + 6.0f, ry + row_h - 4.0f),
                        ImGui::GetColorU32(ImVec4(0.16f, 0.16f, 0.16f, 1.0f)));
                    sdl->AddText(ImVec2(base.x + text_indent, ry + 1.0f),
                        ImGui::GetColorU32(kAccentLime), e.first.c_str());
                } else {
                    sdl->AddText(ImVec2(base.x + text_indent, ry + 1.0f),
                        ImGui::GetColorU32(ImVec4(0.82f, 0.82f, 0.86f, 1.0f)), e.first.c_str());
                }
            }
        }
        ImGui::EndChild();
        draw_outer_frame(p0, p1);
    }
}

static void draw_ui() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 ws = io.DisplaySize;

    draw_title_bar();

    ImGui::SetNextWindowPos(ImVec2(0, kTitleBarH));
    ImGui::SetNextWindowSize(ImVec2(ws.x, ws.y - kTitleBarH));
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (g_view.load() == app_view::menu) draw_menu_view();
    else                                 draw_install_view();

    ImGui::End();

    draw_rgb_glow_line(ImGui::GetForegroundDrawList(), ImVec2(0, kTitleBarH), ws.x, 3.0f);
}

static ID3D11ShaderResourceView* create_texture_from_png(ID3D11Device* dev, const unsigned char* data, int len) {
    int w = 0, h = 0, ch = 0;
    unsigned char* pixels = stbi_load_from_memory(data, len, &w, &h, &ch, 4);
    if (!pixels) return nullptr;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub = {}; sub.pSysMem = pixels; sub.SysMemPitch = w * 4;
    ID3D11Texture2D* tex = nullptr;
    dev->CreateTexture2D(&desc, &sub, &tex);
    stbi_image_free(pixels);
    if (!tex) return nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = desc.Format; srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* out = nullptr;
    dev->CreateShaderResourceView(tex, &srv, &out);
    tex->Release();
    return out;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    g_wc.cbSize = sizeof(g_wc); g_wc.style = CS_CLASSDC;
    g_wc.lpfnWndProc = wnd_proc; g_wc.hInstance = hInstance;
    g_wc.lpszClassName = "TurtleClubLauncher";
    g_wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExA(&g_wc);

    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    int win_w = kInstallW, win_h = kInstallH + (int)kTitleBarH;
    g_hwnd = CreateWindowExA(0, g_wc.lpszClassName, "Turtle.Club Launcher",
        WS_POPUP | WS_VISIBLE, (sx - win_w) / 2, (sy - win_h) / 2, win_w, win_h,
        nullptr, nullptr, hInstance, nullptr);
    if (!g_hwnd) return 1;

    BOOL dark = TRUE; DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof(dark));
    ShowWindow(g_hwnd, SW_SHOW); UpdateWindow(g_hwnd);
    SetForegroundWindow(g_hwnd);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION, &sd, &g_swapchain, &g_device, &fl, &g_context))) return 1;
    ID3D11Texture2D* bb = nullptr; g_swapchain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (bb) { g_device->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); }
    if (!g_rtv) return 1;

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.IniFilename = nullptr;
    apply_style();

    ImFontConfig cfg; cfg.FontDataOwnedByAtlas = false;
    g_font_regular = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(font_verdana_bold), (int)font_verdana_bold_size, 16.0f, &cfg);
    g_font_title = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(font_tahoma_bold), (int)font_tahoma_bold_size, 25.0f, &cfg);
    io.FontDefault = g_font_regular;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    g_logo_texture = create_texture_from_png(g_device, roblox_logo_data, (int)sizeof(roblox_logo_data));

    init_dependencies();
    check_all_dependencies();

    {   std::time_t t = std::time(nullptr); std::tm tm_buf{};
        localtime_s(&tm_buf, &t); char buf[32];
        std::strftime(buf, sizeof(buf), "%Y/%m/%d %H:%M", &tm_buf);
        g_product_updated = buf;
    }

    bool all = true; for (auto& d : g_deps) if (!d.installed) { all = false; break; }
    if (all) {
        add_log("All dependencies already installed.");
        build_menu_status();
        if (has_install_marker()) g_view = app_view::menu;
    }

    MSG msg = {};
    while (!g_should_close.load()) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_should_close = true;
        }
        if (g_should_close) break;
        resize_window_for_view(g_view.load());

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        draw_ui();
        ImGui::Render();
        const float clear[] = { 0.075f, 0.075f, 0.075f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapchain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    if (g_logo_texture) g_logo_texture->Release();
    if (g_rtv) g_rtv->Release(); if (g_swapchain) g_swapchain->Release();
    if (g_context) g_context->Release(); if (g_device) g_device->Release();
    DestroyWindow(g_hwnd); UnregisterClassA(g_wc.lpszClassName, hInstance);
    return 0;
}
