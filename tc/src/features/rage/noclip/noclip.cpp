#include "noclip.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <sdk/offsets.h>
#include <settings.h>

#include <thread>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
	void restore_collision_flags(const std::unordered_map<std::uint64_t, std::uint8_t>& original_flags)
	{
		for (const auto& [primitive_address, flags] : original_flags)
		{
			if (primitive_address != 0)
			{
				memory->write<std::uint8_t>(primitive_address + Offsets::PrimitiveFlags::CanCollide, flags);
			}
		}
	}
}

void rage::noclip::run()
{
	std::unordered_map<std::uint64_t, std::uint8_t> original_flags;
	bool was_enabled = false;

	while (true)
	{
		try
		{
			if (!game::datamodel.address)
			{
				if (was_enabled)
				{
					restore_collision_flags(original_flags);
					original_flags.clear();
					was_enabled = false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			cache::entity_t local;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				local = cache::cached_local_player;
			}

			if (!local.instance.address)
			{
				if (was_enabled)
				{
					restore_collision_flags(original_flags);
					original_flags.clear();
					was_enabled = false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			rbx::model_instance_t model = rbx::player_t(local.instance.address).get_model_instance();
			if (!model.address)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			if (!settings::rage::noclip)
			{
				if (was_enabled)
				{
					restore_collision_flags(original_flags);
					original_flags.clear();
					was_enabled = false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			was_enabled = true;
			for (const auto& child : model.get_children())
			{
				if (!child.address)
				{
					continue;
				}

				const std::string class_name = child.get_class_name();
				if (class_name.find("Part") == std::string::npos && class_name != "MeshPart")
				{
					continue;
				}

				rbx::part_t part(child.address);
				std::uint64_t primitive_address = memory->read<std::uint64_t>(part.address + Offsets::BasePart::Primitive);
				if (!primitive_address)
				{
					continue;
				}

				try
				{
					std::uint8_t primitive_flags = memory->read<std::uint8_t>(primitive_address + Offsets::PrimitiveFlags::CanCollide);
					if (original_flags.find(primitive_address) == original_flags.end())
					{
						original_flags[primitive_address] = primitive_flags;
					}

					primitive_flags &= ~static_cast<std::uint8_t>(0x08);
					memory->write<std::uint8_t>(primitive_address + Offsets::PrimitiveFlags::CanCollide, primitive_flags);
				}
				catch (...)
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
