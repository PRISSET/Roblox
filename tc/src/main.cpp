#include <cstdint>
#include <chrono>
#include <thread>
#include <windows.h>
#include <iostream>

#include <memory/memory.h>
#include <sdk/offsets.h>
#include <sdk/sdk.h>
#include <game/game.h>
#include <cache/cache.h>
#include <render/render.h>
#include <settings.h>
#include <features/explorer/explorer.h>
#include <features/lighting/exposure/exposure.h>
#include <features/lighting/fog/fog.h>
#include <features/lighting/clocktime/clocktime.h>
#include <features/aimbot/aimbot.h>
#include <features/silentaim/silentaim.h>
#include <features/material/lp_material.h>
#include <features/movement/movement.h>
#include <features/rage/hitsound/hitsounds.h>
#include <features/rage/hittracers/hittracers.h>
#include <features/rage/orbit/orbit.h>
#include <features/rage/rapidfire/rapidfire.h>
#include <features/rage/hbe/hitbox_expander.h>
#include <features/rage/noclip/noclip.h>
#include <features/rage/spin360/spin360.h>
#include <features/exploits/headless.h>
#include <features/exploits/korblox.h>
#include "../src/menu/menu.h"
#include <game/rescan/rescan.h>
#include <features/menu/console/console.h>
#include "../../protection/protection/antidebug.h"
#include "../../ext/console/console.h"
#include <features/installer/installer.h>

namespace {
	bool should_render_ui() {
		HWND hwnd = GetForegroundWindow();
		HWND roblox_window = game::get_roblox_window();
		HWND overlay_window = render->detail->window;

		if (roblox_window != nullptr && IsWindow(roblox_window)) {
			return (hwnd == roblox_window || hwnd == overlay_window);
		}
		return true;
	}
}

std::int32_t main()
{
    // Keeping debugger detection is optional, but common in these builds
    std::thread(debugger_detection).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    logger::setup();
    SetConsoleTitleA("cigar - Patched");

    // Hide console window while installer/launcher GUI is active
    HWND console_hwnd = GetConsoleWindow();
    if (console_hwnd) ShowWindow(console_hwnd, SW_HIDE);

    // Run dependency installer / launcher menu before anything else.
    // Returns false if the user chose to exit from the menu.
    if (!installer_t::run())
    {
        return 0;
    }

    // Show console again after installer/launcher GUI closes
    if (console_hwnd) ShowWindow(console_hwnd, SW_SHOW);

    /* AUTH REMOVED:
       The blocks for auth_manager::initialize and console_login::authenticate have been deleted.
    */

    static const char* BINARY_NAME = "RobloxPlayerBeta.exe";

    // Wait for Roblox to start (up to 5 minutes)
    LOG_ERROR("Waiting for Roblox to start...");
    std::cout << "Waiting for RobloxPlayerBeta.exe..." << std::endl;
    
    auto wait_start = std::chrono::steady_clock::now();
    while (!memory->find_process_id(BINARY_NAME))
    {
        auto elapsed = std::chrono::steady_clock::now() - wait_start;
        if (std::chrono::duration_cast<std::chrono::minutes>(elapsed).count() >= 5)
        {
            LOG_ERROR("Timed out waiting for Roblox (5 min). Exiting.");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    std::cout << "Roblox found! Attaching..." << std::endl;

    if (!memory->attach_to_process(BINARY_NAME))
    {
        LOG_ERROR("unable to attach to Roblox!");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }

    if (!memory->find_module_address(BINARY_NAME))
    {
        LOG_ERROR("unable to find main module address!");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }

    // Initialize Game DataModels
    std::uint64_t fake_datamodel{ memory->read<std::uint64_t>(memory->get_module_address() + Offsets::FakeDataModel::Pointer) };
    game::datamodel = rbx::instance_t(memory->read<std::uint64_t>(fake_datamodel + Offsets::FakeDataModel::RealDataModel));
    game::visengine = { memory->read<std::uint64_t>(memory->get_module_address() + Offsets::VisualEngine::Pointer) };
    game::players = { game::datamodel.find_first_child_by_class("Players") };

	std::cout << "datamodel ->" << game::datamodel.address << std::endl;

    std::thread(cache::run).detach();

    if (!InitializeStorage())
    {
        LOG_ERROR("failed to initialize storage");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }

    // Launch Feature Threads
    std::thread(AutoRescanHandler).detach();
    std::thread(aimbot::run).detach();
    silentaim::run();
    std::thread(rage::hitsounds_detector_thread).detach();
    std::thread(rage::hittracers_detector_thread).detach();
    std::thread(rage::orbit::run).detach();
    std::thread(rage::rapidfire::run).detach();
    std::thread(rage::hitbox_expander::run).detach();
    std::thread(rage::spin360::run).detach();
    std::thread(rage::noclip::run).detach();
    std::thread(movement::run).detach();

    lighting::exposure::run();
    lighting::fog::run();
    lighting::clocktime::run();

    material::lp_material::run();

    std::thread(exploits::headless::run).detach();
    std::thread(exploits::korblox::run).detach();

    std::thread(menu::console::run).detach();

    // Render Initialization
    if (!render->create_window() || !render->create_device() || !render->create_imgui())
    {
        LOG_ERROR("failed to initialize rendering system");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }

    if (!Menu::Initialize(render->detail->window, render->detail->device, render->detail->device_context))
    {
        LOG_ERROR("failed to initialize menu");
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return 1;
    }

    // Main Loop
    auto last_process_seen = std::chrono::steady_clock::now();
    while (true)
    {
        const DWORD current_pid = memory->find_process_id(BINARY_NAME);
        if (current_pid == 0)
        {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_process_seen).count() >= 12)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::exit(0);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        last_process_seen = std::chrono::steady_clock::now();

        render->start_render();

        if (!should_render_ui())
        {
            render->end_render();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        render->render_visuals();

        if (render->running)
        {
            render->render_menu();
        }

        render->end_render();

        if (settings::menu::performance_mode)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
