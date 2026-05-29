#include "hitsounds.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/math/math.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <sdk/offsets.h>
#include <settings.h>
#include <features/silentaim/silentaim.h>
#include <features/aimbot/aimbot.h>
#include <menu/menu.h>
#include "../../../ext/hitsounds.h"
#include <windows.h>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <mutex>
#include <memory>
#include <string>
#include <cctype>
#include <algorithm>
#include <functional>
#include <cfloat>
#include <vector>
#pragma comment(lib, "winmm.lib")

namespace
{
	struct sound_blob_t
	{
		const unsigned char* data;
		size_t size;
	};

	std::mutex g_hitsound_playback_mutex;
	std::shared_ptr<std::vector<unsigned char>> g_active_hitsound_buffer;

	sound_blob_t get_selected_hitsound()
	{
		switch (settings::rage::hitsound_type)
		{
		case 0: return { amongus, sizeof(amongus) };
		case 1: return { skeet_sound, sizeof(skeet_sound) };
		case 2: return { beep, sizeof(beep) };
		case 3: return { bonk, sizeof(bonk) };
		case 4: return { bubble, sizeof(bubble) };
		case 5: return { cod, sizeof(cod) };
		case 6: return { csgo, sizeof(csgo) };
		case 7: return { fairy, sizeof(fairy) };
		case 8: return { fatality, sizeof(fatality) };
		case 9: return { osu, sizeof(osu) };
		default: return { neverlose_sound, sizeof(neverlose_sound) };
		}
	}

	std::shared_ptr<std::vector<unsigned char>> build_scaled_wave(const unsigned char* data, size_t size, float volume)
	{
		auto buffer = std::make_shared<std::vector<unsigned char>>(data, data + size);
		if (buffer->size() < 44 || volume >= 99.9f)
		{
			return buffer;
		}

		const float gain = std::clamp(volume, 0.0f, 100.0f) / 100.0f;
		std::size_t offset = 12;
		std::size_t data_offset = 0;
		std::size_t data_size = 0;
		int bits_per_sample = 0;
		int audio_format = 0;

		while (offset + 8 <= buffer->size())
		{
			const char* chunk_id = reinterpret_cast<const char*>(buffer->data() + offset);
			const std::uint32_t chunk_size = *reinterpret_cast<const std::uint32_t*>(buffer->data() + offset + 4);
			const std::size_t chunk_data_offset = offset + 8;
			if (chunk_data_offset + chunk_size > buffer->size())
			{
				break;
			}

			if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16)
			{
				audio_format = *reinterpret_cast<const std::uint16_t*>(buffer->data() + chunk_data_offset);
				bits_per_sample = *reinterpret_cast<const std::uint16_t*>(buffer->data() + chunk_data_offset + 14);
			}
			else if (std::memcmp(chunk_id, "data", 4) == 0)
			{
				data_offset = chunk_data_offset;
				data_size = chunk_size;
				break;
			}

			offset = chunk_data_offset + chunk_size + (chunk_size & 1u);
		}

		if (data_offset == 0 || data_size == 0 || (audio_format != 1 && audio_format != 3))
		{
			return buffer;
		}

		unsigned char* pcm = buffer->data() + data_offset;
		if (bits_per_sample == 8)
		{
			for (std::size_t i = 0; i < data_size; ++i)
			{
				const int centered = static_cast<int>(pcm[i]) - 128;
				const int scaled = static_cast<int>(centered * gain);
				pcm[i] = static_cast<unsigned char>(std::clamp(scaled + 128, 0, 255));
			}
		}
		else if (bits_per_sample == 16)
		{
			for (std::size_t i = 0; i + 1 < data_size; i += 2)
			{
				std::int16_t sample = *reinterpret_cast<std::int16_t*>(pcm + i);
				const int scaled = static_cast<int>(static_cast<float>(sample) * gain);
				sample = static_cast<std::int16_t>(std::clamp(scaled, -32768, 32767));
				*reinterpret_cast<std::int16_t*>(pcm + i) = sample;
			}
		}

		return buffer;
	}
}

static void play_hitsound()
{
	const sound_blob_t sound = get_selected_hitsound();
	std::lock_guard<std::mutex> lock(g_hitsound_playback_mutex);
	g_active_hitsound_buffer = build_scaled_wave(sound.data, sound.size, settings::rage::hitsound_volume);
	PlaySoundA(
		reinterpret_cast<LPCSTR>(g_active_hitsound_buffer->data()),
		NULL,
		SND_MEMORY | SND_ASYNC | SND_NODEFAULT
	);
}

