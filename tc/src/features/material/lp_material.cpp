#include "lp_material.h"

#include <thread>
#include <chrono>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <cmath>
#include <functional>

#include <memory/memory.h>
#include <sdk/offsets.h>
#include <sdk/sdk.h>
#include <game/game.h>
#include <settings.h>
#include <sdk/math/math.h>

namespace material
{
	namespace lp_material
	{
		struct MaterialCache {
			uint32_t original_color;
			int16_t original_material;
			float original_transparency;
			bool is_cached;
		};

		uint32_t ConvertColorToBGR(math::vector3 rgb) {
			auto clamp_value = [](float value) -> uint32_t {
				return static_cast<uint32_t>(fminf(fmaxf(value * 255.0f, 0.0f), 255.0f));
			};
			return (clamp_value(rgb.z) << 16) | (clamp_value(rgb.y) << 8) | clamp_value(rgb.x);
		}

		void process_children_recursive(rbx::instance_t parent_instance, std::unordered_map<std::uintptr_t, MaterialCache>& registry, uint32_t color) {
			if (parent_instance.address == 0) {
				return;
			}

			auto children = parent_instance.get_children();

			for (auto& child : children) {
				if (child.address == 0) continue;

				std::string class_descriptor = child.get_class_name();

				if (class_descriptor == "MeshPart" || class_descriptor == "Part" || class_descriptor == "BasePart") {
					rbx::part_t part{ child.address };
					std::uint64_t primitive_address = memory->read<std::uint64_t>(child.address + Offsets::BasePart::Primitive);

					if (primitive_address) {
						if (registry.find(child.address) == registry.end()) {
							MaterialCache cache_entry;
							cache_entry.original_material = memory->read<int16_t>(primitive_address + Offsets::Primitive::Material);
							cache_entry.original_color = memory->read<uint32_t>(child.address + Offsets::BasePart::Color3);
							cache_entry.is_cached = true;
							registry[child.address] = cache_entry;
						}

						memory->write<int16_t>(primitive_address + Offsets::Primitive::Material, settings::visuals::client_material_type);
						memory->write<uint32_t>(child.address + Offsets::BasePart::Color3, color);
					}
				}

				process_children_recursive(child, registry, color);
			}
		}

		void material_thread() {
			using namespace std::chrono_literals;
			static std::unordered_map<std::uintptr_t, MaterialCache> material_registry;

			while (true) {
				std::this_thread::sleep_for(std::chrono::milliseconds(200));

				try {
					if (!settings::visuals::client_material) {
						if (!material_registry.empty()) {
							for (const auto& [part_address, cached_state] : material_registry) {
								std::uint64_t primitive_address = memory->read<std::uint64_t>(part_address + Offsets::BasePart::Primitive);
								if (primitive_address) {
									memory->write<int16_t>(primitive_address + Offsets::Primitive::Material, cached_state.original_material);
								}
								memory->write<uint32_t>(part_address + Offsets::BasePart::Color3, cached_state.original_color);
							}
							material_registry.clear();
						}
						continue;
					}

					if (game::players.address == 0) {
						continue;
					}

					std::uint64_t local_player_address = memory->read<std::uint64_t>(game::players.address + Offsets::Player::LocalPlayer);
					if (local_player_address == 0) {
						continue;
					}

					rbx::player_t local_player{ local_player_address };
					rbx::model_instance_t character = local_player.get_model_instance();
					if (character.address == 0) {
						continue;
					}

					uint32_t chams_color = ConvertColorToBGR(math::vector3(
						settings::visuals::client_material_color[0],
						settings::visuals::client_material_color[1],
						settings::visuals::client_material_color[2]
					));

					process_children_recursive(character, material_registry, chams_color);
				}
				catch (...) {
				}
			}
		}

		void run() {
			static bool initialized = false;
			if (!initialized) {
				std::thread(material_thread).detach();
				initialized = true;
			}
		}
	}
}
