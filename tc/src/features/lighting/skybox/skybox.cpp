#include "skybox.h"

#include <thread>
#include <chrono>

#include <memory/memory.h>
#include <sdk/offsets.h>
#include <sdk/sdk.h>
#include <sdk/math/math.h>
#include <game/game.h>
#include <settings.h>

namespace lighting
{
	namespace skybox
	{
		static rbx::instance_t m_cached_lighting{};
		static rbx::instance_t m_cached_sky{};
		static std::chrono::steady_clock::time_point m_last_lighting_check = std::chrono::steady_clock::now();
		static std::chrono::steady_clock::time_point m_last_invalidate = std::chrono::steady_clock::now();
		static uint64_t last_dm = 0;

		static std::string m_last_skybox_bk{};
		static std::string m_last_skybox_dn{};
		static std::string m_last_skybox_ft{};
		static std::string m_last_skybox_lf{};
		static std::string m_last_skybox_rt{};
		static std::string m_last_skybox_up{};

		static rbx::instance_t get_sky_from_lighting(const rbx::instance_t& lighting)
		{
			if (lighting.address == 0)
			{
				return {};
			}

			const auto direct_sky_address = memory->read<std::uint64_t>(lighting.address + Offsets::Lighting::Sky);
			if (direct_sky_address != 0 && direct_sky_address != 0xFFFFFFFFFFFFFFFF)
			{
				rbx::instance_t direct_sky(direct_sky_address);
				const auto class_name = direct_sky.get_class_name();
				if (class_name == "Sky")
				{
					return direct_sky;
				}
			}

			return {};
		}

		static rbx::instance_t find_sky_instance(const rbx::instance_t& root, int depth = 0)
		{
			if (root.address == 0 || depth > 4)
			{
				return {};
			}

			auto children = root.get_children();
			for (const auto& child : children)
			{
				const auto class_name = child.get_class_name();
				if (class_name == "Sky")
				{
					return child;
				}

				const auto child_name = child.get_name();
				if (child_name == "Sky")
				{
					return child;
				}
			}

			for (const auto& child : children)
			{
				auto nested = find_sky_instance(child, depth + 1);
				if (nested.address != 0)
				{
					return nested;
				}
			}

			return {};
		}

		struct preset_fallback_t
		{
			math::vector3 gradient_top;
			math::vector3 gradient_bottom;
			math::vector3 ambient;
			math::vector3 outdoor_ambient;
			float brightness;
		};

		static preset_fallback_t get_preset_fallback(int preset_index)
		{
			switch (preset_index)
			{
			case 0: return { { 0.02f, 0.03f, 0.08f }, { 0.00f, 0.00f, 0.02f }, { 0.18f, 0.20f, 0.28f }, { 0.12f, 0.14f, 0.22f }, 0.8f };
			case 1: return { { 0.92f, 0.55f, 0.78f }, { 0.99f, 0.74f, 0.86f }, { 0.65f, 0.54f, 0.62f }, { 0.78f, 0.64f, 0.72f }, 1.5f };
			case 2: return { { 0.40f, 0.72f, 0.96f }, { 0.70f, 0.86f, 0.98f }, { 0.62f, 0.68f, 0.62f }, { 0.74f, 0.82f, 0.74f }, 1.7f };
			case 3: return { { 0.05f, 0.08f, 0.16f }, { 0.10f, 0.12f, 0.20f }, { 0.28f, 0.30f, 0.38f }, { 0.20f, 0.22f, 0.30f }, 0.9f };
			case 4: return { { 0.07f, 0.08f, 0.16f }, { 0.14f, 0.12f, 0.24f }, { 0.30f, 0.32f, 0.42f }, { 0.22f, 0.24f, 0.34f }, 1.0f };
			case 5: return { { 0.80f, 0.86f, 0.94f }, { 0.93f, 0.96f, 0.99f }, { 0.68f, 0.72f, 0.78f }, { 0.82f, 0.86f, 0.90f }, 1.4f };
			case 6: return { { 0.18f, 0.02f, 0.04f }, { 0.36f, 0.04f, 0.08f }, { 0.34f, 0.18f, 0.18f }, { 0.46f, 0.20f, 0.20f }, 1.1f };
			case 7: return { { 0.08f, 0.02f, 0.14f }, { 0.18f, 0.04f, 0.26f }, { 0.24f, 0.18f, 0.34f }, { 0.30f, 0.22f, 0.40f }, 0.95f };
			case 8: return { { 0.20f, 0.62f, 0.82f }, { 0.93f, 0.72f, 0.38f }, { 0.62f, 0.68f, 0.52f }, { 0.74f, 0.78f, 0.58f }, 1.65f };
			case 9: return { { 0.18f, 0.10f, 0.06f }, { 0.38f, 0.18f, 0.08f }, { 0.30f, 0.20f, 0.16f }, { 0.42f, 0.24f, 0.18f }, 1.15f };
			default: return { { 0.35f, 0.65f, 0.95f }, { 0.75f, 0.85f, 0.95f }, { 0.70f, 0.70f, 0.72f }, { 0.70f, 0.70f, 0.72f }, 2.0f };
			}
		}