static std::string resolve_hit_part_name(const cache::entity_t& target)
{
	if (silentaim::state.target.instance.address == target.instance.address && silentaim::state.data_ready)
	{
		const math::vector3& impact = silentaim::state.target_world_pos;
		std::string best_part = "Body";
		float best_distance = FLT_MAX;

		for (const auto& [part_name, part] : target.parts)
		{
			if (!part.address)
			{
				continue;
			}

			rbx::part_t mutable_part = part;
			rbx::primitive_t primitive = mutable_part.get_primitive();
			if (!primitive.address)
			{
				continue;
			}

			const math::vector3 position = primitive.get_position();
			const float dx = impact.x - position.x;
			const float dy = impact.y - position.y;
			const float dz = impact.z - position.z;
			const float distance = dx * dx + dy * dy + dz * dz;
			if (distance < best_distance)
			{
				best_distance = distance;
				best_part = part_name;
			}
		}

		return best_part;
	}

	if (aimbot::player.instance.address == target.instance.address)
	{
		switch (settings::aimbot::target_part)
		{
		case 1: return "Head";
		case 2: return "HumanoidRootPart";
		case 3: return "LeftArm";
		case 4: return "RightArm";
		case 5: return "LeftLeg";
		case 6: return "RightLeg";
		default: return "Closest";
		}
	}

	return "Body";
}

static void handle_hitsound_health_detection(bool play_sound)
{
	static std::unordered_map<std::uint64_t, float> previous_healths;

	cache::entity_t target{};
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		
		if (silentaim::state.target.instance.address != 0)
		{
			for (const auto& entity : cache::cached_players)
			{
				if (entity.instance.address == silentaim::state.target.instance.address)
				{
					target = entity;
					break;
				}
			}
		}
		else if (aimbot::player.instance.address != 0)
		{
			for (const auto& entity : cache::cached_players)
			{
				if (entity.instance.address == aimbot::player.instance.address)
				{
					target = entity;
					break;
				}
			}
		}
	}

	if (target.instance.address == 0 || target.humanoid.address == 0)
	{
		return;
	}

	std::uint64_t entity_addr = target.instance.address;
	float current_health = target.health;

	auto it = previous_healths.find(entity_addr);
	if (it == previous_healths.end())
	{
		previous_healths[entity_addr] = current_health;
		return;
	}

	float previous_health = it->second;
	if (current_health < previous_health && (previous_health - current_health) > 0.1f)
	{
		const float damage = previous_health - current_health;
		const std::string player_name = target.display_name.empty() ? target.name : target.display_name;
		Menu::PushHitLog(player_name, damage, resolve_hit_part_name(target));
		if (play_sound)
		{
			play_hitsound();
		}
	}

	previous_healths[entity_addr] = current_health;

	for (auto it = previous_healths.begin(); it != previous_healths.end();)
	{
		if (it->first != entity_addr)
		{
			it = previous_healths.erase(it);
		}
		else
		{
			++it;
		}
	}
}

static void handle_hitsound_click_detection()
{
	static bool last_click_state = false;

	if (!settings::globals::is_game_active)
	{
		last_click_state = false;
		return;
	}

	HWND rblxWnd = FindWindowA(nullptr, "Roblox");
	if (!rblxWnd || GetForegroundWindow() != rblxWnd)
	{
		last_click_state = false;
		return;
	}

	bool is_clicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

	if (is_clicking && !last_click_state)
	{
		play_hitsound();
	}

	last_click_state = is_clicking;
}

