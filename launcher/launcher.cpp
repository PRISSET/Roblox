// Turtle.Club Launcher - Windows GUI (DX11 + ImGui)
// Adapted from preview_tmp/preview.cpp for Windows platform.
// Downloads dependencies from GitHub and launches tc1.exe.

#include <Windows.h>
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

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ============================================================
// Constants
// ============================================================
static const char* GITHUB_RAW = "https://raw.githubusercontent.com/PRISSET/Roblox/main/bin/";
static const char* kProductName = "Turtle.Club Loader";

static constexpr int kInstallW = 440;
static constexpr int kInstallH = 600;
static constexpr int kMenuW   = 720;
static constexpr int kMenuH   = 460;

// ============================================================
// Dependency definitions
// ============================================================
struct dependency_t {
    std::string name;
    std::string url;
    std::string dest_filename;
    bool installed = false;
    bool downloading = false;
    bool installing = false;
    float progress = 0.0f;
    std::string status;
};

static std::vector<dependency_t> g_deps;
static std::atomic<bool> g_installing{false};
static std::atomic<bool> g_done{false};
static std::atomic<bool> g_should_close{false};
static std::atomic<bool> g_user_continue{false};
static std::mutex g_mutex;

enum class app_view { install, menu };
static std::atomic<app_view> g_view{app_view::install};

static std::vector<std::string> g_log;
static std::mutex g_log_mutex;
static bool g_log_scroll_pending = false;

static std::vector<std::pair<std::string,bool>> g_menu_status;
static std::string g_product_updated;

// ============================================================
// DX11 / Window state
// ============================================================
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

// ============================================================
// Helpers
// ============================================================
static std::filesystem::path get_install_dir() {
    char p[MAX_PATH]={}; GetModuleFileNameA(nullptr,p,MAX_PATH);
    return std::filesystem::path(p).parent_path() / "TurtleClub";
}

static void add_log(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_log_mutex);
    g_log.push_back(s); g_log_scroll_pending = true;
}

static void build_menu_status() {
    g_menu_status.clear();
    g_menu_status.push_back({"Connected", false});
    g_menu_status.push_back({"Welcome back", false});
    int present=0; for(auto&d:g_deps) if(d.installed) present++;
    char line[128];
    _snprintf_s(line,sizeof(line),_TRUNCATE,"Dependencies ready (%d/%d)",present,(int)g_deps.size());
    g_menu_status.push_back({line, false});
    g_menu_status.push_back({"All runtime components verified", false});
    g_menu_status.push_back({present==(int)g_deps.size()?"Ready to launch":"Warning: some components missing", true});
}

// ============================================================
// Download with progress
// ============================================================
class DownloadProgress : public IBindStatusCallback {
public:
    float* pp = nullptr;
    DownloadProgress(float* p):pp(p){}
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG done,ULONG total,ULONG,LPCWSTR) override {
        if(total>0&&pp) *pp=(float)done/(float)total; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r,void**p) override {
        if(r==IID_IBindStatusCallback||r==IID_IUnknown){*p=this;return S_OK;} return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}
    ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD,IBinding*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT,LPCWSTR) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*,BINDINFO*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD,DWORD,FORMATETC*,STGMEDIUM*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID,IUnknown*) override{return S_OK;}
};

// ============================================================
// Init dependencies
// ============================================================
static void init_deps() {
    g_deps.clear();
    auto dir = get_install_dir();
    auto add = [&](const char* name, const char* file) {
        dependency_t d;
        d.name = name;
        d.url = std::string(GITHUB_RAW) + file;
        d.dest_filename = file;
        auto path = (dir / file).string();
        std::error_code ec;
        d.installed = std::filesystem::exists(path, ec);
        d.status = d.installed ? "Installed" : "Not found";
        g_deps.push_back(d);
    };
    add("tc1 loader (tc1.exe)",       "tc1.exe");
    add("libcurl (libcurl.dll)",      "libcurl.dll");
    add("FreeType (freetype.dll)",    "freetype.dll");
    add("libpng (libpng16.dll)",      "libpng16.dll");
    add("zlib (zlib1.dll)",           "zlib1.dll");
    add("zstd (zstd.dll)",            "zstd.dll");
    add("xxhash (xxhash.dll)",        "xxhash.dll");
    add("brotli (brotlicommon.dll)",   "brotlicommon.dll");
    add("brotli dec (brotlidec.dll)", "brotlidec.dll");
    add("bz2 (bz2.dll)",             "bz2.dll");
}

