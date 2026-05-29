#include "installer.h"

#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <urlmon.h>
#include <shlobj.h>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <ctime>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>

// Embedded fonts shipped with the project
#include "../../../ext/font/arial.h"
#include "../../../ext/font/tahoma_bold.h"
#include "../../../ext/font/verdana_bold.h"
#include "roblox_logo.h"
#include "../../render/textures/texture.h"

#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "Advapi32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
// Static state
// ============================================================

enum class app_view { install, menu };

static HWND g_hwnd = nullptr;
static WNDCLASSEX g_wc = {};
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static IDXGISwapChain* g_swapchain = nullptr;

static ImFont* g_font_regular = nullptr;   // Verdana Bold, body text
static ImFont* g_font_title = nullptr;     // Tahoma Bold, headings

static std::vector<dependency_t> g_dependencies;
static std::atomic<bool> g_installing{ false };
static std::atomic<bool> g_done{ false };
static std::atomic<bool> g_should_close{ false };
static std::atomic<bool> g_user_continue{ false };   // true = Load/Run, false = Exit
static std::mutex g_mutex;
static std::string g_global_status = "Ready";

static std::atomic<app_view> g_view{ app_view::install };

// Status log (CS2-launcher style)
static std::vector<std::string> g_log;
static std::mutex g_log_mutex;
static bool g_log_scroll_pending = false;

// Post-install launcher menu status lines
static std::vector<std::pair<std::string, bool>> g_menu_status; // text, highlight

// Product card info (shown on the launcher menu)
static const char* kProductName = "Turtle.Club Loader";
static std::string g_product_updated;

// Logo texture for the launcher menu
static ID3D11ShaderResourceView* g_logo_texture = nullptr;
static float g_logo_w = 0.0f, g_logo_h = 0.0f;

// Two layouts use two window sizes: compact installer, wide launcher.
static constexpr int kInstallW = 440;
static constexpr int kInstallH = 600;
static constexpr int kMenuW = 720;
static constexpr int kMenuH = 460;

// Tracks which size the window currently is, so we only resize on change.
static app_view g_window_sized_for = app_view::install;

// ============================================================
// Window procedure
// ============================================================

static LRESULT CALLBACK installer_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        g_should_close = true;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============================================================
// Download progress callback
// ============================================================

class DownloadProgress : public IBindStatusCallback {
public:
    float* progress_ptr = nullptr;

    DownloadProgress(float* p) : progress_ptr(p) {}

    HRESULT STDMETHODCALLTYPE OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG, LPCWSTR) override {
        if (ulProgressMax > 0 && progress_ptr)
            *progress_ptr = (float)ulProgress / (float)ulProgressMax;
        return S_OK;
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (riid == IID_IBindStatusCallback || riid == IID_IUnknown) {
            *ppvObject = this;
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    // Unused
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD, IBinding*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*, BINDINFO*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }
};

// ============================================================
// Dependency definitions
//
// Each dependency declares HOW to verify it is present, so we only
// download what's actually missing instead of re-fetching every run.
// ============================================================

// Resolves a path relative to the running executable's directory.
static std::string resolve_near_exe(const std::string& relative)
{
    char exe_path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::filesystem::path dir = std::filesystem::path(exe_path).parent_path();
    return (dir / relative).string();
}