		static void apply_preset_fallback(const rbx::instance_t& lighting)
		{
			if (lighting.address == 0 || settings::lighting::clocktime::enabled)
			{
				return;
			}

			const auto fallback = get_preset_fallback(settings::lighting::skybox::preset_index);
			memory->write<math::vector3>(lighting.address + Offsets::Lighting::GradientTop, fallback.gradient_top);
			memory->write<math::vector3>(lighting.address + Offsets::Lighting::GradientBottom, fallback.gradient_bottom);
			memory->write<math::vector3>(lighting.address + Offsets::Lighting::Ambient, fallback.ambient);
			memory->write<math::vector3>(lighting.address + Offsets::Lighting::OutdoorAmbient, fallback.outdoor_ambient);
			memory->write<float>(lighting.address + Offsets::Lighting::Brightness, fallback.brightness);
		}

		static void invalidate_render_view()
		{
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_invalidate).count() <= 250)
			{
				return;
			}

			uint64_t ptr1 = memory->read<uint64_t>(game::datamodel.address + 0x1D0);
			if (ptr1 != 0)
			{
				uint64_t ptr2 = memory->read<uint64_t>(ptr1 + 0x8);
				if (ptr2 != 0)
				{
					uint64_t renderView = memory->read<uint64_t>(ptr2 + 0x28);
					if (renderView != 0)
					{
						memory->write<bool>(renderView + Offsets::RenderView::SkyValid, false);
						memory->write<bool>(renderView + Offsets::RenderView::LightingValid, false);
					}
				}
			}