// ============================================================
// Install thread
// ============================================================
static void install_all() {
    g_installing = true;
    add_log("Starting download...");
    auto dir = get_install_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    for (auto& dep : g_deps) {
        if (g_should_close) break;
        if (dep.installed) { add_log(dep.name + " - already present"); continue; }
        {std::lock_guard<std::mutex> lk(g_mutex); dep.downloading=true; dep.status="Downloading..."; dep.progress=0;}
        add_log("Downloading " + dep.name + "...");

        auto dest = (dir / dep.dest_filename).string();
        DownloadProgress cb(&dep.progress);
        std::wstring wurl(dep.url.begin(), dep.url.end());
        std::wstring wdest(dest.begin(), dest.end());
        HRESULT hr = URLDownloadToFileW(nullptr, wurl.c_str(), wdest.c_str(), 0, &cb);

        std::lock_guard<std::mutex> lk(g_mutex);
        dep.downloading = false;
        dep.progress = 1.0f;
        if (SUCCEEDED(hr) && std::filesystem::exists(dest, ec)) {
            dep.installed = true; dep.status = "Downloaded";
            add_log(dep.name + " - OK");
        } else {
            dep.status = "Failed!";
            add_log(dep.name + " - FAILED");
        }
    }
    g_installing = false; g_done = true;
    add_log("All done.");
    build_menu_status();
    g_view = app_view::menu;
}

// ============================================================
// Launch tc1.exe
// ============================================================
static bool launch_target() {
    auto dir = get_install_dir();
    auto target = (dir / "tc1.exe").string();
    std::error_code ec;
    if (!std::filesystem::exists(target, ec)) { add_log("ERROR: tc1.exe not found"); return false; }
    SHELLEXECUTEINFOA sei={};
    sei.cbSize=sizeof(sei); sei.fMask=SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb="runas"; sei.lpFile=target.c_str();
    auto dir_str = dir.string(); sei.lpDirectory=dir_str.c_str();
    sei.nShow=SW_SHOWNORMAL;
    if(ShellExecuteExA(&sei)){if(sei.hProcess)CloseHandle(sei.hProcess); add_log("Launched tc1.exe"); return true;}
    add_log("ERROR: failed to launch"); return false;
}

// ============================================================
// Window proc
// ============================================================
static LRESULT CALLBACK wnd_proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(ImGui_ImplWin32_WndProcHandler(h,m,w,l)) return true;
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    if(m==WM_CLOSE){g_should_close=true;return 0;}
    return DefWindowProcA(h,m,w,l);
}