static void init_dependencies()
{
    g_dependencies.clear();

    dependency_t dep;

    // ---------------- Microsoft runtimes ----------------

    // VC++ Redistributable 2015-2022 (x64) - verified via official registry key.
    dep = {};
    dep.name = "VC++ Redist 2015-2022 (x64)";
    dep.kind = dep_kind::installer;
    dep.download_url = "https://aka.ms/vs/17/release/vc_redist.x64.exe";
    dep.install_args = "/install /quiet /norestart";
    dep.check = check_kind::registry_dword;
    dep.reg_subkey = "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64";
    dep.reg_value = "Installed";
    g_dependencies.push_back(dep);

    // VC++ Redistributable 2015-2022 (x86).
    dep = {};
    dep.name = "VC++ Redist 2015-2022 (x86)";
    dep.kind = dep_kind::installer;
    dep.download_url = "https://aka.ms/vs/17/release/vc_redist.x86.exe";
    dep.install_args = "/install /quiet /norestart";
    dep.check = check_kind::registry_dword;
    dep.reg_subkey = "SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x86";
    dep.reg_value = "Installed";
    g_dependencies.push_back(dep);

    // DirectX End-User Runtime - verified by loading a DirectX redist DLL.
    dep = {};
    dep.name = "DirectX End-User Runtime";
    dep.kind = dep_kind::installer;
    dep.download_url = "https://download.microsoft.com/download/1/7/1/1718CCC4-6315-4D8E-9543-8E28A4E18C4C/dxwebsetup.exe";
    dep.install_args = "/Q";
    dep.check = check_kind::load_library;
    dep.check_path = "d3dx9_43.dll";
    g_dependencies.push_back(dep);

    // .NET Desktop Runtime 8 - verified by a shared runtime folder starting "8.".
    dep = {};
    dep.name = ".NET Desktop Runtime 8.0";
    dep.kind = dep_kind::installer;
    dep.download_url = "https://aka.ms/dotnet/8.0/windowsdesktop-runtime-win-x64.exe";
    dep.install_args = "/install /quiet /norestart";
    dep.check = check_kind::dotnet_runtime;
    dep.check_path = "C:\\Program Files\\dotnet\\shared\\Microsoft.WindowsDesktop.App";
    dep.version_prefix = "8.";
    g_dependencies.push_back(dep);

    // ---------------- Loader runtime files (raw downloads) ----------------
    // These are pulled straight from raw GitHub URLs into verify\ next to tc1.exe.
    // Presence is verified by the file existing, so they are only fetched once.
    // TODO: replace the placeholder URLs below with the real raw GitHub links.

    auto add_raw = [&](const std::string& name, const std::string& url, const std::string& rel_dest) {
        dependency_t d{};
        d.name = name;
        d.kind = dep_kind::raw_file;
        d.download_url = url;
        d.dest_path = resolve_near_exe(rel_dest);
        d.check = check_kind::file;
        d.check_path = d.dest_path;
        g_dependencies.push_back(d);
    };

    add_raw("tc1 loader (tc1.exe)",   "https://raw.githubusercontent.com/PRISSET/Roblox/main/bin/tc1.exe",      "verify\\tc1.exe");
    add_raw("Injector (injector.exe)","https://raw.githubusercontent.com/PRISSET/Roblox/main/bin/injector.exe", "verify\\injector.exe");
    add_raw("Core library (core.dll)","https://raw.githubusercontent.com/PRISSET/Roblox/main/bin/core.dll",     "verify\\core.dll");
}

// ============================================================
// Implementation
// ============================================================

bool installer_t::create_window()
{
    g_wc.cbSize = sizeof(g_wc);
    g_wc.style = CS_CLASSDC;
    g_wc.lpfnWndProc = installer_wnd_proc;
    g_wc.hInstance = GetModuleHandleA(nullptr);
    g_wc.lpszClassName = "InstallerWindow";
    g_wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExA(&g_wc);

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int win_w = kInstallW;
    int win_h = kInstallH;
    int x = (screen_w - win_w) / 2;
    int y = (screen_h - win_h) / 2;

    g_hwnd = CreateWindowExA(
        0,
        g_wc.lpszClassName,
        "Dependency Installer",
        WS_POPUP | WS_VISIBLE,
        x, y, win_w, win_h,
        nullptr, nullptr, g_wc.hInstance, nullptr
    );

    if (!g_hwnd)
        return false;

    // Dark title bar (Windows 10+)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof(dark));

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    return true;
}