static void handle_hitsound_ammo_detection()
{
	static int last_ammo = -1;
	static std::uint64_t last_tool_addr = 0;

	std::string tool_name;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		if (cache::cached_local_player.instance.address != 0)
		{
			rbx::player_t local_player(cache::cached_local_player.instance.address);
			rbx::model_instance_t model = local_player.get_model_instance();
			if (model.address != 0)
			{
				rbx::instance_t tool_instance = model.find_first_child_by_class("Tool");
				if (tool_instance.address != 0)
				{
					tool_name = tool_instance.get_name();
				}
			}
		}
	}

	if (tool_name.empty())
	{
		last_ammo = -1;
		last_tool_addr = 0;
		return;
	}

	try
	{
		rbx::player_t local_player(cache::cached_local_player.instance.address);
		rbx::instance_t character = local_player.get_model_instance();

		if (character.address == 0)
		{
			last_ammo = -1;
			last_tool_addr = 0;
			return;
		}

		rbx::instance_t tool = character.find_first_child(tool_name);

		if (tool.address == 0)
		{
			last_ammo = -1;
			last_tool_addr = 0;
			return;
		}

		if (tool.address != last_tool_addr)
		{
			last_tool_addr = tool.address;
			last_ammo = -1;
		}

		rbx::instance_t ammo_value(0);

		auto is_ammo_name = [](const std::string& name) -> bool
		{
			std::string lower_name = name;
			std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
			return lower_name.find("ammo") != std::string::npos ||
				lower_name.find("mag") != std::string::npos ||
				lower_name.find("clip") != std::string::npos ||
				lower_name.find("bullet") != std::string::npos ||
				lower_name.find("round") != std::string::npos ||
				lower_name == "currentammo" ||
				lower_name == "ammoleft" ||
				lower_name == "ammocount";
		};

		std::function<rbx::instance_t(rbx::instance_t, int)> find_ammo_recursive;
		find_ammo_recursive = [&](rbx::instance_t parent, int depth) -> rbx::instance_t
		{
			if (depth > 3 || parent.address == 0) return rbx::instance_t(0);

			try
			{
				auto children = parent.get_children();

				for (const auto& child : children)
				{
					try
					{
						std::string class_name = child.get_class_name();
						if (class_name == "IntValue" || class_name == "NumberValue")
						{
							std::string name = child.get_name();
							if (is_ammo_name(name))
							{
								return child;
							}
						}
					}
					catch (...) {}
				}

				for (const auto& child : children)
				{
					try
					{
						std::string class_name = child.get_class_name();
						std::string name = child.get_name();

						if (class_name == "Script" || class_name == "LocalScript" ||
							class_name == "ModuleScript" || class_name == "Folder" ||
							class_name == "Configuration" || name == "Settings" ||
							name == "Config" || name == "Data")
						{
							rbx::instance_t result = find_ammo_recursive(child, depth + 1);
							if (result.address != 0) return result;
						}
					}
					catch (...) {}
				}
			}
			catch (...) {}

			return rbx::instance_t(0);
		};

		ammo_value = tool.find_first_child("Ammo");
		if (ammo_value.address == 0) ammo_value = tool.find_first_child("CurrentAmmo");
		if (ammo_value.address == 0) ammo_value = tool.find_first_child("AmmoLeft");
		if (ammo_value.address == 0) ammo_value = tool.find_first_child("Magazine");
		if (ammo_value.address == 0) ammo_value = tool.find_first_child("Mag");

		if (ammo_value.address == 0)
		{
			rbx::instance_t script = tool.find_first_child("Script");
			if (script.address != 0)
			{
				ammo_value = script.find_first_child("Ammo");
				if (ammo_value.address == 0) ammo_value = script.find_first_child("CurrentAmmo");
			}
		}

		if (ammo_value.address == 0)
		{
			rbx::instance_t local_script = tool.find_first_child("LocalScript");
			if (local_script.address != 0)
			{
				ammo_value = local_script.find_first_child("Ammo");
				if (ammo_value.address == 0) ammo_value = local_script.find_first_child("CurrentAmmo");
			}
		}

		if (ammo_value.address == 0)
		{
			const char* container_names[] = { "Settings", "Config", "Configuration", "Data", "Values" };
			for (const char* container_name : container_names)
			{
				rbx::instance_t container = tool.find_first_child(container_name);
				if (container.address != 0)
				{
					ammo_value = container.find_first_child("Ammo");
					if (ammo_value.address == 0) ammo_value = container.find_first_child("CurrentAmmo");
					if (ammo_value.address != 0) break;
				}
			}
		}

		if (ammo_value.address == 0)
		{
			ammo_value = find_ammo_recursive(tool, 0);
		}

		if (ammo_value.address == 0)
		{
			return;
		}

		try
		{
			std::string class_name = ammo_value.get_class_name();
			if (class_name != "IntValue" && class_name != "NumberValue")
			{
				return;
			}
		}
		catch (...)
		{
			return;
		}

		int current_ammo = memory->read<int>(ammo_value.address + 0xD0);

		if (last_ammo != -1 && current_ammo < last_ammo)
		{
			play_hitsound();
		}

		last_ammo = current_ammo;
	}
	catch (...)
	{
		last_ammo = -1;
		last_tool_addr = 0;
	}
}

void rage::hitsounds_detector_thread()
{
	using namespace std::chrono_literals;

	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		const bool wants_hitsounds = settings::rage::hitsounds;
		const bool wants_hit_logs = settings::ui::hit_logs;
		if ((!wants_hitsounds && !wants_hit_logs) || !game::datamodel.address)
		{
			std::this_thread::sleep_for(50ms);
			continue;
		}

		int method = settings::rage::hitsound_method;
		if (wants_hit_logs || (wants_hitsounds && method == 0))
		{
			handle_hitsound_health_detection(wants_hitsounds && method == 0);
		}

		if (wants_hitsounds && method == 1)
		{
			if (settings::globals::is_game_active)
			{
				handle_hitsound_click_detection();
			}
		}
		else if (wants_hitsounds && method == 2)
		{
			if (settings::globals::is_game_active)
			{
				handle_hitsound_ammo_detection();
			}
		}
	}
}

