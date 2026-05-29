#include "fog.h"

#include <algorithm>
#include <thread>
#include <chrono>

#include <memory/memory.h>
#include <sdk/offsets.h>
#include <sdk/sdk.h>
#include <game/game.h>
#include <settings.h>

namespace lighting
{
	namespace fog
	{
		struct OriginalFog {
			float fog_start;
			float fog_end;
			math::vector3 fog_color;
			math::vector3 atmosphere_color;
			float atmosphere_density;
			float atmosphere_haze;
			bool saved = false;
			bool atmosphere_saved = false;
		};

		static OriginalFog original;
		static bool was_enabled = false;
		static std::uint64_t last_datamodel = 0;

		static void restore_fog(const rbx::instance_t& lighting, const rbx::instance_t& atmosphere)
		{
			if (!original.saved || lighting.address == 0)
			{
				return;
			}

			memory->write<float>(lighting.address + Offsets::Lighting::FogStart, original.fog_start);
			memory->write<float>(lighting.address + Offsets::Lighting::FogEnd, original.fog_end);
			memory->write<math::vector3>(lighting.address + Offsets::Lighting::FogColor, original.fog_color);

			if (atmosphere.address != 0 && original.atmosphere_saved)
			{
				memory->write<math::vector3>(atmosphere.address + Offsets::Atmosphere::Color, original.atmosphere_color);
				memory->write<float>(atmosphere.address + Offsets::Atmosphere::Density, original.atmosphere_density);
				memory->write<float>(atmosphere.address + Offsets::Atmosphere::Haze, original.atmosphere_haze);
			}
		}

		static void fog_thread() {
			using namespace std::chrono_literals;

			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(250));

				try {
					if (game::datamodel.address == 0) {
						original = {};
						was_enabled = false;
						last_datamodel = 0;
						continue;
					}

					if (game::datamodel.address != last_datamodel)
					{
						last_datamodel = game::datamodel.address;
						original = {};
						was_enabled = false;
					}

					rbx::instance_t lighting = game::datamodel.find_first_child("Lighting");
					if (lighting.address == 0) {
						lighting = game::datamodel.find_first_child_by_class("Lighting");
					}
					if (lighting.address == 0) {
						continue;
					}

					rbx::instance_t atmosphere = lighting.find_first_child_by_class("Atmosphere");

					if (!settings::lighting::fog::enabled) {
						if (was_enabled && original.saved) {
							restore_fog(lighting, atmosphere);
							was_enabled = false;
						}
						continue;
					}

					if (!original.saved) {
						original.fog_start = memory->read<float>(lighting.address + Offsets::Lighting::FogStart);
						original.fog_end = memory->read<float>(lighting.address + Offsets::Lighting::FogEnd);
						original.fog_color = memory->read<math::vector3>(lighting.address + Offsets::Lighting::FogColor);
						original.saved = true;
					}

					if (atmosphere.address != 0 && !original.atmosphere_saved)
					{
						original.atmosphere_color = memory->read<math::vector3>(atmosphere.address + Offsets::Atmosphere::Color);
						original.atmosphere_density = memory->read<float>(atmosphere.address + Offsets::Atmosphere::Density);
						original.atmosphere_haze = memory->read<float>(atmosphere.address + Offsets::Atmosphere::Haze);
						original.atmosphere_saved = true;
					}

					const float fog_start = settings::lighting::fog::fog_start;
					const float fog_end = (std::max)(fog_start + 1.0f, settings::lighting::fog::fog_end);
					memory->write<float>(lighting.address + Offsets::Lighting::FogStart, fog_start);
					memory->write<float>(lighting.address + Offsets::Lighting::FogEnd, fog_end);
					
					math::vector3 fog_color(settings::lighting::fog::fog_r, 
						settings::lighting::fog::fog_g, 
						settings::lighting::fog::fog_b);
					memory->write<math::vector3>(lighting.address + Offsets::Lighting::FogColor, fog_color);

					if (atmosphere.address != 0)
					{
						const float fog_range = fog_end - fog_start;
						const float density = (std::clamp)(600.0f / fog_range, 0.05f, 0.65f);
						const float haze = (std::clamp)(density * 2.0f, 0.1f, 1.5f);

						memory->write<math::vector3>(atmosphere.address + Offsets::Atmosphere::Color, fog_color);
						memory->write<float>(atmosphere.address + Offsets::Atmosphere::Density, density);
						memory->write<float>(atmosphere.address + Offsets::Atmosphere::Haze, haze);
					}

					was_enabled = true;
				}
				catch (...) {
				}
			}
		}

		void run() {
			static bool initialized = false;
			if (!initialized) {
				std::thread(fog_thread).detach();
				initialized = true;
			}
		}
	}
}