bool installer_t::create_device()
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION,
        &sd, &g_swapchain, &g_device, &fl, &g_context
    );

    if (FAILED(hr))
        return false;

    ID3D11Texture2D* back_buffer = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer) {
        g_device->CreateRenderTargetView(back_buffer, nullptr, &g_rtv);
        back_buffer->Release();
    }

    return g_rtv != nullptr;
}

// Resizes the window (and the swapchain RTV) to fit the active view, then
// re-centers it on screen. Only does work when the target size changes.
static void resize_window_for_view(app_view view)
{
    if (view == g_window_sized_for)
        return;

    int w = (view == app_view::menu) ? kMenuW : kInstallW;
    int h = (view == app_view::menu) ? kMenuH : kInstallH;

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - w) / 2;
    int y = (screen_h - h) / 2;

    // Release the old render target before resizing swapchain buffers.
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }

    SetWindowPos(g_hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);

    if (g_swapchain) {
        g_swapchain->ResizeBuffers(0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* back_buffer = nullptr;
        g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
        if (back_buffer) {
            g_device->CreateRenderTargetView(back_buffer, nullptr, &g_rtv);
            back_buffer->Release();
        }
    }

    g_window_sized_for = view;
}

bool installer_t::create_imgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.FrameRounding = 6;
    style.GrabRounding = 6;
    style.ChildRounding = 6;
    style.WindowBorderSize = 0;
    style.FrameBorderSize = 0;
    style.ChildBorderSize = 1;
    style.WindowPadding = ImVec2(18, 16);
    style.ItemSpacing = ImVec2(10, 10);
    style.FramePadding = ImVec2(10, 6);
    style.ScrollbarSize = 8.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.07f, 0.08f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.16f, 0.52f, 0.32f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.64f, 0.40f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.13f, 0.44f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.13f, 0.15f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.30f, 0.75f, 0.50f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.94f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.23f, 1.0f);

    // Launcher fonts: Verdana Bold for body, Tahoma Bold for headings.
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false; // data is a static const array
    g_font_regular = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(font_verdana_bold), (int)font_verdana_bold_size, 16.0f, &cfg);
    g_font_title = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(font_tahoma_bold), (int)font_tahoma_bold_size, 25.0f, &cfg);
    io.FontDefault = g_font_regular;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    // Decode the embedded Roblox logo into a DX11 texture for the menu card.
    g_logo_texture = D3D11CreateTextureFromBytes(g_device, roblox_logo_data, roblox_logo_size);
    g_logo_w = 512.0f;
    g_logo_h = 512.0f;

    return true;
}

void installer_t::destroy()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_logo_texture) { g_logo_texture->Release(); g_logo_texture = nullptr; }
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }

    DestroyWindow(g_hwnd);
    UnregisterClassA(g_wc.lpszClassName, g_wc.hInstance);
    g_hwnd = nullptr;
}

void installer_t::check_dependencies()
{
    for (auto& dep : g_dependencies) {
        dep.installed = is_dependency_installed(dep);
        dep.status = dep.installed ? "Installed" : "Not found";
    }
}

