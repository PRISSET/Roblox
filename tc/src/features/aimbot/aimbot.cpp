#include "aimbot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <game/game.h>
#include <settings.h>
#include <sdk/math/math.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <sdk/offsets.h>
#include <menu/keybind/keybind.h>
#include <cache/bodyparts/bodyparts.h>
#include <cache/cache.h>
#include <cache/custom_entities/custom_entities.h>
#include <mutex>

static float get_world_distance_to_entity(cache::entity_t& entity, cache::entity_t& local_player);

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

static bool is_custom_entity_knocked(const settings::custom_entities::custom_entity_t& custom_entity)
{
	if (custom_entity.instance.address == 0)
		return false;

	rbx::instance_t instance(custom_entity.instance.address);
	std::string instance_class = instance.get_class_name();
	
	rbx::model_instance_t model_instance{ 0 };
	if (instance_class == "Model")
	{
		model_instance = rbx::model_instance_t(custom_entity.instance.address);
	}
	else
	{
		rbx::instance_t model_child = instance.find_first_child_by_class("Model");
		if (model_child.address == 0)
			return false;
		model_instance = rbx::model_instance_t(model_child.address);
	}

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

static bool is_jumping(const cache::entity_t& entity)
{
	if (entity.humanoid.address == 0)
		return false;

	rbx::humanoid_t humanoid(entity.humanoid.address);
	int humanoid_state = static_cast<int>(humanoid.get_state());
	
	if (humanoid_state == 3 || humanoid_state == 5)
		return true;

	auto root_it = entity.parts.find("HumanoidRootPart");
	if (root_it == entity.parts.end() || !root_it->second.address)
		return false;

	try
	{
		std::uint64_t prim_address = memory->read<std::uint64_t>(root_it->second.address + Offsets::BasePart::Primitive);
		if (!prim_address)
			return false;

		math::vector3 velocity = memory->read<math::vector3>(prim_address + Offsets::Primitive::AssemblyLinearVelocity);
		return velocity.y > 5.0f;
	}
	catch (...)
	{
		return false;
	}
}

static math::vector3 get_velocity(cache::entity_t& entity)
{
	auto root_it = entity.parts.find("HumanoidRootPart");
	if (root_it == entity.parts.end() || !root_it->second.address)
	{
		return math::vector3(0.0f, 0.0f, 0.0f);
	}

	rbx::part_t part_copy = root_it->second;
	rbx::primitive_t prim = part_copy.get_primitive();
	if (!prim.address)
	{
		return math::vector3(0.0f, 0.0f, 0.0f);
	}

	try
	{
		return memory->read<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity);
	}
	catch (...)
	{
		return math::vector3(0.0f, 0.0f, 0.0f);
	}
}

static math::vector3 apply_prediction(cache::entity_t& entity, const math::vector3& position)
{
	math::vector3 velocity = get_velocity(entity);
	
	float prediction_factor_x;
	float prediction_factor_y;
	
	if (settings::aimbot::air_prediction_enabled && is_jumping(entity))
	{
		prediction_factor_x = (20.0f - settings::aimbot::air_prediction_x) * 0.02f;
		prediction_factor_y = (20.0f - settings::aimbot::air_prediction_y) * 0.02f;
	}
	else
	{
		prediction_factor_x = (20.0f - settings::aimbot::prediction_x) * 0.02f;
		prediction_factor_y = (20.0f - settings::aimbot::prediction_y) * 0.02f;
	}
	
	math::vector3 predicted_offset;
	predicted_offset.x = velocity.x * prediction_factor_x;
	predicted_offset.y = velocity.y * prediction_factor_y;
	predicted_offset.z = 0.0f;
	
	return position + predicted_offset;
}

namespace easing {
    float linear(float t) {
        return t;
    }

    float ease_in_quad(float t) {
        return t * t;
    }

    float ease_out_quad(float t) {
        return t * (2 - t);
    }

    float ease_in_out_quad(float t) {
        return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;
    }

    float ease_in_cubic(float t) {
        return t * t * t;
    }

    float ease_out_cubic(float t) {
        return (--t) * t * t + 1;
    }

    float ease_in_out_cubic(float t) {
        return t < 0.5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1;
    }

