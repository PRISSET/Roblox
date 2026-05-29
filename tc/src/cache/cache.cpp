#include "cache.h"
#include <thread>
#include <game/game.h>
#include <memory/memory.h>
#include <sdk/offsets.h>

void cache::run()
{
	while (true)
	{
		try
		{
			if (game::datamodel.address == 0 || game::players.address == 0)
			{
				{
					std::lock_guard<std::mutex> lock(mtx);
					cached_players.clear();
					cached_local_player = {};
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
				continue;
			}

			std::vector<rbx::player_t> players = game::players.get_children<rbx::player_t>();
			std::vector<cache::entity_t> temp_cache;
			rbx::player_t local_player_obj{ memory->read<std::uint64_t>(game::players.address + Offsets::Player::LocalPlayer) };

			rbx::instance_t workspace = game::datamodel.find_first_child_by_class("Workspace");
			if (workspace.address != 0)
			{
				game::camera = memory->read<std::uint64_t>(workspace.address + Offsets::Workspace::CurrentCamera);
			}

			cache::entity_t local_entity{};

			for (rbx::player_t& player : players)
			{
				cache::entity_t entity{};

				entity.instance = { player.address };
				entity.name = player.get_name();
				entity.display_name = player.get_display_name();

				rbx::model_instance_t model_instance = player.get_model_instance();

				for (rbx::part_t& part : model_instance.get_children<rbx::part_t>())
				{
					std::string part_class = part.get_class_name();
					if (part_class.find("Part") != std::string::npos)
					{
						entity.parts[part.get_name()] = part;
					}
				}

				entity.humanoid = { model_instance.find_first_child("Humanoid").address };
				if (entity.humanoid.address != 0)
				{
					entity.rig_type = entity.humanoid.get_rig_type();
					entity.health = memory->read<float>(entity.humanoid.address + Offsets::Humanoid::Health);
					entity.max_health = memory->read<float>(entity.humanoid.address + Offsets::Humanoid::MaxHealth);
				}

				if (local_player_obj.address != 0 && player.address == local_player_obj.address)
				{
					local_entity = entity;
				}

				temp_cache.push_back(entity);
			}

			{
				std::lock_guard<std::mutex> lock(mtx);
				cached_players = std::move(temp_cache);
				cached_local_player = local_entity;
			}
		}
		catch (...)
		{
			{
				std::lock_guard<std::mutex> lock(mtx);
				cached_players.clear();
				cached_local_player = {};
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(150));
			continue;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}