// Real detection: load DLLs, query registry, scan the .NET runtime folder.
// This is what lets us skip already-present dependencies on later runs.
bool installer_t::is_dependency_installed(const dependency_t& dep)
{
    switch (dep.check)
    {
    case check_kind::file:
    {
        return std::filesystem::exists(dep.check_path);
    }

    case check_kind::load_library:
    {
        // Actually attempt to load the library; if it resolves, it's present.
        HMODULE existing = GetModuleHandleA(dep.check_path.c_str());
        if (existing)
            return true;

        HMODULE mod = LoadLibraryExA(dep.check_path.c_str(), nullptr,
                                     LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!mod)
            mod = LoadLibraryA(dep.check_path.c_str());
        if (mod) {
            FreeLibrary(mod);
            return true;
        }
        return false;
    }

    case check_kind::registry_dword:
    {
        HKEY hKey = nullptr;
        // Read the native 64-bit view so x64 redist keys are found.
        LONG rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, dep.reg_subkey.c_str(), 0,
                                KEY_READ | KEY_WOW64_64KEY, &hKey);
        if (rc != ERROR_SUCCESS)
            return false;

        DWORD value = 0, size = sizeof(value), type = 0;
        rc = RegQueryValueExA(hKey, dep.reg_value.c_str(), nullptr, &type,
                              reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);

        return (rc == ERROR_SUCCESS && type == REG_DWORD && value != 0);
    }

    case check_kind::dotnet_runtime:
    {
        std::error_code ec;
        if (!std::filesystem::exists(dep.check_path, ec))
            return false;

        for (const auto& entry : std::filesystem::directory_iterator(dep.check_path, ec)) {
            if (ec) break;
            if (!entry.is_directory())
                continue;
            const std::string folder = entry.path().filename().string();
            if (folder.rfind(dep.version_prefix, 0) == 0) // starts with prefix
                return true;
        }
        return false;
    }
    }

    return false;
}

bool installer_t::download_file(const std::string& url, const std::string& dest, float& progress)
{
    progress = 0.0f;
    DownloadProgress callback(&progress);

    std::wstring wurl(url.begin(), url.end());
    std::wstring wdest(dest.begin(), dest.end());

    HRESULT hr = URLDownloadToFileW(nullptr, wurl.c_str(), wdest.c_str(), 0, &callback);
    progress = 1.0f;
    return SUCCEEDED(hr);
}

void installer_t::install_dependency(dependency_t& dep)
{
    // -------- raw_file: download straight to its destination --------
    if (dep.kind == dep_kind::raw_file) {
        std::filesystem::path dest(dep.dest_path);
        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            dep.downloading = true;
            dep.status = "Downloading...";
            dep.progress = 0.0f;
        }

        bool ok = download_file(dep.download_url, dep.dest_path, dep.progress);

        std::lock_guard<std::mutex> lock(g_mutex);
        dep.downloading = false;
        dep.progress = 1.0f;
        if (ok && std::filesystem::exists(dep.dest_path, ec)) {
            dep.installed = true;
            dep.status = "Downloaded";
        } else {
            dep.installed = false;
            dep.status = "Download failed!";
        }
        return;
    }

    // -------- installer: download to temp, run silently --------
    char temp_path[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_path);

    // Extract filename from URL
    std::string filename = dep.name;
    for (auto& c : filename) {
        if (c == ' ' || c == '+' || c == '(' || c == ')') c = '_';
    }
    filename += ".exe";

    std::string full_path = std::string(temp_path) + filename;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        dep.downloading = true;
        dep.status = "Downloading...";
    }

    bool ok = download_file(dep.download_url, full_path, dep.progress);

    if (!ok) {
        std::lock_guard<std::mutex> lock(g_mutex);
        dep.downloading = false;
        dep.status = "Download failed!";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        dep.downloading = false;
        dep.installing = true;
        dep.status = "Installing...";
        dep.progress = 0.0f;
    }

    // Run installer silently
    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "runas";
    sei.lpFile = full_path.c_str();
    sei.lpParameters = dep.install_args.c_str();
    sei.nShow = SW_HIDE;

    if (ShellExecuteExA(&sei) && sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
    }

    // Cleanup temp file
    DeleteFileA(full_path.c_str());

    // Re-check
    bool now_installed = is_dependency_installed(dep);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        dep.installing = false;
        dep.installed = now_installed;
        dep.progress = 1.0f;
        dep.status = now_installed ? "Installed" : "Install complete (verify manually)";
    }
}

void installer_t::add_log(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log.push_back(line);
    g_log_scroll_pending = true;
}

