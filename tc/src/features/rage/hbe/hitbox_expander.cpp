#include "hitbox_expander.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <sdk/offsets.h>
#include <settings.h>

#include <thread>
#include <chrono>
#include <mutex>

static bool is_player_knocked(const cache::entity_t& player)
{
	if (player.instance.address == 0)
		return false;

	rbx::model_instance_t model_instance = rbx::player_t(player.instance.address).get_model_instance();

	if (model_instance.address == 0)
		return false;

	rbx::instance_t body_effects = model_instance.find_first_child("BodyEffects");
	if (body_effects.address == 0)
	{
		std::vector<rbx::instance_t> children = model_instance.get_children();
		for (const auto& child : children)
		{
			if (child.get_name() == "BodyEffects")
			{
				body_effects = child;
				break;
			}
		}
		if (body_effects.address == 0)
			return false;
	}

	rbx::instance_t ko = body_effects.find_first_child("K.O");
	if (ko.address == 0)
		return false;

	std::string ko_class = ko.get_class_name();
	if (ko_class != "BoolValue")
		return false;

	bool value = false;
	try {
		value = memory->read<bool>(ko.address + Offsets::Misc::Value);
	} catch (...) {
		value = false;
	}
	return value;
}

void rage::hitbox_expander::run()
{
	while (true)
	{
		try
		{
			if (!game::datamodel.address)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			if (!settings::rage::hitbox_expander::enabled)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			if (settings::rage::hitbox_expander::size_x <= 0.0f ||
				settings::rage::hitbox_expander::size_y <= 0.0f ||
				settings::rage::hitbox_expander::size_z <= 0.0f)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			std::vector<cache::entity_t> snapshot;
			std::uint64_t local_address = 0;
			std::string local_name;
			std::uint64_t local_target_address = 0;
			std::uint64_t local_primitive_address = 0;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				snapshot = cache::cached_players;
				local_address = cache::cached_local_player.instance.address;
				local_name = cache::cached_local_player.name;
				
				auto local_part_it = cache::cached_local_player.parts.find("HumanoidRootPart");
				if (local_part_it != cache::cached_local_player.parts.end() && local_part_it->second.address != 0)
				{
					local_target_address = local_part_it->second.address;
					local_primitive_address = memory->read<std::uint64_t>(local_target_address + Offsets::BasePart::Primitive);
				}
			}

			for (auto& entity : snapshot)
			{
				if (!entity.instance.address)
				{
					continue;
				}

				if ((local_address != 0 && entity.instance.address == local_address) ||
					(!local_name.empty() && entity.name == local_name))
				{
					continue;
				}

				if (settings::rage::hitbox_expander::knock_check && is_player_knocked(entity))
				{
					continue;
				}

				math::vector3 new_size = {
					settings::rage::hitbox_expander::size_x,
					settings::rage::hitbox_expander::size_y,
					settings::rage::hitbox_expander::size_z
				};

				auto part_it = entity.parts.find("HumanoidRootPart");
				if (part_it == entity.parts.end() || part_it->second.address == 0)
				{
					continue;
				}

				if (local_target_address != 0 && part_it->second.address == local_target_address)
				{
					continue;
				}

				rbx::part_t target_part = part_it->second;
				std::uint64_t primitive_address = memory->read<std::uint64_t>(target_part.address + Offsets::BasePart::Primitive);
				if (!primitive_address)
				{
					continue;
				}

				if (local_primitive_address != 0 && primitive_address == local_primitive_address)
				{
					continue;
				}

				try
				{
					memory->write<math::vector3>(primitive_address + Offsets::Primitive::Size, new_size);
					
					std::uint8_t primitive_flags = memory->read<std::uint8_t>(primitive_address + Offsets::PrimitiveFlags::CanCollide);
					primitive_flags &= ~0x08;
					memory->write<std::uint8_t>(primitive_address + Offsets::PrimitiveFlags::CanCollide, primitive_flags);
				}
				catch (const std::exception& e)
				{
					continue;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		catch (const std::exception& e)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}