// ============================================================
// Resize window for view
// ============================================================
static void resize_window_for_view(app_view v) {
    if(v==g_window_sized_for) return;
    int w=(v==app_view::menu)?kMenuW:kInstallW;
    int h=(v==app_view::menu)?kMenuH:kInstallH;
    int sx=GetSystemMetrics(SM_CXSCREEN), sy=GetSystemMetrics(SM_CYSCREEN);
    if(g_rtv){g_rtv->Release();g_rtv=nullptr;}
    SetWindowPos(g_hwnd,nullptr,(sx-w)/2,(sy-h)/2,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
    if(g_swapchain){
        g_swapchain->ResizeBuffers(0,(UINT)w,(UINT)h,DXGI_FORMAT_UNKNOWN,0);
        ID3D11Texture2D*bb=nullptr; g_swapchain->GetBuffer(0,IID_PPV_ARGS(&bb));
        if(bb){g_device->CreateRenderTargetView(bb,nullptr,&g_rtv);bb->Release();}
    }
    g_window_sized_for=v;
}

// ============================================================
// Drawing helpers (from preview)
// ============================================================
static void draw_rgb_glow_line(ImDrawList* draw, ImVec2 pos, float width, float thickness) {
    const int segments=96; const float seg_w=width/segments;
    const float time=(float)ImGui::GetTime(), speed=0.08f, glow_height=22.0f; const int glow_layers=6;
    auto color_at=[&](float t)->ImVec4{
        float hue=fmodf(t*0.9f+time*speed,1.0f); float r,g,b;
        ImGui::ColorConvertHSVtoRGB(hue,0.75f,1.0f,r,g,b); return ImVec4(r,g,b,1);
    };
    for(int s=0;s<segments;s++){
        float t0=(float)s/segments,t1=(float)(s+1)/segments;
        ImVec4 c0=color_at(t0),c1=color_at(t1);
        float x0=pos.x+s*seg_w,x1=pos.x+(s+1)*seg_w;
        for(int g=0;g<glow_layers;g++){
            float layer=(float)(g+1)/glow_layers,h=glow_height*layer,alpha=0.10f*(1.0f-layer);
            draw->AddRectFilledMultiColor(ImVec2(x0,pos.y),ImVec2(x1,pos.y+h),
                ImGui::GetColorU32(ImVec4(c0.x,c0.y,c0.z,alpha)),ImGui::GetColorU32(ImVec4(c1.x,c1.y,c1.z,alpha)),
                ImGui::GetColorU32(ImVec4(c1.x,c1.y,c1.z,alpha)),ImGui::GetColorU32(ImVec4(c0.x,c0.y,c0.z,alpha)));
        }
    }
    for(int s=0;s<segments;s++){
        float t0=(float)s/segments,t1=(float)(s+1)/segments;
        float x0=pos.x+s*seg_w,x1=pos.x+(s+1)*seg_w;
        draw->AddRectFilledMultiColor(ImVec2(x0,pos.y),ImVec2(x1,pos.y+thickness),
            ImGui::GetColorU32(color_at(t0)),ImGui::GetColorU32(color_at(t1)),
            ImGui::GetColorU32(color_at(t1)),ImGui::GetColorU32(color_at(t0)));
    }
}

static void draw_status_badge(const char* text, ImVec4 bg, ImVec4 fg) {
    ImDrawList* draw=ImGui::GetWindowDrawList();
    ImVec2 ts=ImGui::CalcTextSize(text); float px=9,py=3;
    ImVec2 bs(ts.x+px*2,ts.y+py*2);
    float right=ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x;
    ImVec2 p0(right-bs.x,ImGui::GetCursorScreenPos().y),p1(right,p0.y+bs.y);
    draw->AddRectFilled(p0,p1,ImGui::GetColorU32(bg),5.0f);
    draw->AddText(ImVec2(p0.x+px,p0.y+py),ImGui::GetColorU32(fg),text);
    ImGui::Dummy(bs);
}

// ============================================================
// Install view
// ============================================================
static void draw_install_view() {
    ImGuiStyle& style=ImGui::GetStyle();
    float avail_w=ImGui::GetContentRegionAvail().x;

    if(g_font_title) ImGui::PushFont(g_font_title);
    float tw=ImGui::CalcTextSize("Dependency Installer").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(avail_w-tw)*0.5f);
    ImGui::TextColored(ImVec4(0.55f,0.92f,0.62f,1),"Dependency Installer");
    if(g_font_title) ImGui::PopFont();

    const char* sub="Required runtime components";
    float sw=ImGui::CalcTextSize(sub).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(avail_w-sw)*0.5f);
    ImGui::TextColored(ImVec4(0.55f,0.55f,0.60f,1),"%s",sub);
    ImGui::Spacing();

    const float row_h=ImGui::GetTextLineHeight()+5.0f+style.ItemSpacing.y;
    const float sep_h=1.0f+style.ItemSpacing.y*2.0f;
    const int n=(int)g_deps.size();
    float list_h=row_h*n+sep_h*(n>0?(n-1):0)+style.WindowPadding.y*2.0f;
    const float btn_h=40.0f;
    const float status_h=ImGui::GetTextLineHeightWithSpacing();
    const float region_h=ImGui::GetContentRegionAvail().y;
    float log_h=region_h-list_h-status_h-btn_h-style.ItemSpacing.y*3.0f;
    float min_log=ImGui::GetTextLineHeightWithSpacing()*2+style.WindowPadding.y*2;
    if(log_h<min_log){log_h=min_log;list_h=region_h-log_h-status_h-btn_h-style.ItemSpacing.y*3.0f;}

    ImGui::BeginChild("##dep_list",ImVec2(0,list_h),ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    for(size_t i=0;i<g_deps.size();i++){
        auto&dep=g_deps[i]; ImGui::PushID((int)i);
        ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(dep.name.c_str());
        ImVec4 bg,fg; const char* badge;
        if(dep.installed){badge="READY";bg=ImVec4(0.15f,0.40f,0.26f,1);fg=ImVec4(0.65f,1,0.78f,1);}
        else if(dep.downloading){badge="DOWNLOAD";bg=ImVec4(0.40f,0.34f,0.12f,1);fg=ImVec4(1,0.90f,0.55f,1);}
        else{badge="MISSING";bg=ImVec4(0.42f,0.18f,0.18f,1);fg=ImVec4(1,0.70f,0.70f,1);}
        ImGui::SameLine(); draw_status_badge(badge,bg,fg);
        if(dep.downloading) ImGui::ProgressBar(dep.progress,ImVec2(-1,5),"");
        ImGui::PopID();
        if(i+1<g_deps.size()) ImGui::Separator();
    }
    ImGui::EndChild();

    ImGui::TextColored(ImVec4(0.55f,0.55f,0.60f,1),"Status");
    ImGui::BeginChild("##log",ImVec2(0,log_h),ImGuiChildFlags_Borders,0);
    {std::lock_guard<std::mutex> lk(g_log_mutex);
    if(g_log.empty()) ImGui::TextColored(ImVec4(0.45f,0.45f,0.50f,1),"Idle - press Install to begin.");
    else for(auto&l:g_log) ImGui::TextColored(ImVec4(0.75f,0.78f,0.82f,1),"%s",l.c_str());
    if(g_log_scroll_pending){ImGui::SetScrollHereY(1.0f);g_log_scroll_pending=false;}}
    ImGui::EndChild();

    ImGui::SetCursorPosY(ImGui::GetWindowHeight()-btn_h-style.WindowPadding.y);
    if(g_installing.load()){
        ImGui::BeginDisabled(); ImGui::Button("Installing...",ImVec2(-1,btn_h)); ImGui::EndDisabled();
    } else {
        if(ImGui::Button("Install",ImVec2(-1,btn_h))) std::thread(install_all).detach();
    }
}