// Launches verify\tc1.exe located next to the running executable.
// Returns true if the process was started.
static bool launch_target()
{
    auto log = [](const std::string& s) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log.push_back(s);
        g_log_scroll_pending = true;
    };

    char exe_path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);

    std::filesystem::path dir = std::filesystem::path(exe_path).parent_path();
    std::filesystem::path target = dir / "verify" / "tc1.exe";

    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) {
        log("ERROR: verify\\tc1.exe not found");
        return false;
    }

    SHELLEXECUTEINFOA sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "open";
    std::string target_str = target.string();
    std::string dir_str = target.parent_path().string();
    sei.lpFile = target_str.c_str();
    sei.lpDirectory = dir_str.c_str();   // run with its own folder as cwd
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExA(&sei)) {
        if (sei.hProcess)
            CloseHandle(sei.hProcess);
        log("Launching tc1.exe...");
        return true;
    }

    log("ERROR: failed to launch tc1.exe");
    return false;
}

void installer_t::install_all()
{
    g_installing = true;
    g_global_status = "Installing dependencies...";
    add_log("Starting installation...");

    std::thread([]() {
        for (auto& dep : g_dependencies) {
            if (dep.installed) {
                add_log(dep.name + " - already installed, skipping");
                continue;
            }

            add_log("Downloading " + dep.name + "...");
            install_dependency(dep);

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (dep.installed)
                    add_log(dep.name + " - installed");
                else
                    add_log(dep.name + " - finished (verify manually)");
            }

            if (g_should_close)
                break;
        }

        g_installing = false;
        g_done = true;
        g_global_status = "All done!";
        add_log("All dependencies processed.");

        // Move to the launcher menu once everything is present.
        build_menu_status();
        g_view = app_view::menu;
    }).detach();
}

// Builds the status lines shown on the post-install launcher menu.
void installer_t::build_menu_status()
{
    g_menu_status.clear();
    g_menu_status.push_back({ "Connected", false });
    g_menu_status.push_back({ "Welcome back", false });

    int present = 0;
    for (const auto& dep : g_dependencies)
        if (dep.installed) present++;

    char line[128];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "Dependencies ready (%d/%d)", present, (int)g_dependencies.size());
    g_menu_status.push_back({ line, false });
    g_menu_status.push_back({ "All runtime components verified", false });

    if (present < (int)g_dependencies.size())
        g_menu_status.push_back({ "Warning: some components could not be verified", true });
    else
        g_menu_status.push_back({ "Ready to launch", true });
}

// Draws a slowly shifting RGB gradient line with a soft glow/shadow below it.
static void draw_rgb_glow_line(ImDrawList* draw, ImVec2 pos, float width, float thickness)
{
    const int segments = 96;               // resolution of the gradient
    const float seg_w = width / segments;
    const float time = (float)ImGui::GetTime();
    const float speed = 0.08f;             // lower = slower color shift
    const float glow_height = 22.0f;       // how far the shadow/glow bleeds down
    const int glow_layers = 6;             // soft shadow build-up

    auto color_at = [&](float t) -> ImVec4 {
        // hue sweeps across the line and drifts over time
        float hue = fmodf(t * 0.9f + time * speed, 1.0f);
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(hue, 0.75f, 1.0f, r, g, b);
        return ImVec4(r, g, b, 1.0f);
    };

    // Soft glow / shadow underneath (drawn first, behind the line)
    for (int s = 0; s < segments; s++) {
        float t0 = (float)s / segments;
        float t1 = (float)(s + 1) / segments;
        ImVec4 c0 = color_at(t0);
        ImVec4 c1 = color_at(t1);

        float x0 = pos.x + s * seg_w;
        float x1 = pos.x + (s + 1) * seg_w;

        for (int g = 0; g < glow_layers; g++) {
            float layer = (float)(g + 1) / glow_layers;
            float h = glow_height * layer;
            float alpha = 0.10f * (1.0f - layer);   // fades out downward

            ImU32 col0 = ImGui::GetColorU32(ImVec4(c0.x, c0.y, c0.z, alpha));
            ImU32 col1 = ImGui::GetColorU32(ImVec4(c1.x, c1.y, c1.z, alpha));

            draw->AddRectFilledMultiColor(
                ImVec2(x0, pos.y),
                ImVec2(x1, pos.y + h),
                col0, col1, col1, col0
            );
        }
    }

    // The crisp gradient line itself
    for (int s = 0; s < segments; s++) {
        float t0 = (float)s / segments;
        float t1 = (float)(s + 1) / segments;
        ImU32 col0 = ImGui::GetColorU32(color_at(t0));
        ImU32 col1 = ImGui::GetColorU32(color_at(t1));

        float x0 = pos.x + s * seg_w;
        float x1 = pos.x + (s + 1) * seg_w;

        draw->AddRectFilledMultiColor(
            ImVec2(x0, pos.y),
            ImVec2(x1, pos.y + thickness),
            col0, col1, col1, col0
        );
    }
}