    float ease_in_sine(float t) {
        return 1 - cos((t * 3.14159265358979323846) / 2);
    }

    float ease_out_sine(float t) {
        return sin((t * 3.14159265358979323846) / 2);
    }

    float ease_in_out_sine(float t) {
        return -(cos(3.14159265358979323846 * t) - 1) / 2;
    }
}

static float apply_smoothing_style(float smoothing_value, int smoothing_style)
{
	float t = 1.0f / (smoothing_value < 1.0f ? 1.0f : smoothing_value);
	t = (std::max)(0.01f, (std::min)(1.0f, t));

	switch (smoothing_style)
	{
	case 1: return easing::linear(t);
	case 2: return easing::ease_in_quad(t);
	case 3: return easing::ease_out_quad(t);
	case 4: return easing::ease_in_out_quad(t);
	case 5: return easing::ease_in_cubic(t);
	case 6: return easing::ease_out_cubic(t);
	case 7: return easing::ease_in_out_cubic(t);
	case 8: return easing::ease_in_sine(t);
	case 9: return easing::ease_out_sine(t);
	case 10: return easing::ease_in_out_sine(t);
	default: return t;
	}
}

static float get_distance_from_center(const math::vector2& point, const math::vector2& position)
{
	float dx{ position.x - point.x };
	float dy{ position.y - point.y };
	return std::sqrt(dx * dx + dy * dy);
}