// ============================================================
// Menu view (post-install launcher)
// ============================================================
static bool launcher_button(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.13f,0.13f,0.15f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.18f,0.18f,0.21f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0.10f,0.10f,0.12f,1));
    bool c=ImGui::Button(label,size); ImGui::PopStyleColor(3); return c;
}

static void draw_menu_view() {
    float full_w=ImGui::GetContentRegionAvail().x;
    const float gap=12,options_w=200,top_h=150;
    const float card_w=full_w-options_w-gap;

    ImGui::BeginChild("##card",ImVec2(card_w,top_h),ImGuiChildFlags_Borders);
    {
        ImGui::Dummy(ImVec2(0,2)); float icon=64;
        ImVec2 p=ImGui::GetCursorScreenPos();
        if(g_logo_texture)
            ImGui::GetWindowDrawList()->AddImage((ImTextureID)g_logo_texture,p,ImVec2(p.x+icon,p.y+icon));
        else
            ImGui::GetWindowDrawList()->AddRectFilled(p,ImVec2(p.x+icon,p.y+icon),
                ImGui::GetColorU32(ImVec4(0.18f,0.22f,0.28f,1)),6.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+icon+14);
        ImGui::BeginGroup();
        if(g_font_title) ImGui::PushFont(g_font_title);
        ImGui::TextColored(ImVec4(0.55f,0.92f,0.62f,1),"%s",kProductName);
        if(g_font_title) ImGui::PopFont();
        ImGui::TextColored(ImVec4(0.70f,0.70f,0.75f,1),"Updated %s",g_product_updated.c_str());
        ImGui::EndGroup();
    }
    ImGui::EndChild();

    ImGui::SameLine(0,gap);
    ImGui::BeginChild("##opts",ImVec2(options_w,top_h),ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    {
        ImGui::TextColored(ImVec4(0.70f,0.70f,0.75f,1),"Options"); ImGui::Spacing();
        float bw=ImGui::GetContentRegionAvail().x;
        float bh=(ImGui::GetContentRegionAvail().y-ImGui::GetStyle().ItemSpacing.y)*0.5f;
        if(bh<28) bh=28;
        if(launcher_button("Load",ImVec2(bw,bh))){launch_target(); g_user_continue=true; g_should_close=true;}
        if(launcher_button("Exit",ImVec2(bw,bh))){g_should_close=true;}
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.70f,0.70f,0.75f,1),"Status");
    float sh=ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##mstatus",ImVec2(0,sh),ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    for(auto&e:g_menu_status){
        if(e.second) ImGui::TextColored(ImVec4(0.55f,0.90f,0.30f,1),"%s",e.first.c_str());
        else ImGui::TextColored(ImVec4(0.80f,0.80f,0.85f,1),"%s",e.first.c_str());
    }
    ImGui::EndChild();
}