// Draws a single status badge (rounded pill) at the current cursor, right-aligned.
static void draw_status_badge(const char* text, ImVec4 bg, ImVec4 fg)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 text_size = ImGui::CalcTextSize(text);
    float pad_x = 9.0f, pad_y = 3.0f;
    ImVec2 badge_size(text_size.x + pad_x * 2.0f, text_size.y + pad_y * 2.0f);

    float avail_right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImVec2 p_min(avail_right - badge_size.x, ImGui::GetCursorScreenPos().y);
    ImVec2 p_max(avail_right, p_min.y + badge_size.y);

    draw->AddRectFilled(p_min, p_max, ImGui::GetColorU32(bg), 5.0f);
    draw->AddText(ImVec2(p_min.x + pad_x, p_min.y + pad_y), ImGui::GetColorU32(fg), text);

    ImGui::Dummy(badge_size);
}

void installer_t::draw_main_view()
{
    ImGuiStyle& style = ImGui::GetStyle();
    float avail_w = ImGui::GetContentRegionAvail().x;

    // ---- Header ----
    if (g_font_title) ImGui::PushFont(g_font_title);
    float title_w = ImGui::CalcTextSize("Dependency Installer").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - title_w) * 0.5f);
    ImGui::TextColored(ImVec4(0.55f, 0.92f, 0.62f, 1.0f), "Dependency Installer");
    if (g_font_title) ImGui::PopFont();

    const char* sub = "Required runtime components";
    float sub_w = ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_w) * 0.5f);
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.60f, 1.0f), "%s", sub);

    ImGui::Spacing();

    // ---- Measure the per-row height so the list fits ALL rows exactly ----
    const float row_text_h = ImGui::GetTextLineHeight();
    const float bar_h = 5.0f;                                    // progress bar height
    const float row_h = row_text_h + bar_h + style.ItemSpacing.y; // worst case (active row)
    const float sep_h = 1.0f + style.ItemSpacing.y * 2.0f;       // separator between rows
    const int n = (int)g_dependencies.size();
    float list_inner = row_h * n + sep_h * (n > 0 ? (n - 1) : 0);
    float list_h = list_inner + style.WindowPadding.y * 2.0f;

    // ---- Bottom controls reserved up-front so the button is NEVER clipped ----
    const float btn_h = 40.0f;
    const float status_label_h = ImGui::GetTextLineHeightWithSpacing();
    const float region_h = ImGui::GetContentRegionAvail().y;

    // Log gets whatever is left between the list and the bottom button.
    float log_h = region_h - list_h - status_label_h - btn_h
                  - style.ItemSpacing.y * 3.0f;
    float min_log = ImGui::GetTextLineHeightWithSpacing() * 2.0f + style.WindowPadding.y * 2.0f;
    if (log_h < min_log) {
        // Window too short for everything at full size: let the list shrink and scroll.
        log_h = min_log;
        list_h = region_h - log_h - status_label_h - btn_h - style.ItemSpacing.y * 3.0f;
    }

    // ---- Dependency list ----
    ImGui::BeginChild("##dep_list", ImVec2(0, list_h), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    for (size_t i = 0; i < g_dependencies.size(); i++) {
        auto& dep = g_dependencies[i];
        ImGui::PushID((int)i);

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(dep.name.c_str());

        ImVec4 bg, fg;
        const char* badge;
        if (dep.installed) {
            badge = "READY"; bg = ImVec4(0.15f, 0.40f, 0.26f, 1.0f); fg = ImVec4(0.65f, 1.0f, 0.78f, 1.0f);
        } else if (dep.downloading) {
            badge = "DOWNLOAD"; bg = ImVec4(0.40f, 0.34f, 0.12f, 1.0f); fg = ImVec4(1.0f, 0.90f, 0.55f, 1.0f);
        } else if (dep.installing) {
            badge = "INSTALL"; bg = ImVec4(0.40f, 0.34f, 0.12f, 1.0f); fg = ImVec4(1.0f, 0.90f, 0.55f, 1.0f);
        } else {
            badge = "MISSING"; bg = ImVec4(0.42f, 0.18f, 0.18f, 1.0f); fg = ImVec4(1.0f, 0.70f, 0.70f, 1.0f);
        }

        ImGui::SameLine();
        draw_status_badge(badge, bg, fg);

        if (dep.downloading || dep.installing) {
            ImGui::ProgressBar(dep.progress, ImVec2(-1, bar_h), "");
        }

        ImGui::PopID();

        if (i + 1 < g_dependencies.size())
            ImGui::Separator();
    }

    ImGui::EndChild();

    // ---- Status log box ----
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.60f, 1.0f), "Status");
    ImGui::BeginChild("##status_log", ImVec2(0, log_h), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        if (g_log.empty()) {
            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1.0f), "Idle - press Install to begin.");
        } else {
            for (const auto& line : g_log)
                ImGui::TextColored(ImVec4(0.75f, 0.78f, 0.82f, 1.0f), "%s", line.c_str());
        }
        if (g_log_scroll_pending) {
            ImGui::SetScrollHereY(1.0f);
            g_log_scroll_pending = false;
        }
    }
    ImGui::EndChild();

    // ---- Install button: pinned to the very bottom, full width ----
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - btn_h - style.WindowPadding.y);
    if (g_installing) {
        ImGui::BeginDisabled();
        ImGui::Button("Installing...", ImVec2(-1, btn_h));
        ImGui::EndDisabled();
    } else {
        if (ImGui::Button("Install", ImVec2(-1, btn_h))) {
            install_all();
        }
    }
}