			m_last_invalidate = now;
		}

		static void skybox_thread()
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
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
						continue;
					}

					if (game::datamodel.address != last_dm)
					{
						last_dm = game::datamodel.address;
						m_cached_lighting = {};
						m_cached_sky = {};
						settings::lighting::skybox::apply_skybox = settings::lighting::skybox::enabled;
						std::this_thread::sleep_for(std::chrono::seconds(1));
						continue;
					}

					if (m_cached_lighting.address == 0 || std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_last_lighting_check).count() > 2)
					{
						m_cached_lighting = game::datamodel.find_first_child_by_class("Lighting");
						if (m_cached_lighting.address != 0)
						{
							m_cached_sky = get_sky_from_lighting(m_cached_lighting);
							if (m_cached_sky.address == 0)
							{
								m_cached_sky = find_sky_instance(m_cached_lighting);
							}
						}
						m_last_lighting_check = std::chrono::steady_clock::now();
					}

					if (m_cached_lighting.address == 0)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(500));
						continue;
					}

					if (settings::lighting::skybox::enabled && settings::lighting::skybox::skybox_bk.empty() && settings::lighting::skybox::preset_index >= 0 && settings::lighting::skybox::preset_index < 10)
					{
						const auto& preset = PRESETS[settings::lighting::skybox::preset_index + 1];
						settings::lighting::skybox::skybox_bk = preset.bk ? preset.bk : "";
						settings::lighting::skybox::skybox_dn = preset.dn ? preset.dn : "";
						settings::lighting::skybox::skybox_ft = preset.ft ? preset.ft : "";
						settings::lighting::skybox::skybox_lf = preset.lf ? preset.lf : "";
						settings::lighting::skybox::skybox_rt = preset.rt ? preset.rt : "";
						settings::lighting::skybox::skybox_up = preset.up ? preset.up : "";
						settings::lighting::skybox::apply_skybox = true;
					}

					if (settings::lighting::skybox::enabled && m_cached_sky.address != 0)
					{
						auto sky = m_cached_sky.address != 0 ? m_cached_sky : get_sky_from_lighting(m_cached_lighting);
						if (sky.address == 0)
						{
							sky = find_sky_instance(m_cached_lighting);
						}
						const bool skybox_changed =
							m_last_skybox_bk != settings::lighting::skybox::skybox_bk ||
							m_last_skybox_dn != settings::lighting::skybox::skybox_dn ||
							m_last_skybox_ft != settings::lighting::skybox::skybox_ft ||
							m_last_skybox_lf != settings::lighting::skybox::skybox_lf ||
							m_last_skybox_rt != settings::lighting::skybox::skybox_rt ||
							m_last_skybox_up != settings::lighting::skybox::skybox_up;
						const bool should_apply = settings::lighting::skybox::apply_skybox || skybox_changed;

						if (should_apply)
						{
							try
							{
								if (sky.address == 0)
								{
									m_cached_sky = {};
									continue;
								}

								m_cached_sky = sky;
								apply_preset_fallback(m_cached_lighting);

								if (!settings::lighting::skybox::skybox_bk.empty())
								{
									memory->write_string(sky.address + Offsets::Sky::SkyboxBk, settings::lighting::skybox::skybox_bk);
									m_last_skybox_bk = settings::lighting::skybox::skybox_bk;
								}
								if (!settings::lighting::skybox::skybox_dn.empty())
								{
									memory->write_string(sky.address + Offsets::Sky::SkyboxDn, settings::lighting::skybox::skybox_dn);
									m_last_skybox_dn = settings::lighting::skybox::skybox_dn;
								}
								if (!settings::lighting::skybox::skybox_ft.empty())
								{
									memory->write_string(sky.address + Offsets::Sky::SkyboxFt, settings::lighting::skybox::skybox_ft);
									m_last_skybox_ft = settings::lighting::skybox::skybox_ft;
								}
								if (!settings::lighting::skybox::skybox_lf.empty())
								{
									memory->write_string(sky.address + Offsets::Sky::SkyboxLf, settings::lighting::skybox::skybox_lf);
									m_last_skybox_lf = settings::lighting::skybox::skybox_lf;
								}
								if (!settings::lighting::skybox::skybox_rt.empty())
								{
									memory->write_string(sky.address + Offsets::Sky::SkyboxRt, settings::lighting::skybox::skybox_rt);
									m_last_skybox_rt = settings::lighting::skybox::skybox_rt;
								}
								if (!settings::lighting::skybox::skybox_up.empty())
								{
									memory->write_string(sky.address + Offsets::Sky::SkyboxUp, settings::lighting::skybox::skybox_up);
									m_last_skybox_up = settings::lighting::skybox::skybox_up;
								}

								invalidate_render_view();
							}
							catch (...)
							{
							}

							settings::lighting::skybox::apply_skybox = false;
						}
					}

					if (settings::lighting::clocktime::enabled && m_cached_lighting.address != 0)
					{
						invalidate_render_view();
					}

					if (!settings::lighting::skybox::enabled)
					{
						m_last_skybox_bk.clear();
						m_last_skybox_dn.clear();
						m_last_skybox_ft.clear();
						m_last_skybox_lf.clear();
						m_last_skybox_rt.clear();
						m_last_skybox_up.clear();
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
				std::thread(skybox_thread).detach();
				initialized = true;
			}
		}
	}
}
