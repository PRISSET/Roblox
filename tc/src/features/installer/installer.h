#pragma once

#include <string>
#include <vector>
#include <utility>

// What kind of action a dependency entry represents.
enum class dep_kind {
    installer,   // download an installer (.exe) and run it silently
    raw_file,    // download a raw file (.dll/.exe/etc.) straight to dest_path
};

// How a dependency's presence is verified.
enum class check_kind {
    file,            // check_path must exist on disk
    load_library,    // check_path is a DLL that must actually load (truly "invokes" it)
    registry_dword,  // HKLM\reg_subkey : reg_value must exist and be non-zero
    dotnet_runtime,  // a shared .NET runtime dir with a folder matching version_prefix
};

struct dependency_t {
    std::string name;
    std::string download_url;
    std::string install_args;   // silent install arguments (installer kind only)

    dep_kind    kind = dep_kind::installer;
    std::string dest_path;      // where a raw_file is written (relative to exe dir or absolute)

    check_kind  check = check_kind::file;
    std::string check_path;     // file / dll name / .NET base dir
    std::string reg_subkey;     // for registry_dword
    std::string reg_value;      // for registry_dword
    std::string version_prefix; // for dotnet_runtime, e.g. "8."

    bool installed = false;
    bool downloading = false;
    bool installing = false;
    float progress = 0.0f;
    std::string status;
};

class installer_t {
public:
    // Runs the installer/menu window.
    // Returns true if the user chose to continue (Load), false to exit the app.
    static bool run();

private:
    static bool create_window();
    static bool create_device();
    static bool create_imgui();
    static void destroy();

    static void check_dependencies();
    static bool is_dependency_installed(const dependency_t& dep);
    static void install_all();
    static void install_dependency(dependency_t& dep);
    static bool download_file(const std::string& url, const std::string& dest, float& progress);

    static void render_frame();
    static void draw_ui();
    static void draw_main_view();   // install screen
    static void draw_main_menu();   // post-install launcher menu
    static void add_log(const std::string& line);
    static void build_menu_status();
};