// Draws a flat, bordered panel button like the reference launcher.
static bool launcher_button(const char* label, const ImVec2& size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.21f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

// Post-install launcher menu (modeled on the reference screenshot):
//   left = product card, right = Options (Load/Exit), bottom = Status log.
void installer_t::draw_main_menu()
{
    ImGuiStyle& style = ImGui::GetStyle();
    float full_w = ImGui::GetContentRegionAvail().x;

    const float gap = 12.0f;
    const float options_w = 200.0f;
    const float card_w = full_w - options_w - gap;
    const float top_h = 150.0f;

    // ---------- Product card (left) ----------
    ImGui::BeginChild("##product_card", ImVec2(card_w, top_h), ImGuiChildFlags_Borders);
    {
        ImGui::Dummy(ImVec2(0, 2));
        float icon = 64.0f;
        ImVec2 p = ImGui::GetCursorScreenPos();

        if (g_logo_texture) {
            ImGui::GetWindowDrawList()->AddImage(
                reinterpret_cast<ImTextureID>(g_logo_texture),
                p, ImVec2(p.x + icon, p.y + icon));
        } else {
            // Fallback placeholder if the texture failed to load.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + icon, p.y + icon),
                              ImGui::GetColorU32(ImVec4(0.18f, 0.22f, 0.28f, 1.0f)), 6.0f);
        }

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + icon + 14.0f);
        ImGui::BeginGroup();
        if (g_font_title) ImGui::PushFont(g_font_title);
        ImGui::TextColored(ImVec4(0.55f, 0.92f, 0.62f, 1.0f), "%s", kProductName);
        if (g_font_title) ImGui::PopFont();
        ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.75f, 1.0f), "Updated %s", g_product_updated.c_str());
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    // ---------- Options (right) ----------
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##options", ImVec2(options_w, top_h), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.75f, 1.0f), "Options");
        ImGui::Spacing();

        // Two buttons evenly fill the remaining height. Account for the
        // ItemSpacing ImGui auto-inserts between them so nothing clips.
        float bw = ImGui::GetContentRegionAvail().x;
        float bh = (ImGui::GetContentRegionAvail().y - style.ItemSpacing.y) * 0.5f;
        if (bh < 28.0f) bh = 28.0f;

        // Run launches verify\tc1.exe as a separate process, then closes this
        // launcher (return false -> main() exits, so we don't double-run).
        if (launcher_button("Run", ImVec2(bw, bh))) {
            if (launch_target()) {
                g_user_continue = false;
                g_should_close = true;
            }
        }
        if (launcher_button("Exit", ImVec2(bw, bh))) {
            g_user_continue = false;
            g_should_close = true;
        }
    }
    ImGui::EndChild();

    // ---------- Status (bottom) ----------
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.75f, 1.0f), "Status");

    float status_h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##menu_status", ImVec2(0, status_h), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        for (const auto& entry : g_menu_status) {
            if (entry.second)
                ImGui::TextColored(ImVec4(0.55f, 0.90f, 0.30f, 1.0f), "%s", entry.first.c_str());
            else
                ImGui::TextColored(ImVec4(0.80f, 0.80f, 0.85f, 1.0f), "%s", entry.first.c_str());
        }
    }
    ImGui::EndChild();
    (void)style;
}