// ============================================================
// Main draw
// ============================================================
static void draw_ui() {
    ImGuiIO& io=ImGui::GetIO(); ImVec2 ws=io.DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ws);
    ImGui::Begin("##main",nullptr,
        ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoScrollbar|
        ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoBringToFrontOnFocus);
    draw_rgb_glow_line(ImGui::GetForegroundDrawList(),ImVec2(0,0),ws.x,3.0f);
    if(g_view.load()==app_view::menu) draw_menu_view(); else draw_install_view();
    ImGui::End();
}

// ============================================================
// Apply style (same as preview)
// ============================================================
static void apply_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s=ImGui::GetStyle();
    s.WindowRounding=0;s.FrameRounding=6;s.GrabRounding=6;s.ChildRounding=6;
    s.WindowBorderSize=0;s.FrameBorderSize=0;s.ChildBorderSize=1;
    s.WindowPadding=ImVec2(18,16);s.ItemSpacing=ImVec2(10,10);s.FramePadding=ImVec2(10,6);
    s.ScrollbarSize=8.0f;
    s.Colors[ImGuiCol_WindowBg]=ImVec4(0.07f,0.07f,0.08f,1);
    s.Colors[ImGuiCol_ChildBg]=ImVec4(0.10f,0.10f,0.11f,1);
    s.Colors[ImGuiCol_Border]=ImVec4(0.20f,0.20f,0.23f,1);
    s.Colors[ImGuiCol_Button]=ImVec4(0.16f,0.52f,0.32f,1);
    s.Colors[ImGuiCol_ButtonHovered]=ImVec4(0.20f,0.64f,0.40f,1);
    s.Colors[ImGuiCol_ButtonActive]=ImVec4(0.13f,0.44f,0.27f,1);
    s.Colors[ImGuiCol_FrameBg]=ImVec4(0.13f,0.13f,0.15f,1);
    s.Colors[ImGuiCol_PlotHistogram]=ImVec4(0.30f,0.75f,0.50f,1);
    s.Colors[ImGuiCol_Text]=ImVec4(0.92f,0.92f,0.94f,1);
    s.Colors[ImGuiCol_Separator]=ImVec4(0.20f,0.20f,0.23f,1);
}

// ============================================================
// Inline logo texture loader (stb_image -> DX11 SRV)
// ============================================================
static ID3D11ShaderResourceView* create_texture_from_png(ID3D11Device* dev, const unsigned char* data, int len) {
    int w=0,h=0,ch=0;
    unsigned char* pixels = stbi_load_from_memory(data, len, &w, &h, &ch, 4);
    if(!pixels) return nullptr;

    D3D11_TEXTURE2D_DESC desc={};
    desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub={};
    sub.pSysMem=pixels; sub.SysMemPitch=w*4;

    ID3D11Texture2D* tex=nullptr;
    dev->CreateTexture2D(&desc,&sub,&tex);
    stbi_image_free(pixels);
    if(!tex) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv={};
    srv.Format=desc.Format; srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels=1;
    ID3D11ShaderResourceView* out=nullptr;
    dev->CreateShaderResourceView(tex,&srv,&out);
    tex->Release();
    return out;
}