static bool get_target_point_for_entity
(
	const cache::entity_t& entity,
	const math::matrix4& view,
	const math::vector2& dims,
	float fov,
	float& out_distance,
	math::vector2& out_screen
)
{
	POINT cursor{};
	if (!GetCursorPos(&cursor))
	{
		return false;
	}

	const math::vector2 cursor_pos{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };

	float best_distance = FLT_MAX;
	bool found = false;

	auto try_part = [&](const char* name)
		{
			auto it = entity.parts.find(name);
			if (it == entity.parts.end())
			{
				return;
			}

			rbx::part_t part{ it->second };
			if (!part.address)
			{
				return;
			}

			rbx::primitive_t primitive = part.get_primitive();
			if (!primitive.address)
			{
				return;
			}

			math::vector3 pos_3d;
			try
			{
				pos_3d = primitive.get_position();
			}
			catch (...)
			{
				return;
			}

			if (settings::aimbot::enable_prediction)
			{
				try
				{
					pos_3d = apply_prediction(const_cast<cache::entity_t&>(entity), pos_3d);
				}
				catch (...)
				{
				}
			}

			math::vector2 pos_2d{};

			if (!game::visengine.world_to_screen(pos_3d, pos_2d, dims, view))
			{
				return;
			}

			const float distance = get_distance_from_center(cursor_pos, pos_2d);

			if (settings::aimbot::use_fov && distance > fov)
			{
				return;
			}

			if (distance < best_distance)
			{
				best_distance = distance;
				out_screen = pos_2d;
				found = true;
			}
		};

	int target_part_to_use = settings::aimbot::target_part;
	if (settings::aimbot::air_part_enabled && entity.humanoid.address != 0 && is_jumping(entity))
	{
		target_part_to_use = settings::aimbot::air_part;
	}

	switch (target_part_to_use)
	{
	case 1: // Head
		try_part("Head");
		break;
	case 2: // HumanoidRootPart
		try_part("HumanoidRootPart");
		break;
	case 3: // LeftArm
	{
		auto part_names = bodyparts::get_part_names(entity, "LeftArm");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	case 4: // RightArm
	{
		auto part_names = bodyparts::get_part_names(entity, "RightArm");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	case 5: // LeftLeg
	{
		auto part_names = bodyparts::get_part_names(entity, "LeftLeg");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	case 6: // RightLeg
	{
		auto part_names = bodyparts::get_part_names(entity, "RightLeg");
		for (const auto& name : part_names)
		{
			try_part(name.c_str());
		}
		break;
	}
	default: // closest
	{
		// Try all parts in order of priority
		const char* common_parts[] = { "Head", "HumanoidRootPart" };
		for (const char* name : common_parts)
		{
			try_part(name);
		}

		// Try arms and legs with R6/R15 support
		std::vector<std::string> part_types = { "LeftArm", "RightArm", "LeftLeg", "RightLeg" };
		for (const auto& part_type : part_types)
		{
			auto part_names = bodyparts::get_part_names(entity, part_type);
			for (const auto& name : part_names)
			{
				try_part(name.c_str());
			}
		}
		break;
	}
	}

	if (!found)
	{
		return false;
	}

	out_distance = best_distance;
	return true;
}

static void mouse_aim(const math::vector2& target)
{
	POINT cursor{};
	if (!GetCursorPos(&cursor))
	{
		return;
	}

	float delta_x = target.x - static_cast<float>(cursor.x);
	float delta_y = target.y - static_cast<float>(cursor.y);
	const float distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
	
	if (distance < 3.f)
	{
		return;
	}

	if (!settings::aimbot::smoothing)
	{
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dx = static_cast<LONG>(std::round(delta_x));
		input.mi.dy = static_cast<LONG>(std::round(delta_y));
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		SendInput(1, &input, sizeof(INPUT));
		return;
	}

	float smooth_factor_x = apply_smoothing_style(settings::aimbot::smoothingx, settings::aimbot::smoothing_style);
	float smooth_factor_y = apply_smoothing_style(settings::aimbot::smoothingy, settings::aimbot::smoothing_style);

	// Apply smoothing to mouse movement
	static thread_local float remainder_x = 0.0f;
	static thread_local float remainder_y = 0.0f;
	static thread_local float last_delta_x = 0.0f;
	static thread_local float last_delta_y = 0.0f;

	if ((delta_x > 0.0f && last_delta_x < 0.0f) || (delta_x < 0.0f && last_delta_x > 0.0f))
	{
		remainder_x = 0.0f;
	}

	if ((delta_y > 0.0f && last_delta_y < 0.0f) || (delta_y < 0.0f && last_delta_y > 0.0f))
	{
		remainder_y = 0.0f;
	}

	remainder_x += delta_x * smooth_factor_x;
	remainder_y += delta_y * smooth_factor_y;

	const int move_x = static_cast<int>(std::round(remainder_x));
	const int move_y = static_cast<int>(std::round(remainder_y));

	if (move_x != 0 || move_y != 0)
	{
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dx = move_x;
		input.mi.dy = move_y;
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		SendInput(1, &input, sizeof(INPUT));

		remainder_x -= static_cast<float>(move_x);
		remainder_y -= static_cast<float>(move_y);
	}

	last_delta_x = delta_x;
	last_delta_y = delta_y;
}

static void mouse_aimbot()
{
	if (aimbot::player.instance.address == 0)
	{
		return;
	}

	math::matrix4 view = game::visengine.get_viewmatrix();
	math::vector2 dims = game::visengine.get_dimensions();

	cache::entity_t local_player_snapshot;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		local_player_snapshot = cache::cached_local_player;
	}
	
	float world_distance = get_world_distance_to_entity(aimbot::player, local_player_snapshot);
	if (world_distance == FLT_MAX)
	{
		return;
	}

	float distance{};
	math::vector2 target_screen{};
	if (get_target_point_for_entity(aimbot::player, view, dims, settings::aimbot::fov, distance, target_screen))
	{
		if (settings::aimbot::offset_enabled)
		{
			target_screen.x += settings::aimbot::offset_x;
			target_screen.y += settings::aimbot::offset_y;
		}
		mouse_aim(target_screen);
	}
}

static math::matrix3 look_at(const math::vector3& from, const math::vector3& to)
{
	math::vector3 forward = (to - from);
	float length = forward.length();
	if (length < 0.0001f)
	{
		return math::matrix3::identity();
	}
	forward = forward * (1.0f / length);

	math::vector3 world_up(0.0f, 1.0f, 0.0f);
	math::vector3 right = world_up.cross(forward);
	length = right.length();
	if (length < 0.0001f)
	{
		world_up = math::vector3(0.0f, 0.0f, 1.0f);
		right = world_up.cross(forward);
		length = right.length();
		if (length < 0.0001f)
		{
			return math::matrix3::identity();
		}
	}
	right = right * (1.0f / length);

	math::vector3 up = forward.cross(right);

	math::matrix3 result{};
	result.m[0][0] = -right.x;
	result.m[0][1] = up.x;
	result.m[0][2] = -forward.x;
	result.m[1][0] = -right.y;
	result.m[1][1] = up.y;
	result.m[1][2] = -forward.y;
	result.m[2][0] = -right.z;
	result.m[2][1] = up.z;
	result.m[2][2] = -forward.z;

	return result;
}

static math::matrix3 lerp_rotation(const math::matrix3& from, const math::matrix3& to, float t)
{
	math::vector3 from_forward = from.forward();
	math::vector3 to_forward = to.forward();
	
	math::vector3 from_right = from.right();
	math::vector3 to_right = to.right();
	
	math::vector3 from_up = from.up();
	math::vector3 to_up = to.up();
	
	math::vector3 lerped_forward = (from_forward + (to_forward - from_forward) * t).normalized();
	math::vector3 lerped_right = (from_right + (to_right - from_right) * t).normalized();
	math::vector3 lerped_up = lerped_forward.cross(lerped_right).normalized();
	lerped_right = lerped_up.cross(lerped_forward).normalized();
	
	math::matrix3 result{};
	result.m[0][0] = -lerped_right.x;
	result.m[0][1] = lerped_up.x;
	result.m[0][2] = -lerped_forward.x;
	result.m[1][0] = -lerped_right.y;
	result.m[1][1] = lerped_up.y;
	result.m[1][2] = -lerped_forward.y;
	result.m[2][0] = -lerped_right.z;
	result.m[2][1] = lerped_up.z;
	result.m[2][2] = -lerped_forward.z;
	
	return result;
}

static void camera_aimbot()
{
	if (aimbot::player.instance.address == 0)
	{
		return;
	}

	std::uint64_t workspace = memory->read<std::uint64_t>(game::datamodel.address + Offsets::DataModel::Workspace);
	if (workspace == 0)
	{
		return;
	}

	std::uint64_t camera = memory->read<std::uint64_t>(workspace + Offsets::Workspace::CurrentCamera);
	if (camera == 0)
	{
		return;
	}

	math::vector3 target_pos;
	bool found_part = false;

	int target_part_to_use = settings::aimbot::target_part;
	if (settings::aimbot::air_part_enabled && aimbot::player.humanoid.address != 0 && is_jumping(aimbot::player))
	{
		target_part_to_use = settings::aimbot::air_part;
	}

	switch (target_part_to_use)
	{
	case 1: // Head
		found_part = bodyparts::get_part_position(aimbot::player, "Head", target_pos);
		break;
	case 2: // HumanoidRootPart
		found_part = bodyparts::get_part_position(aimbot::player, "HumanoidRootPart", target_pos);
		break;
	case 3: // LeftArm
		found_part = bodyparts::get_part_position(aimbot::player, "LeftArm", target_pos);
		break;
	case 4: // RightArm
		found_part = bodyparts::get_part_position(aimbot::player, "RightArm", target_pos);
		break;
	case 5: // LeftLeg
		found_part = bodyparts::get_part_position(aimbot::player, "LeftLeg", target_pos);
		break;
	case 6: // RightLeg
		found_part = bodyparts::get_part_position(aimbot::player, "RightLeg", target_pos);
		break;
	default: // Closest - try all parts
	{
		std::vector<std::string> part_types = { "Head", "HumanoidRootPart", "LeftArm", "RightArm", "LeftLeg", "RightLeg" };
		float best_distance = FLT_MAX;
		math::vector3 best_pos;
		math::vector3 camera_pos = memory->read<math::vector3>(camera + Offsets::Camera::Position);
		
		for (const auto& part_type : part_types)
		{
			math::vector3 pos;
			if (bodyparts::get_part_position(aimbot::player, part_type, pos))
			{
				float distance = (pos - camera_pos).length();
				if (distance < best_distance)
				{
					best_distance = distance;
					best_pos = pos;
					found_part = true;
				}
			}
		}
		
		if (found_part)
		{
			target_pos = best_pos;
		}
		break;
	}
	}

	if (!found_part)
	{
		return;
	}

	math::vector3 camera_pos = memory->read<math::vector3>(camera + Offsets::Camera::Position);
	
	bool is_custom_entity = (aimbot::player.humanoid.address == 0);
	if (is_custom_entity)
	{
		std::uint64_t entity_key = aimbot::player.instance.address;
		auto prev_pos_it = aimbot::previous_positions.find(entity_key);
		if (prev_pos_it != aimbot::previous_positions.end())
		{
			math::vector3 prev_pos = prev_pos_it->second;
			math::vector3 pos_diff = target_pos - prev_pos;
			float pos_change = std::sqrt(pos_diff.x * pos_diff.x + pos_diff.y * pos_diff.y + pos_diff.z * pos_diff.z);
			if (pos_change < 0.1f)
			{
				target_pos = prev_pos + pos_diff * 0.3f;
			}
			aimbot::previous_positions[entity_key] = target_pos;
		}
		else
		{
			aimbot::previous_positions[entity_key] = target_pos;
		}
	}
	
	if (settings::aimbot::enable_prediction)
	{
		target_pos = apply_prediction(aimbot::player, target_pos);
	}
	
	math::vector3 direction = target_pos - camera_pos;
	float distance = direction.length();
	if (distance < 0.1f)
	{
		return;
	}

	if (settings::aimbot::offset_enabled)
	{
		math::matrix3 current_rot = memory->read<math::matrix3>(camera + Offsets::Camera::Rotation);
		math::vector3 right = current_rot.right();
		math::vector3 up = current_rot.up();
		
		math::matrix4 view = game::visengine.get_viewmatrix();
		math::vector2 dims = game::visengine.get_dimensions();
		
		float fov_rad = 1.0472f;
		float scale_x = distance * std::tan(fov_rad * 0.5f) * 2.0f / dims.x;
		float scale_y = distance * std::tan(fov_rad * 0.5f) * 2.0f / dims.y;
		
		target_pos = target_pos + right * (settings::aimbot::offset_x * scale_x) + up * (-settings::aimbot::offset_y * scale_y);
	}

	math::matrix3 target_rot = look_at(camera_pos, target_pos);
	math::matrix3 current_rot = memory->read<math::matrix3>(camera + Offsets::Camera::Rotation);

	math::vector3 current_forward = current_rot.forward();
	math::vector3 target_forward = target_rot.forward();

	float current_yaw = std::atan2(-current_forward.x, -current_forward.z);
	float current_pitch = std::asin(current_forward.y);

	float target_yaw = std::atan2(-target_forward.x, -target_forward.z);
	float target_pitch = std::asin(target_forward.y);

	float yaw_diff = target_yaw - current_yaw;
	if (yaw_diff > 3.14159f)
	{
		yaw_diff -= 6.28318f;
	}
	else if (yaw_diff < -3.14159f)
	{
		yaw_diff += 6.28318f;
	}

	float pitch_diff = target_pitch - current_pitch;

	float total_angle_diff = std::sqrt(yaw_diff * yaw_diff + pitch_diff * pitch_diff);
	if (total_angle_diff < 0.0015f)
	{
		return;
	}

	if (settings::aimbot::smoothing) {
		const float smooth_factor_x = apply_smoothing_style(settings::aimbot::smoothingx, settings::aimbot::smoothing_style);
		const float smooth_factor_y = apply_smoothing_style(settings::aimbot::smoothingy, settings::aimbot::smoothing_style);
		const float smoothing_factor = (std::max)(0.01f, (std::min)(1.0f, (smooth_factor_x + smooth_factor_y) * 0.5f));
		const math::matrix3 smoothed_rotation = lerp_rotation(current_rot, target_rot, smoothing_factor);
		memory->write<math::matrix3>(camera + Offsets::Camera::Rotation, smoothed_rotation);
	} else {
		memory->write<math::matrix3>(camera + Offsets::Camera::Rotation, target_rot);
	}
}

static float get_world_distance_to_entity(cache::entity_t& entity, cache::entity_t& local_player)
{
	auto local_it = local_player.parts.find("HumanoidRootPart");
	if (local_it == local_player.parts.end() || !local_it->second.address)
	{
		return FLT_MAX;
	}

	auto target_it = entity.parts.find("HumanoidRootPart");
	if (target_it == entity.parts.end() || !target_it->second.address)
	{
		return FLT_MAX;
	}

	rbx::part_t local_part_copy = local_it->second;
	rbx::part_t target_part_copy = target_it->second;
	rbx::primitive_t local_primitive = local_part_copy.get_primitive();
	rbx::primitive_t target_primitive = target_part_copy.get_primitive();
	
	if (!local_primitive.address || !target_primitive.address)
	{
		return FLT_MAX;
	}

	math::vector3 local_pos;
	math::vector3 target_pos;
	try
	{
		local_pos = local_primitive.get_position();
		target_pos = target_primitive.get_position();
	}
	catch (...)
	{
		return FLT_MAX;
	}

	return local_pos.distance(target_pos);
}


static cache::entity_t get_closest_player(const math::matrix4& view, const math::vector2& dims)
{
	cache::entity_t best_player{};
	float closest = FLT_MAX;

	std::vector<cache::entity_t> entities_snapshot;
	cache::entity_t local_player_snapshot;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		entities_snapshot = cache::cached_players;
		local_player_snapshot = cache::cached_local_player;
	}

	for (cache::entity_t& entity : entities_snapshot)
	{
		if (!entity.instance.address)
		{
			continue;
		}

		if (entity.instance.address == local_player_snapshot.instance.address)
		{
			continue;
		}

		if (settings::aimbot::teamcheck && entity.team == local_player_snapshot.team)
		{
			continue;
		}

		if (settings::aimbot::knock_check && is_player_knocked(entity))
		{
			continue;
		}

		if (settings::aimbot::health_check_enabled && entity.health < settings::aimbot::min_health)
		{
			continue;
		}

		if (local_player_snapshot.instance.address != 0)
		{
			float world_distance = get_world_distance_to_entity(entity, local_player_snapshot);
			if (world_distance == FLT_MAX)
			{
				continue;
			}
		}

		float distance{};
		math::vector2 target_screen{};
		if (!get_target_point_for_entity(entity, view, dims, settings::aimbot::fov, distance, target_screen))
		{
			continue;
		}

		if (distance < closest)
		{
			closest = distance;
			best_player = entity;
		}
	}

	std::vector<settings::custom_entities::custom_container_t> containers_snapshot;
	{
		std::lock_guard<std::mutex> lock(custom_entities::containers_mtx);
		containers_snapshot = settings::custom_entities::containers;
	}

	for (const auto& container : containers_snapshot)
	{
		if (!container.enabled) continue;

		for (const auto& custom_entity : container.entities)
		{
			if (!custom_entity.enabled) continue;
			if (!custom_entity.instance.address) continue;

			if (settings::aimbot::knock_check && is_custom_entity_knocked(custom_entity))
				continue;

			if (custom_entity.root_part.position.x == 0 && custom_entity.root_part.position.y == 0 && custom_entity.root_part.position.z == 0)
				continue;

			std::vector<math::vector3> target_parts;

			if (custom_entity.head.part.address)
			{
				math::vector3 pos = custom_entity.head.position;
				if (pos.x != 0 || pos.y != 0 || pos.z != 0)
					target_parts.push_back(pos);
			}

			if (custom_entity.root_part.part.address)
			{
				math::vector3 pos = custom_entity.root_part.position;
				if (pos.x != 0 || pos.y != 0 || pos.z != 0)
					target_parts.push_back(pos);
			}

			if (target_parts.empty())
				continue;

			int hitbox_index = settings::aimbot::target_part - 1;
			if (hitbox_index >= static_cast<int>(target_parts.size()))
				hitbox_index = 0;

			math::vector3 target_pos = target_parts[hitbox_index];

			if (isnan(target_pos.x) || isnan(target_pos.y) || isnan(target_pos.z) ||
				(target_pos.x == 0 && target_pos.y == 0 && target_pos.z == 0))
				continue;

			std::uint64_t entity_key = custom_entity.instance.address;
			auto prev_pos_it = aimbot::previous_positions.find(entity_key);
			if (prev_pos_it != aimbot::previous_positions.end())
			{
				math::vector3 prev_pos = prev_pos_it->second;
				math::vector3 pos_diff = target_pos - prev_pos;
				float pos_change = std::sqrt(pos_diff.x * pos_diff.x + pos_diff.y * pos_diff.y + pos_diff.z * pos_diff.z);
				if (pos_change < 0.01f)
				{
					target_pos = prev_pos;
				}
				else
				{
					aimbot::previous_positions[entity_key] = target_pos;
				}
			}
			else
			{
				aimbot::previous_positions[entity_key] = target_pos;
			}

			math::vector2 dimensions = game::visengine.get_dimensions();
			math::matrix4 view_matrix = game::visengine.get_viewmatrix();
			math::vector2 screen_pos;
			if (!game::visengine.world_to_screen(target_pos, screen_pos, dimensions, view_matrix))
				continue;

			if (screen_pos.x < 0 || screen_pos.y < 0 || screen_pos.x > dimensions.x || screen_pos.y > dimensions.y)
				continue;

			math::vector2 cursor_pos = { dimensions.x * 0.5f, dimensions.y * 0.5f };
			float distance_2d = sqrtf((screen_pos.x - cursor_pos.x) * (screen_pos.x - cursor_pos.x) +
				(screen_pos.y - cursor_pos.y) * (screen_pos.y - cursor_pos.y));

			if (settings::aimbot::use_fov && distance_2d > settings::aimbot::fov)
				continue;

			if (distance_2d < closest)
			{
				closest = distance_2d;
				best_player = cache::entity_t{};
				best_player.instance = custom_entity.instance;
				best_player.name = custom_entity.name;
				best_player.display_name = custom_entity.name;
				best_player.humanoid = rbx::humanoid_t{ 0 };
				best_player.rig_type = 0;
				best_player.team = 0;
				best_player.health = 0.0f;
				best_player.max_health = 0.0f;
				best_player.knocked = false;
				
				best_player.parts.clear();
				for (const auto& part_pair : custom_entity.parts)
				{
					if (part_pair.second.part.address != 0)
					{
						try
						{
							rbx::part_t part_copy = part_pair.second.part;
							rbx::primitive_t test_prim = part_copy.get_primitive();
							if (test_prim.address != 0)
							{
								best_player.parts[part_pair.first] = part_pair.second.part;
							}
						}
						catch (...)
						{
							continue;
						}
					}
				}
			}
		}
	}

	return best_player;
}


static bool is_target_valid(const cache::entity_t& entity)
{
	if (entity.instance.address == 0)
	{
		return false;
	}

	if (entity.instance.address == cache::cached_local_player.instance.address)
	{
		return false;
	}

	if (settings::aimbot::teamcheck && entity.team == cache::cached_local_player.team)
	{
		return false;
	}

	if (settings::aimbot::knock_check && is_player_knocked(entity))
	{
		return false;
	}

	if (settings::aimbot::health_check_enabled && entity.health < settings::aimbot::min_health)
	{
		return false;
	}

	return true;
}

void aimbot::run()
{
	using namespace std::chrono_literals;

	keybind::keybind_t aimbot_kb{};
	aimbot_kb.key = settings::aimbot::keybind;
	aimbot_kb.mode = static_cast<keybind::activation_mode>(settings::aimbot::activation_mode);

	for (;;)
	{
		if (!settings::aimbot::enabled)
		{
			aimbot::sticky_target = cache::entity_t{};
			Sleep(1);
			continue;
		}

		aimbot_kb.key = settings::aimbot::keybind;
		aimbot_kb.mode = static_cast<keybind::activation_mode>(settings::aimbot::activation_mode);

		if (!keybind::is_active(aimbot_kb))
		{
			aimbot::sticky_target = cache::entity_t{};
			aimbot::player = cache::entity_t{};
			Sleep(1);
			continue;
		}

		math::matrix4 view = game::visengine.get_viewmatrix();
		math::vector2 dims = game::visengine.get_dimensions();

		cache::entity_t player{};

		if (settings::aimbot::sticky_aim && aimbot::sticky_target.instance.address != 0)
		{
			cache::entity_t refreshed_target{};
			bool target_found = false;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				for (const auto& entity : cache::cached_players)
				{
					if (entity.instance.address == aimbot::sticky_target.instance.address)
					{
						refreshed_target = entity;
						target_found = true;
						break;
					}
				}
			}

			if (!target_found)
			{
				std::vector<settings::custom_entities::custom_container_t> containers_snapshot;
				{
					std::lock_guard<std::mutex> lock(custom_entities::containers_mtx);
					containers_snapshot = settings::custom_entities::containers;
				}

				for (const auto& container : containers_snapshot)
				{
					if (!container.enabled) continue;

					for (const auto& custom_entity : container.entities)
					{
						if (!custom_entity.enabled) continue;
						if (custom_entity.instance.address != aimbot::sticky_target.instance.address) continue;

						if (settings::aimbot::knock_check && is_custom_entity_knocked(custom_entity))
							continue;

						if (custom_entity.root_part.position.x == 0 && custom_entity.root_part.position.y == 0 && custom_entity.root_part.position.z == 0)
							continue;

						refreshed_target = cache::entity_t{};
						refreshed_target.instance = custom_entity.instance;
						refreshed_target.name = custom_entity.name;
						refreshed_target.display_name = custom_entity.name;
						refreshed_target.humanoid = rbx::humanoid_t{ 0 };
						refreshed_target.rig_type = 0;
						refreshed_target.team = 0;
						refreshed_target.health = 0.0f;
						refreshed_target.max_health = 0.0f;
						refreshed_target.knocked = false;

						for (const auto& part_pair : custom_entity.parts)
						{
							if (part_pair.second.part.address != 0)
							{
								try
								{
									rbx::part_t part_copy = part_pair.second.part;
									rbx::primitive_t test_prim = part_copy.get_primitive();
									if (test_prim.address != 0)
									{
										refreshed_target.parts[part_pair.first] = part_pair.second.part;
									}
								}
								catch (...)
								{
									continue;
								}
							}
						}

						target_found = true;
						break;
					}

					if (target_found)
						break;
				}
			}

			if (target_found)
			{
				if (refreshed_target.humanoid.address == 0 || is_target_valid(refreshed_target))
				{
					player = refreshed_target;
					aimbot::sticky_target = refreshed_target;
				}
				else
				{
					aimbot::sticky_target = refreshed_target;
					aimbot::player = cache::entity_t{};
					Sleep(1);
					continue;
				}
			}
			else
			{
				aimbot::sticky_target = cache::entity_t{};
				aimbot::player = cache::entity_t{};
				Sleep(1);
				continue;
			}
		}
		else
		{
			player = get_closest_player(view, dims);
			if (settings::aimbot::sticky_aim && player.instance.address != 0)
			{
				aimbot::sticky_target = player;
			}
		}
		if (player.instance.address == 0)
		{
			aimbot::player = cache::entity_t{};
			aimbot::sticky_target = cache::entity_t{};
			Sleep(1);
			continue;
		}

		aimbot::player = player;
		
		std::vector<std::uint64_t> entities_to_keep;
		entities_to_keep.push_back(player.instance.address);
		if (aimbot::sticky_target.instance.address != 0)
		{
			entities_to_keep.push_back(aimbot::sticky_target.instance.address);
		}
		
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			for (const auto& entity : cache::cached_players)
			{
				if (entity.instance.address != 0)
				{
					entities_to_keep.push_back(entity.instance.address);
				}
			}
		}
		
		{
			std::lock_guard<std::mutex> lock(custom_entities::containers_mtx);
			for (const auto& container : settings::custom_entities::containers)
			{
				for (const auto& custom_entity : container.entities)
				{
					if (custom_entity.instance.address != 0)
					{
						entities_to_keep.push_back(custom_entity.instance.address);
					}
				}
			}
		}
		
		auto it = aimbot::previous_positions.begin();
		while (it != aimbot::previous_positions.end())
		{
			bool found = false;
			for (std::uint64_t addr : entities_to_keep)
			{
				if (it->first == addr)
				{
					found = true;
					break;
				}
			}
			if (!found)
			{
				it = aimbot::previous_positions.erase(it);
			}
			else
			{
				++it;
			}
		}

		if (settings::aimbot::mode == 0)
		{
			mouse_aimbot();
		}
		else if (settings::aimbot::mode == 1)
		{
			camera_aimbot();
		}

		if (settings::aimbot::mode == 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		else if (settings::aimbot::smoothing || settings::menu::performance_mode)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}