void installer_t::draw_ui()
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 window_size = io.DisplaySize;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(window_size);
    ImGui::Begin("##installer_main", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Animated RGB glow line across the very top (drawn over the window edge)
    draw_rgb_glow_line(ImGui::GetForegroundDrawList(), ImVec2(0, 0), window_size.x, 3.0f);

    if (g_view == app_view::menu) {
        draw_main_menu();
    } else {
        draw_main_view();
    }

    ImGui::End();
}

void installer_t::render_frame()
{
    // Resize the window to match the active view before drawing.
    resize_window_for_view(g_view.load());

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    draw_ui();

    ImGui::Render();

    float clear_color[4] = { 0.06f, 0.06f, 0.06f, 1.0f };
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swapchain->Present(1, 0);
}

bool installer_t::run()
{
    if (!create_window())
        return false;

    if (!create_device()) {
        DestroyWindow(g_hwnd);
        return false;
    }

    if (!create_imgui()) {
        destroy();
        return false;
    }

    // Initialize dependency list and check what's already installed.
    init_dependencies();
    check_dependencies();

    // Product "updated" date for the launcher card.
    {
        std::time_t t = std::time(nullptr);
        std::tm tm_buf{};
        localtime_s(&tm_buf, &t);
        char date_buf[32];
        std::strftime(date_buf, sizeof(date_buf), "%Y/%m/%d %H:%M", &tm_buf);
        g_product_updated = date_buf;
    }

    // If every dependency is already present, skip the installer entirely
    // and go straight to the launcher menu.
    bool all_present = true;
    for (const auto& dep : g_dependencies) {
        if (!dep.installed) { all_present = false; break; }
    }
    if (all_present) {
        add_log("All dependencies already present.");
        build_menu_status();
        g_done = true;
        g_view = app_view::menu;
    }

    // Main loop
    MSG msg = {};
    while (!g_should_close) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                g_should_close = true;
            }
        }

        if (g_should_close)
            break;

        render_frame();
    }

    destroy();

    // true  -> user pressed Load (continue into the app)
    // false -> user pressed Exit or closed the window
    return g_user_continue.load();
}