// ============================================================
// WinMain entry point (no console window)
// ============================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Create window
    g_wc.cbSize=sizeof(g_wc); g_wc.style=CS_CLASSDC;
    g_wc.lpfnWndProc=wnd_proc; g_wc.hInstance=hInstance;
    g_wc.lpszClassName="TurtleClubLauncher"; g_wc.hCursor=LoadCursor(nullptr,IDC_ARROW);
    RegisterClassExA(&g_wc);

    int sx=GetSystemMetrics(SM_CXSCREEN),sy=GetSystemMetrics(SM_CYSCREEN);
    g_hwnd=CreateWindowExA(0,g_wc.lpszClassName,"Turtle.Club Launcher",
        WS_POPUP|WS_VISIBLE,(sx-kInstallW)/2,(sy-kInstallH)/2,kInstallW,kInstallH,
        nullptr,nullptr,hInstance,nullptr);
    if(!g_hwnd) return 1;

    BOOL dark=TRUE; DwmSetWindowAttribute(g_hwnd,20,&dark,sizeof(dark));
    ShowWindow(g_hwnd,SW_SHOW); UpdateWindow(g_hwnd);
    SetForegroundWindow(g_hwnd);

    // Create D3D11 device
    DXGI_SWAP_CHAIN_DESC sd={};
    sd.BufferCount=1; sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow=g_hwnd;
    sd.SampleDesc.Count=1; sd.Windowed=TRUE; sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl; D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};
    if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
        levels,2,D3D11_SDK_VERSION,&sd,&g_swapchain,&g_device,&fl,&g_context))) return 1;

    ID3D11Texture2D*bb=nullptr; g_swapchain->GetBuffer(0,IID_PPV_ARGS(&bb));
    if(bb){g_device->CreateRenderTargetView(bb,nullptr,&g_rtv);bb->Release();}
    if(!g_rtv) return 1;

    // ImGui init
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO(); io.IniFilename=nullptr;
    apply_style();

    ImFontConfig cfg; cfg.FontDataOwnedByAtlas=false;
    g_font_regular=io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(font_verdana_bold),(int)font_verdana_bold_size,16.0f,&cfg);
    g_font_title=io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(font_tahoma_bold),(int)font_tahoma_bold_size,25.0f,&cfg);
    io.FontDefault=g_font_regular;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device,g_context);

    // Load logo texture
    g_logo_texture=create_texture_from_png(g_device,roblox_logo_data,(int)roblox_logo_size);

    // Init deps and date
    init_deps();
    {std::time_t t=std::time(nullptr); std::tm tm_buf{};
    localtime_s(&tm_buf,&t); char buf[32];
    std::strftime(buf,sizeof(buf),"%Y/%m/%d %H:%M",&tm_buf); g_product_updated=buf;}

    // If all present, skip to menu
    bool all=true; for(auto&d:g_deps) if(!d.installed){all=false;break;}
    if(all){add_log("All files present."); build_menu_status(); g_view=app_view::menu;}

    // Main loop
    MSG msg={};
    while(!g_should_close.load()){
        while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){
            TranslateMessage(&msg); DispatchMessage(&msg);
            if(msg.message==WM_QUIT) g_should_close=true;
        }
        if(g_should_close) break;
        resize_window_for_view(g_view.load());

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        draw_ui();
        ImGui::Render();
        const float clear[]={0.06f,0.06f,0.06f,1.0f};
        g_context->OMSetRenderTargets(1,&g_rtv,nullptr);
        g_context->ClearRenderTargetView(g_rtv,clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapchain->Present(1,0);
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    if(g_logo_texture){g_logo_texture->Release();}
    if(g_rtv){g_rtv->Release();} if(g_swapchain){g_swapchain->Release();}
    if(g_context){g_context->Release();} if(g_device){g_device->Release();}
    DestroyWindow(g_hwnd); UnregisterClassA(g_wc.lpszClassName,hInstance);
    return 0;
}
