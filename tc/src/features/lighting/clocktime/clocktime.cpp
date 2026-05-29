#include "clocktime.h"

#include <thread>
#include <chrono>
#include <cmath>

#include <memory/memory.h>
#include <sdk/offsets.h>
#include <sdk/sdk.h>
#include <sdk/math/math.h>
#include <game/game.h>
#include <settings.h>

namespace lighting
{
	namespace clocktime
	{
		static rbx::instance_t m_cached_lighting{};
		static std::chrono::steady_clock::time_point m_last_lighting_check = std::chrono::steady_clock::now();
		static uint64_t last_dm = 0;

		static void clocktime_thread()
		{
			while (true)
			{
				try
				{
					if (!settings::globals::is_game_active)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(200));
						continue;
					}

					if (game::datamodel.address == 0)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
						continue;
					}

					if (game::datamodel.address != last_dm)
					{
						last_dm = game::datamodel.address;
						m_cached_lighting = {};
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
						continue;
					}

					if (m_cached_lighting.address == 0 || std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_last_lighting_check).count() > 5)
					{
						m_cached_lighting = game::datamodel.find_first_child_by_class("Lighting");
						m_last_lighting_check = std::chrono::steady_clock::now();
					}

					if (m_cached_lighting.address == 0)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(100));
						continue;
					}

					if (settings::lighting::clocktime::enabled)
					{
						rbx::instance_t lighting = m_cached_lighting;

						double clock_time = static_cast<double>(settings::lighting::clocktime::clock_time);
						memory->write<double>(lighting.address + Offsets::Lighting::ClockTime, clock_time);

						float normalized_time = static_cast<float>(clock_time / 24.0);
						float sun_angle = (normalized_time - 0.25f) * 2.0f * 3.14159265359f;

						float latitude = 0.0f;
						float sun_y = std::sin(sun_angle);
						float sun_z = -std::cos(sun_angle) * std::cos(latitude);
						float sun_x = -std::cos(sun_angle) * std::sin(latitude);

						math::vector3 sun_pos(sun_x, sun_y, sun_z);
						math::vector3 moon_pos(-sun_x, -sun_y, -sun_z);

						memory->write<math::vector3>(lighting.address + Offsets::Lighting::SunPosition, sun_pos);
						memory->write<math::vector3>(lighting.address + Offsets::Lighting::MoonPosition, moon_pos);

						math::vector3 gradient_top;
						math::vector3 gradient_bottom;
						math::vector3 outdoor_ambient_color;
						math::vector3 ambient_color;
						math::vector3 atmosphere_color;
						float atmosphere_density;
						float atmosphere_haze;
						float brightness;

						float sun_height = sun_y;

						if (sun_height > 0.8f) {
							gradient_top = math::vector3(0.35f, 0.65f, 0.95f);
							gradient_bottom = math::vector3(0.75f, 0.85f, 0.95f);
							outdoor_ambient_color = math::vector3(0.7f, 0.7f, 0.72f);
							ambient_color = math::vector3(0.7f, 0.7f, 0.72f);
							atmosphere_color = math::vector3(0.65f, 0.8f, 0.95f);
							atmosphere_density = 0.35f;
							atmosphere_haze = 1.8f;
							brightness = 2.0f;
						}
						else if (sun_height > 0.0f) {
							float t = sun_height / 0.8f;
							if (sun_height < 0.15f) {
								float sunset_t = sun_height / 0.15f;
								gradient_top = math::vector3(
									0.15f + sunset_t * 0.2f,
									0.15f + sunset_t * 0.5f,
									0.25f + sunset_t * 0.7f
								);
								gradient_bottom = math::vector3(
									0.95f,
									0.45f + sunset_t * 0.4f,
									0.25f + sunset_t * 0.7f
								);
								outdoor_ambient_color = math::vector3(0.6f, 0.6f, 0.62f);
								ambient_color = math::vector3(0.6f, 0.6f, 0.62f);
								atmosphere_color = math::vector3(
									0.95f,
									0.5f + sunset_t * 0.3f,
									0.35f + sunset_t * 0.6f
								);
								atmosphere_density = 0.25f + sunset_t * 0.1f;
								atmosphere_haze = 1.2f + sunset_t * 0.6f;
								brightness = 1.5f;
							}
							else {
								float day_t = (sun_height - 0.15f) / 0.65f;
								gradient_top = math::vector3(
									0.35f,
									0.65f,
									0.95f
								);
								gradient_bottom = math::vector3(
									0.75f,
									0.85f,
									0.95f
								);
								outdoor_ambient_color = math::vector3(0.7f, 0.7f, 0.72f);
								ambient_color = math::vector3(0.7f, 0.7f, 0.72f);
								atmosphere_color = math::vector3(
									0.65f,
									0.8f,
									0.95f
								);
								atmosphere_density = 0.35f;
								atmosphere_haze = 1.8f;
								brightness = 2.0f;
							}
						}
						else {
							float night_val = (-sun_height) / 0.3f;
							if (night_val < 0.0f) night_val = 0.0f;
							if (night_val > 1.0f) night_val = 1.0f;
							float night_t = night_val;
							gradient_top = math::vector3(
								0.05f + (1.0f - night_t) * 0.1f,
								0.05f + (1.0f - night_t) * 0.1f,
								0.12f + (1.0f - night_t) * 0.13f
							);
							gradient_bottom = math::vector3(
								0.08f + (1.0f - night_t) * 0.07f,
								0.08f + (1.0f - night_t) * 0.07f,
								0.15f + (1.0f - night_t) * 0.1f
							);
							outdoor_ambient_color = math::vector3(0.6f, 0.6f, 0.62f);
							ambient_color = math::vector3(0.6f, 0.6f, 0.62f);
							atmosphere_color = math::vector3(
								0.08f + (1.0f - night_t) * 0.07f,
								0.08f + (1.0f - night_t) * 0.07f,
								0.15f + (1.0f - night_t) * 0.1f
							);
							atmosphere_density = 0.15f + (1.0f - night_t) * 0.1f;
							atmosphere_haze = 0.3f + (1.0f - night_t) * 0.9f;
							brightness = 1.5f;
						}

						memory->write<math::vector3>(lighting.address + Offsets::Lighting::GradientTop, gradient_top);
						memory->write<math::vector3>(lighting.address + Offsets::Lighting::GradientBottom, gradient_bottom);
						memory->write<math::vector3>(lighting.address + Offsets::Lighting::OutdoorAmbient, outdoor_ambient_color);
						memory->write<math::vector3>(lighting.address + Offsets::Lighting::Ambient, ambient_color);
						memory->write<float>(lighting.address + Offsets::Lighting::Brightness, brightness);

						rbx::instance_t atmosphere = lighting.find_first_child_by_class("Atmosphere");
						if (atmosphere.address != 0) {
							memory->write<math::vector3>(atmosphere.address + Offsets::Atmosphere::Color, atmosphere_color);
							memory->write<float>(atmosphere.address + Offsets::Atmosphere::Density, atmosphere_density);
							memory->write<float>(atmosphere.address + Offsets::Atmosphere::Haze, atmosphere_haze);
						}
					}
				}
				catch (...)
				{
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}

		void run()
		{
			static bool initialized = false;
			if (!initialized)
			{
				std::thread(clocktime_thread).detach();
				initialized = true;
			}
		}
	}
}
