#include "esp.h"
#include "parser/meshparser.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <clipper2/clipper.h>

#include <settings.h>
#include <game/game.h>
#include <cache/cache.h>
#include <cache/custom_entities/custom_entities.h>
#include <cache/bodyparts/bodyparts.h>
#include <render/render.h>
#include <memory/memory.h>
#include <sdk/offsets.h>
#include <wallcheck/wallcheck.h>
#include "../../font/config/font_config.h"
#include <features/avatarmanager/avatarmanager.h>

namespace helper
{
	__forceinline ImU32 apply_opacity(ImU32 color, float opacity)
	{
		ImU32 r = (color >> 0) & 0xFF;
		ImU32 g = (color >> 8) & 0xFF;
		ImU32 b = (color >> 16) & 0xFF;
		ImU32 a = (color >> 24) & 0xFF;
		a = static_cast<ImU32>(a * opacity);
		return IM_COL32(r, g, b, a);
	}

	__forceinline ImU32 color_with_opacity(float r, float g, float b, float a, float opacity)
	{
		return IM_COL32(
			static_cast<int>(r * 255.f),
			static_cast<int>(g * 255.f),
			static_cast<int>(b * 255.f),
			static_cast<int>(a * opacity * 255.f)
		);
	}

	__forceinline void draw_text_outlined(ImDrawList* draw, ImFont* font, float font_size, ImVec2 pos, ImU32 col, const char* text_begin, const char* text_end = nullptr)
	{
		pos.x = roundf(pos.x);
		pos.y = roundf(pos.y);

		ImU32 outline_col = IM_COL32(0, 0, 0, 255);

		ImDrawListFlags old_flags = draw->Flags;
		draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;

		const char* text_end_ptr = text_end ? text_end : text_begin + std::strlen(text_begin);
		int text_len = static_cast<int>(text_end_ptr - text_begin);
		
		if (text_len == 0)
			return;

		float current_x = pos.x;
		for (int i = 0; i < text_len; i++)
		{
			const char* char_begin = text_begin + i;
			const char* char_end = text_begin + i + 1;

		for (int x = -1; x <= 1; x++)
		{
			for (int y = -1; y <= 1; y++)
			{
				if (x == 0 && y == 0)
				{
					continue;
				}
					draw->AddText(font, font_size, ImVec2(current_x + x, pos.y + y), outline_col, char_begin, char_end);
				}
			}

			float char_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, char_begin, char_end).x;
			draw->AddText(font, font_size, ImVec2(current_x, pos.y), col, char_begin, char_end);
			current_x += char_width + 1.0f;
		}

		draw->Flags = old_flags;
	}

	__forceinline void draw_text_blended(ImDrawList* draw, ImFont* font, float font_size, ImVec2 pos, ImU32 col_start, ImU32 col_end, const char* text_begin, const char* text_end = nullptr)
	{
		pos.x = roundf(pos.x);
		pos.y = roundf(pos.y);

		ImU32 outline_col = IM_COL32(0, 0, 0, 255);

		ImDrawListFlags old_flags = draw->Flags;
		draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;

		const char* text_end_ptr = text_end ? text_end : text_begin + std::strlen(text_begin);
		int text_len = static_cast<int>(text_end_ptr - text_begin);
		
		if (text_len == 0)
			return;

		float start_r = static_cast<float>((col_start >> 0) & 0xFF);
		float start_g = static_cast<float>((col_start >> 8) & 0xFF);
		float start_b = static_cast<float>((col_start >> 16) & 0xFF);
		float start_a = static_cast<float>((col_start >> 24) & 0xFF);

		float end_r = static_cast<float>((col_end >> 0) & 0xFF);
		float end_g = static_cast<float>((col_end >> 8) & 0xFF);
		float end_b = static_cast<float>((col_end >> 16) & 0xFF);
		float end_a = static_cast<float>((col_end >> 24) & 0xFF);

		float current_x = pos.x;
		for (int i = 0; i < text_len; i++)
		{
			float t = text_len > 1 ? static_cast<float>(i) / static_cast<float>(text_len - 1) : 0.0f;
			
			float r = start_r + (end_r - start_r) * t;
			float g = start_g + (end_g - start_g) * t;
			float b = start_b + (end_b - start_b) * t;
			float a = start_a + (end_a - start_a) * t;

			ImU32 char_col = IM_COL32(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), static_cast<int>(a));

			const char* char_begin = text_begin + i;
			const char* char_end = text_begin + i + 1;

			for (int x = -1; x <= 1; x++)
			{
				for (int y = -1; y <= 1; y++)
				{
					if (x == 0 && y == 0)
					{
						continue;
					}
					draw->AddText(font, font_size, ImVec2(current_x + x, pos.y + y), outline_col, char_begin, char_end);
				}
			}

			float char_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, char_begin, char_end).x;
			draw->AddText(font, font_size, ImVec2(current_x, pos.y), char_col, char_begin, char_end);
			current_x += char_width + 1.0f;
		}

		draw->Flags = old_flags;
	}

	__forceinline void box(ImVec2& c1, ImVec2& c2, ImU32 color)
	{
		c1.x = roundf(c1.x);
		c1.y = roundf(c1.y);
		c2.x = roundf(c2.x);
		c2.y = roundf(c2.y);

		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		draw->Flags &= ImDrawListFlags_AntiAliasedLines;

		ImRect rect(c1.x, c1.y, c1.x + c2.x, c1.y + c2.y);
		ImVec2 shadow = { cosf(0.f) * 2.f, sinf(0.f) * 2.f };

		draw->AddRect(rect.Min, rect.Max, IM_COL32(0, 0, 0, color >> 24));
		draw->AddRect({ rect.Min.x - 1.f, rect.Min.y - 1.f }, { rect.Max.x + 1.f, rect.Max.y + 1.f }, color);
		draw->AddRect({ rect.Min.x - 2.f, rect.Min.y - 2.f }, { rect.Max.x + 2.f, rect.Max.y + 2.f }, IM_COL32(0, 0, 0, color >> 24));
	}

	__forceinline void corner_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float thickness = 1.f)
	{
		float x1 = roundf(min.x) - 1;
		float y1 = roundf(min.y) - 1;
		float x2 = roundf(max.x) + 1;
		float y2 = roundf(max.y) + 1;

		ImU32 outline_col = IM_COL32(0, 0, 0, 255);

		float box_width = x2 - x1;
		float box_height = y2 - y1;
		float length = (std::min)(box_width * 0.3f, box_height * 0.3f);
		length = (std::max)(length, 5.f);
		length = (std::min)(length, 15.f);

		float x1_len = roundf(x1 + length);
		float y1_len = roundf(y1 + length);
		float x2_len = roundf(x2 - length);
		float y2_len = roundf(y2 - length);

		draw->AddRectFilled(ImVec2(x1 - 1.f, y1 - 1.f), ImVec2(x1_len + 1.f, y1 + thickness + 1.f), outline_col);
		draw->AddRectFilled(ImVec2(x1 - 1.f, y1 - 1.f), ImVec2(x1 + thickness + 1.f, y1_len + 1.f), outline_col);

		draw->AddRectFilled(ImVec2(x2_len - 1.f, y1 - 1.f), ImVec2(x2 + 1.f, y1 + thickness + 1.f), outline_col);
		draw->AddRectFilled(ImVec2(x2 - thickness - 1.f, y1 - 1.f), ImVec2(x2 + 1.f, y1_len + 1.f), outline_col);

		draw->AddRectFilled(ImVec2(x1 - 1.f, y2 - thickness - 1.f), ImVec2(x1_len + 1.f, y2 + 1.f), outline_col);
		draw->AddRectFilled(ImVec2(x1 - 1.f, y2_len - 1.f), ImVec2(x1 + thickness + 1.f, y2 + 1.f), outline_col);

		draw->AddRectFilled(ImVec2(x2_len - 1.f, y2 - thickness - 1.f), ImVec2(x2 + 1.f, y2 + 1.f), outline_col);
		draw->AddRectFilled(ImVec2(x2 - thickness - 1.f, y2_len - 1.f), ImVec2(x2 + 1.f, y2 + 1.f), outline_col);

		draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x1_len, y1 + thickness), col);
		draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x1 + thickness, y1_len), col);

		draw->AddRectFilled(ImVec2(x2_len, y1), ImVec2(x2, y1 + thickness), col);
		draw->AddRectFilled(ImVec2(x2 - thickness, y1), ImVec2(x2, y1_len), col);

		draw->AddRectFilled(ImVec2(x1, y2 - thickness), ImVec2(x1_len, y2), col);
		draw->AddRectFilled(ImVec2(x1, y2_len), ImVec2(x1 + thickness, y2), col);

		draw->AddRectFilled(ImVec2(x2_len, y2 - thickness), ImVec2(x2, y2), col);
		draw->AddRectFilled(ImVec2(x2 - thickness, y2_len), ImVec2(x2, y2), col);
	}

	__forceinline void outlined_polyline(ImDrawList* draw, ImVec2* points, int count, ImU32 col, float thickness, ImU32 outline_col, float outline_thickness, bool use_outline, bool closed)
	{
		if (count < 2)
			return;

		if (use_outline && outline_thickness > 0.f)
		{
			draw->AddPolyline(points, count, outline_col, closed ? ImDrawFlags_Closed : 0, outline_thickness);
		}

		draw->AddPolyline(points, count, col, closed ? ImDrawFlags_Closed : 0, thickness);
	}

	inline float cross_product_2d(const ImVec2& O, const ImVec2& A, const ImVec2& B) {
		return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
	}

	inline float distance_sq(const ImVec2& A, const ImVec2& B) {
		return (A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y);
	}

	inline std::vector<ImVec2> convex_hull(const std::vector<ImVec2>& points) {
		if (points.size() <= 3) return points;

		std::vector<ImVec2> working_points = points;

		auto it = std::min_element(working_points.begin(), working_points.end(), [](const ImVec2& a, const ImVec2& b) {
			return (a.y < b.y) || (a.y == b.y && a.x < b.x);
			});

		std::swap(working_points[0], *it);
		ImVec2 p0 = working_points[0];

		std::sort(working_points.begin() + 1, working_points.end(), [&p0](const ImVec2& a, const ImVec2& b) {
			float cross = cross_product_2d(p0, a, b);
			return (cross > 0) || (cross == 0 && distance_sq(p0, a) < distance_sq(p0, b));
			});

		std::vector<ImVec2> hull;
		hull.push_back(working_points[0]);
		hull.push_back(working_points[1]);

		for (size_t i = 2; i < working_points.size(); i++) {
			while (hull.size() > 1 && cross_product_2d(hull[hull.size() - 2], hull.back(), working_points[i]) <= 0) {
				hull.pop_back();
			}
			hull.push_back(working_points[i]);
		}

		return hull;
	}
}

struct render_cache_entry
{
	struct part_entry_t
	{
		math::vector3 last_pos{};
		math::matrix3 last_rot{};
		math::vector3 last_size{};
		math::matrix4 last_view{};
		math::vector2 last_dims{};
		uint64_t frame_updated = 0;
		bool has_cached_projection = false;
		std::vector<ImVec2> cached_projected_points;
		std::vector<ImVec2> hull;
	};

	std::unordered_map<std::string, part_entry_t> parts;
	uint64_t last_mesh_update_frame = 0;
	uint64_t last_union_frame = 0;
	bool has_valid_union = false;
	Clipper2Lib::Paths64 cached_union_result;
};

static std::unordered_map<uint64_t, render_cache_entry> g_render_cache;

struct health_smooth_state_t
{
	float smooth_health = 0.0f;
	float smooth_armor = 0.0f;
};

static std::unordered_map<uint64_t, health_smooth_state_t> g_health_smooth_states;

struct esp_fade_state_t
{
	float box_opacity = 0.0f;
	float box_fill_opacity = 0.0f;
	float name_opacity = 0.0f;
	float healthbar_opacity = 0.0f;
	float health_percent_opacity = 0.0f;
	float armorbar_opacity = 0.0f;
	float distance_opacity = 0.0f;
	float tool_opacity = 0.0f;
	float flags_opacity = 0.0f;
	float chams_opacity = 0.0f;
	float client_box_opacity = 0.0f;
	float client_box_fill_opacity = 0.0f;
	float client_name_opacity = 0.0f;
	float client_healthbar_opacity = 0.0f;
	float client_health_percent_opacity = 0.0f;
	float client_armorbar_opacity = 0.0f;
	float client_distance_opacity = 0.0f;
	float client_tool_opacity = 0.0f;
	float client_flags_opacity = 0.0f;
	float client_chams_opacity = 0.0f;
};

static esp_fade_state_t g_esp_fade_state;

static float update_fade_opacity(float current_opacity, bool enabled, float fade_in_speed, float fade_out_speed)
{
	float target_opacity = enabled ? 1.0f : 0.0f;
	float speed = enabled ? fade_in_speed : fade_out_speed;
	float delta_time = ImGui::GetIO().DeltaTime;
	
	if (current_opacity < target_opacity)
	{
		current_opacity += speed * delta_time;
		if (current_opacity > target_opacity)
			current_opacity = target_opacity;
	}
	else if (current_opacity > target_opacity)
	{
		current_opacity -= speed * delta_time;
		if (current_opacity < target_opacity)
			current_opacity = target_opacity;
	}
	
	return current_opacity;
}

static math::vector3 visual_corners[8] =
{
	{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
	{-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}
};

static bool is_valid_r15_body_part(const std::string& part_name)
{
	return part_name == "Head" ||
	       part_name == "UpperTorso" ||
	       part_name == "LowerTorso" ||
	       part_name == "LeftUpperArm" ||
	       part_name == "LeftLowerArm" ||
	       part_name == "LeftHand" ||
	       part_name == "RightUpperArm" ||
	       part_name == "RightLowerArm" ||
	       part_name == "RightHand" ||
	       part_name == "LeftUpperLeg" ||
	       part_name == "LeftLowerLeg" ||
	       part_name == "LeftFoot" ||
	       part_name == "RightUpperLeg" ||
	       part_name == "RightLowerLeg" ||
	       part_name == "RightFoot";
}

static bool render_highlight_chams(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims,
	ImU32 fill_col, ImU32 outline_col, ImDrawList* draw)
{
	struct projected_part_t
	{
		std::string name;
		std::vector<ImVec2> points;
	};

	std::vector<projected_part_t> projected_parts;
	projected_parts.reserve(entity.parts.size());

	bool is_r15 = bodyparts::is_r15(entity);

	for (const auto& pair : entity.parts)
	{
		const std::string& part_name = pair.first;
		rbx::part_t part = pair.second;

		if (!part.address || part_name == "HumanoidRootPart")
			continue;

		if (is_r15 && !is_valid_r15_body_part(part_name))
			continue;

		rbx::primitive_t prim = part.get_primitive();
		if (!prim.address)
			continue;

		math::vector3 size = prim.get_size();
		if (size.x == 0.f && size.y == 0.f && size.z == 0.f)
			continue;

		math::vector3 pos = prim.get_position();
		math::matrix3 rot = prim.get_rotation();

		std::vector<ImVec2> projected;
		projected.reserve(8);

		for (const math::vector3& lc : visual_corners)
		{
			math::vector3 world = pos + rot * math::vector3{
				lc.x * size.x * 0.5f,
				lc.y * size.y * 0.5f,
				lc.z * size.z * 0.5f
			};

			math::vector2 screen{};
			if (game::visengine.world_to_screen(world, screen, dims, view))
			{
				if (screen.x > 0 && screen.y > 0 && screen.x < dims.x && screen.y < dims.y)
					projected.emplace_back(screen.x, screen.y);
			}
		}

		if (!projected.empty())
		{
			projected_parts.push_back({ part_name, std::move(projected) });
		}
	}

	if (projected_parts.empty())
		return false;

	ImDrawListFlags old_flags = draw->Flags;
	draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;

	for (auto& projected : projected_parts)
	{
		auto hull = helper::convex_hull(projected.points);
		if (hull.empty())
			continue;

		draw->AddConvexPolyFilled(hull.data(), static_cast<int>(hull.size()), fill_col);
		if (settings::visuals::chams_outline_enabled && (outline_col & 0xFF000000) != 0)
			draw->AddPolyline(hull.data(), static_cast<int>(hull.size()), outline_col, ImDrawFlags_Closed, 1.0f);
	}

	draw->Flags = old_flags;
	return true;
}

static bool render_cube_chams(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims,
	ImU32 fill_col, ImU32 outline_col, ImDrawList* draw)
{
	struct projected_part_t
	{
		std::string name;
		std::vector<ImVec2> points;
	};

	std::vector<projected_part_t> projected_parts;
	projected_parts.reserve(entity.parts.size());

	bool is_r15 = bodyparts::is_r15(entity);

	for (const auto& pair : entity.parts)
	{
		const std::string& part_name = pair.first;
		rbx::part_t part = pair.second;

		if (!part.address || part_name == "HumanoidRootPart")
			continue;

		if (is_r15 && !is_valid_r15_body_part(part_name))
			continue;

		rbx::primitive_t prim = part.get_primitive();
		if (!prim.address)
			continue;

		math::vector3 size = prim.get_size();
		if (size.x == 0.f && size.y == 0.f && size.z == 0.f)
			continue;

		math::vector3 pos = prim.get_position();
		math::matrix3 rot = prim.get_rotation();

		std::vector<ImVec2> projected;
		projected.reserve(8);

		for (const math::vector3& lc : visual_corners)
		{
			math::vector3 world = pos + rot * math::vector3{
				lc.x * size.x * 0.5f,
				lc.y * size.y * 0.5f,
				lc.z * size.z * 0.5f
			};

			math::vector2 screen{};
			if (game::visengine.world_to_screen(world, screen, dims, view))
			{
				if (screen.x > 0 && screen.y > 0 && screen.x < dims.x && screen.y < dims.y)
					projected.emplace_back(screen.x, screen.y);
			}
		}

		if (!projected.empty())
		{
			projected_parts.push_back({ part_name, std::move(projected) });
		}
	}

	if (projected_parts.empty())
		return false;

	Clipper2Lib::Paths64 all_parts;
	all_parts.reserve(projected_parts.size());

	for (auto& projected : projected_parts)
	{
		auto hull = helper::convex_hull(projected.points);
		if (hull.size() < 3)
			continue;

		Clipper2Lib::Path64 path;
		path.reserve(hull.size());
		for (auto& pt : hull)
			path.push_back({ static_cast<int64_t>(pt.x * 1000.0), static_cast<int64_t>(pt.y * 1000.0) });

		all_parts.push_back(path);
	}

	if (all_parts.empty())
		return false;

	auto unified_solution = Clipper2Lib::Union(all_parts, Clipper2Lib::FillRule::NonZero);

	ImDrawListFlags old_flags = draw->Flags;
	draw->Flags &= ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines);

	for (auto& sp : unified_solution)
	{
		if (sp.size() < 3)
			continue;

		std::vector<ImVec2> poly;
		poly.reserve(sp.size());
		for (auto& pt : sp)
			poly.push_back(ImVec2(pt.x / 1000.0f, pt.y / 1000.0f));

		if (settings::visuals::chams_fill_enabled)
			draw->AddConcavePolyFilled(poly.data(), static_cast<int>(poly.size()), fill_col);
		if (settings::visuals::chams_outline_enabled && (outline_col & 0xFF000000) != 0)
			draw->AddPolyline(poly.data(), static_cast<int>(poly.size()), outline_col, ImDrawFlags_Closed, 1.0f);
	}

	draw->Flags = old_flags;
	return true;
}

static bool render_mesh_chams(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims,
	ImU32 fill_col, ImU32 outline_col, ImDrawList* draw, bool fill_enabled = true)
{
	uint64_t character_address = entity.instance.address;
	if (character_address == 0)
		return false;

	static uint64_t g_frame_counter = 0;
	static uint64_t g_last_cleanup_frame = 0;

	g_frame_counter++;

	if (g_frame_counter - g_last_cleanup_frame > 600)
	{
		for (auto it = g_render_cache.begin(); it != g_render_cache.end();)
		{
			if (g_frame_counter - it->second.last_union_frame > 300)
				it = g_render_cache.erase(it);
			else
				++it;
		}
		g_last_cleanup_frame = g_frame_counter;
	}

	auto& char_cache = g_render_cache[character_address];
	if (g_frame_counter - char_cache.last_mesh_update_frame > 120)
	{
		std::unordered_map<std::string, uint64_t> part_addresses;
		for (const auto& pair : entity.parts)
		{
			if (pair.second.address != 0 && pair.first != "HumanoidRootPart")
				part_addresses[pair.first] = pair.second.address;
		}
		meshparser::get_or_create_character_cache(character_address);
		meshparser::update_character_meshes(character_address, part_addresses);
		char_cache.last_mesh_update_frame = g_frame_counter;
	}

	bool any_part_changed = false;
	bool has_any_part = false;

	bool is_r15 = bodyparts::is_r15(entity);

	for (const auto& pair : entity.parts)
	{
		const std::string& part_name = pair.first;
		rbx::part_t part = pair.second;

		if (!part.address || part_name == "HumanoidRootPart")
			continue;

		if (is_r15 && !is_valid_r15_body_part(part_name))
			continue;

		rbx::primitive_t prim(part.get_primitive());
		if (!prim.address)
			continue;

		math::vector3 pos = prim.get_position();
		math::matrix3 rot = prim.get_rotation();
		math::vector3 size = prim.get_size();

		if (size.x == 0.f && size.y == 0.f && size.z == 0.f)
			continue;

		if (size.x > 10.f || size.y > 10.f || size.z > 10.f)
			continue;

		auto& part_entry = char_cache.parts[part_name];

		float pos_delta = fabsf(pos.x - part_entry.last_pos.x) +
		                  fabsf(pos.y - part_entry.last_pos.y) +
		                  fabsf(pos.z - part_entry.last_pos.z);

		bool rot_changed = false;
		if (part_entry.has_cached_projection)
		{
			float rot_delta = 0.f;
			for (int i = 0; i < 9; i++)
			{
				rot_delta += fabsf(rot.data()[i] - part_entry.last_rot.data()[i]);
			}
			rot_changed = rot_delta > 0.01f;
		}

		float size_delta = fabsf(size.x - part_entry.last_size.x) +
		                   fabsf(size.y - part_entry.last_size.y) +
		                   fabsf(size.z - part_entry.last_size.z);

		bool view_changed = false;
		if (part_entry.has_cached_projection)
		{
			float view_delta = 0.f;
			for (int i = 0; i < 16; i++)
			{
				view_delta += fabsf(view.data()[i] - part_entry.last_view.data()[i]);
			}
			view_changed = view_delta > 0.001f;
		}

		bool dims_changed = false;
		if (part_entry.has_cached_projection)
		{
			dims_changed = (fabsf(dims.x - part_entry.last_dims.x) > 0.1f) ||
			               (fabsf(dims.y - part_entry.last_dims.y) > 0.1f);
		}

		bool needs_projection_update = !part_entry.has_cached_projection ||
		                               pos_delta > 0.02f ||
		                               rot_changed ||
		                               size_delta > 0.01f ||
		                               view_changed ||
		                               dims_changed ||
		                               (g_frame_counter - part_entry.frame_updated) > 20;

		std::vector<ImVec2> projected_points;

		if (needs_projection_update)
		{
			any_part_changed = true;
			part_entry.last_pos = pos;
			part_entry.last_rot = rot;
			part_entry.last_size = size;
			part_entry.last_view = view;
			part_entry.last_dims = dims;
			part_entry.frame_updated = g_frame_counter;

			const meshparser::parsed_mesh* mesh = meshparser::get_cached_mesh(character_address, part_name);

			if (mesh && mesh->bounds.valid && !mesh->hull_vertices.empty())
			{
				projected_points.reserve(mesh->hull_vertices.size());

				for (const auto& normalized_pos : mesh->hull_vertices)
				{
					math::vector3 scaled_pos = {
						normalized_pos.x * size.x,
						normalized_pos.y * size.y,
						normalized_pos.z * size.z
					};
					math::vector3 world_pos = pos + rot * scaled_pos;

					math::vector2 screen{};
					if (game::visengine.world_to_screen(world_pos, screen, dims, view))
					{
						if (screen.x > 0 && screen.y > 0 && screen.x < dims.x && screen.y < dims.y)
							projected_points.emplace_back(screen.x, screen.y);
					}
				}
			}
			else
			{
				projected_points.reserve(8);
				for (const math::vector3& lc : visual_corners)
				{
					math::vector3 world = pos + rot * math::vector3{
						lc.x * size.x * 0.5f,
						lc.y * size.y * 0.5f,
						lc.z * size.z * 0.5f
					};

					math::vector2 screen{};
					if (game::visengine.world_to_screen(world, screen, dims, view))
					{
						if (screen.x > 0 && screen.y > 0 && screen.x < dims.x && screen.y < dims.y)
							projected_points.emplace_back(screen.x, screen.y);
					}
				}
			}

			part_entry.cached_projected_points = projected_points;
			part_entry.has_cached_projection = true;
		}
		else
		{
			projected_points = part_entry.cached_projected_points;
		}

		bool needs_hull_update = needs_projection_update || part_entry.hull.empty();

		if (needs_hull_update)
		{
			if (projected_points.size() >= 3)
			{
				part_entry.hull = helper::convex_hull(projected_points);
				has_any_part = true;
			}
			else
			{
				part_entry.hull.clear();
			}
		}
		else if (!part_entry.hull.empty())
		{
			has_any_part = true;
		}
	}

	if (!has_any_part)
		return false;

	if (any_part_changed || !char_cache.has_valid_union || (g_frame_counter - char_cache.last_union_frame) > 20)
	{
		Clipper2Lib::Paths64 all_parts;
		all_parts.reserve(char_cache.parts.size());

		for (const auto& [name, entry] : char_cache.parts)
		{
			if (entry.hull.size() >= 3)
			{
				Clipper2Lib::Path64 path;
				path.reserve(entry.hull.size());
				for (const auto& pt : entry.hull)
					path.push_back({ static_cast<int64_t>(pt.x * 1000.0), static_cast<int64_t>(pt.y * 1000.0) });
				all_parts.push_back(std::move(path));
			}
		}

		if (!all_parts.empty())
		{
			try
			{
				char_cache.cached_union_result = Clipper2Lib::Union(all_parts, Clipper2Lib::FillRule::NonZero);
				char_cache.cached_union_result = Clipper2Lib::SimplifyPaths(char_cache.cached_union_result, 0.1);
				if (!char_cache.cached_union_result.empty())
				{
					char_cache.has_valid_union = true;
					char_cache.last_union_frame = g_frame_counter;
				}
				else
				{
					char_cache.has_valid_union = false;
				}
			}
			catch (...)
			{
				char_cache.has_valid_union = false;
			}
		}
		else
		{
			char_cache.has_valid_union = false;
			return false;
		}
	}

	if (char_cache.has_valid_union && !char_cache.cached_union_result.empty())
	{
		ImDrawListFlags old_flags = draw->Flags;
		draw->Flags &= ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines);

		for (auto& sp : char_cache.cached_union_result)
		{
			if (sp.size() < 3)
				continue;

			std::vector<ImVec2> poly;
			poly.reserve(sp.size());
			for (auto& pt : sp)
				poly.push_back(ImVec2(pt.x / 1000.0f, pt.y / 1000.0f));

			if (fill_enabled)
				draw->AddConcavePolyFilled(poly.data(), static_cast<int>(poly.size()), fill_col);
			if (settings::visuals::chams_outline_enabled && (outline_col & 0xFF000000) != 0)
				draw->AddPolyline(poly.data(), static_cast<int>(poly.size()), outline_col, ImDrawFlags_Closed, 1.0f);
		}

		draw->Flags = old_flags;
		return true;
	}

	return false;
}

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

static void draw_hitbox_3d(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims, ImDrawList* draw, std::uint64_t local_player_address)
{
	if (!settings::visuals::view_hitbox)
		return;

	if (entity.instance.address == local_player_address)
		return;

	if (settings::rage::hitbox_expander::knock_check && is_player_knocked(entity))
		return;

	rbx::part_t hitbox_part{};
	math::vector3 hitbox_pos{};
	math::vector3 hitbox_size{};
	math::matrix3 hitbox_rot{};
	bool has_hitbox = false;

	auto part_it = entity.parts.find("HumanoidRootPart");
	if (part_it != entity.parts.end() && part_it->second.address != 0)
	{
		hitbox_part = part_it->second;
		rbx::primitive_t prim = hitbox_part.get_primitive();
		if (prim.address != 0)
		{
			hitbox_pos = prim.get_position();
			hitbox_size = prim.get_size();
			hitbox_rot = prim.get_rotation();
			has_hitbox = (hitbox_size.x > 0 && hitbox_size.y > 0 && hitbox_size.z > 0);
		}
	}

	if (!has_hitbox)
		return;

	math::vector3 expanded_size
	{
		settings::rage::hitbox_expander::size_x,
		settings::rage::hitbox_expander::size_y,
		settings::rage::hitbox_expander::size_z
	};

	static const math::vector3 corners[8] =
	{
		{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
		{-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}
	};

	std::vector<ImVec2> projected_corners;
	projected_corners.reserve(8);

	for (const auto& corner : corners)
	{
		math::vector3 world = hitbox_pos + hitbox_rot * math::vector3
		{
			corner.x * expanded_size.x * 0.5f,
			corner.y * expanded_size.y * 0.5f,
			corner.z * expanded_size.z * 0.5f
		};

		math::vector2 screen{};
		if (game::visengine.world_to_screen(world, screen, dims, view))
		{
			projected_corners.emplace_back(screen.x, screen.y);
		}
		else
		{
			projected_corners.emplace_back(-1, -1);
		}
	}

	if (projected_corners.size() != 8)
		return;

	ImU32 box_color = IM_COL32(
		static_cast<int>(settings::visuals::view_hitbox_color[0] * 255.f),
		static_cast<int>(settings::visuals::view_hitbox_color[1] * 255.f),
		static_cast<int>(settings::visuals::view_hitbox_color[2] * 255.f),
		static_cast<int>(settings::visuals::view_hitbox_color[3] * 255.f)
	);

	ImDrawListFlags old_flags = draw->Flags;
	draw->Flags |= ImDrawListFlags_AntiAliasedLines;

	const int edges[12][2] =
	{
		{0, 1}, {1, 3}, {3, 2}, {2, 0},
		{4, 5}, {5, 7}, {7, 6}, {6, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	for (int i = 0; i < 12; i++)
	{
		int idx1 = edges[i][0];
		int idx2 = edges[i][1];

		if (projected_corners[idx1].x >= 0 && projected_corners[idx1].y >= 0 &&
			projected_corners[idx2].x >= 0 && projected_corners[idx2].y >= 0)
		{
			draw->AddLine(projected_corners[idx1], projected_corners[idx2], box_color, 1.0f);
		}
	}

	draw->Flags = old_flags;
}

void esp::run()
{
	g_esp_fade_state.box_opacity = update_fade_opacity(g_esp_fade_state.box_opacity, settings::visuals::box, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.box_fill_opacity = update_fade_opacity(g_esp_fade_state.box_fill_opacity, settings::visuals::box_fill, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.name_opacity = update_fade_opacity(g_esp_fade_state.name_opacity, settings::visuals::name, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.healthbar_opacity = update_fade_opacity(g_esp_fade_state.healthbar_opacity, settings::visuals::healthbar, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.health_percent_opacity = update_fade_opacity(g_esp_fade_state.health_percent_opacity, settings::visuals::health_percent, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.armorbar_opacity = update_fade_opacity(g_esp_fade_state.armorbar_opacity, settings::visuals::armorbar, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.distance_opacity = update_fade_opacity(g_esp_fade_state.distance_opacity, settings::visuals::distance, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.tool_opacity = update_fade_opacity(g_esp_fade_state.tool_opacity, settings::visuals::tool, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.flags_opacity = update_fade_opacity(g_esp_fade_state.flags_opacity, settings::visuals::flags, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.chams_opacity = update_fade_opacity(g_esp_fade_state.chams_opacity, settings::visuals::chams, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_box_opacity = update_fade_opacity(g_esp_fade_state.client_box_opacity, settings::visuals::client_box, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_box_fill_opacity = update_fade_opacity(g_esp_fade_state.client_box_fill_opacity, settings::visuals::client_box_fill, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_name_opacity = update_fade_opacity(g_esp_fade_state.client_name_opacity, settings::visuals::client_name, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_healthbar_opacity = update_fade_opacity(g_esp_fade_state.client_healthbar_opacity, settings::visuals::client_healthbar, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_health_percent_opacity = update_fade_opacity(g_esp_fade_state.client_health_percent_opacity, settings::visuals::client_health_percent, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_armorbar_opacity = update_fade_opacity(g_esp_fade_state.client_armorbar_opacity, settings::visuals::client_armorbar, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_distance_opacity = update_fade_opacity(g_esp_fade_state.client_distance_opacity, settings::visuals::client_distance, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_tool_opacity = update_fade_opacity(g_esp_fade_state.client_tool_opacity, settings::visuals::client_tool, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_flags_opacity = update_fade_opacity(g_esp_fade_state.client_flags_opacity, settings::visuals::client_flags, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_chams_opacity = update_fade_opacity(g_esp_fade_state.client_chams_opacity, settings::visuals::client_chams, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);

	if (!settings::visuals::enable_enemies && !settings::visuals::enable_client && !settings::visuals::view_hitbox)
	{
		return;
	}

	static math::vector3 corners[8] =
	{
		{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1},{1, 1, -1},
		{-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}
	};

	game::update_window_offset();

	math::vector2 dims = game::visengine.get_dimensions();
	math::matrix4 view = game::visengine.get_viewmatrix();

	std::vector<cache::entity_t> snapshot;
	cache::entity_t local_player_snapshot{};
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		snapshot = cache::cached_players;
		local_player_snapshot = cache::cached_local_player;
	}

	std::uint64_t local_player_address = 0;
	if (game::players.address != 0)
	{
		local_player_address = memory->read<std::uint64_t>(game::players.address + Offsets::Player::LocalPlayer);
	}

	static bool wallcheck_cached = false;
	if (settings::visuals::debug_wallcheck && !wallcheck_cached)
	{
		wallcheck_cached = wallcheck->cache_workspace();
	}

	math::vector3 camera_pos{};
	bool has_camera = false;
	if (settings::visuals::debug_wallcheck && game::camera != 0)
	{
		camera_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
		has_camera = true;
	}

	math::vector3 local_root_pos{};
	bool has_local_root_pos = false;
	auto local_root_it = local_player_snapshot.parts.find("HumanoidRootPart");
	if (local_root_it != local_player_snapshot.parts.end() && local_root_it->second.address != 0)
	{
		rbx::primitive_t local_prim = local_root_it->second.get_primitive();
		if (local_prim.address != 0)
		{
			local_root_pos = local_prim.get_position();
			has_local_root_pos = std::isfinite(local_root_pos.x) && std::isfinite(local_root_pos.y) && std::isfinite(local_root_pos.z);
		}
	}

	for (cache::entity_t& entity : snapshot)
	{
		bool valid = false;
		float left = FLT_MAX, top = FLT_MAX;
		float right = -FLT_MAX, bottom = -FLT_MAX;

		if (entity.instance.address == 0)
		{
			continue;
		}

		bool is_local_player = entity.instance.address == local_player_address;
		math::vector3 target_hrp_pos{};
		bool has_target_hrp = false;
		auto target_hrp_it = entity.parts.find("HumanoidRootPart");
		if (target_hrp_it != entity.parts.end() && target_hrp_it->second.address != 0)
		{
			rbx::primitive_t target_prim = target_hrp_it->second.get_primitive();
			if (target_prim.address != 0)
			{
				target_hrp_pos = target_prim.get_position();
				has_target_hrp = std::isfinite(target_hrp_pos.x) && std::isfinite(target_hrp_pos.y) && std::isfinite(target_hrp_pos.z);
			}
		}
		
		if (!settings::visuals::view_hitbox)
		{
			if (is_local_player)
			{
				if (!settings::visuals::enable_client)
				{
					continue;
				}
			}
			else
			{
				if (!settings::visuals::enable_enemies)
				{
					continue;
				}
			}
		}

		if (!is_local_player)
		{
			if (settings::visuals::teamcheck &&
				local_player_snapshot.instance.address != 0 &&
				entity.team != 0 &&
				entity.team == local_player_snapshot.team)
			{
				continue;
			}

			if (settings::visuals::distance_check && has_local_root_pos && has_target_hrp)
			{
				math::vector3 diff = {
					target_hrp_pos.x - local_root_pos.x,
					target_hrp_pos.y - local_root_pos.y,
					target_hrp_pos.z - local_root_pos.z
				};
				const float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
				if (distance > settings::visuals::max_distance)
				{
					continue;
				}
			}

			if (settings::visuals::knock_check && is_player_knocked(entity))
			{
				continue;
			}
		}


		for (auto& parts : entity.parts)
		{
			const std::string& part_name = parts.first;
			if (part_name == "HumanoidRootPart")
			{
				continue;
			}
			
			rbx::part_t part = parts.second;
			rbx::primitive_t prim = part.get_primitive();
			auto size = prim.get_size();
			auto pos = prim.get_position();
			auto rot = prim.get_rotation();

			if (size.x == 0 || size.y == 0 || size.z == 0)
			{
				continue;
			}


			for (auto& corner : corners)
			{
				math::vector3 world = pos + rot * math::vector3
				{
					corner.x * size.x * 0.5f,
					corner.y * size.y * 0.5f,
					corner.z * size.z * 0.5f
				};

				math::vector2 out{};
				if (game::visengine.world_to_screen(world, out, dims, view))
				{
					valid = true;
					left = min(left, out.x);
					top = min(top, out.y);
					right = max(right, out.x);
					bottom = max(bottom, out.y);
				}
			}
		}

		ImDrawList* draw = ImGui::GetBackgroundDrawList();

		if (!valid || left >= right || top >= bottom)
		{
			if (settings::visuals::view_hitbox)
			{
				draw_hitbox_3d(entity, view, dims, draw, local_player_address);
			}
			continue;
		}

		ImVec2 c1(left, top);
		ImVec2 c2(right - left, bottom - top);

		float opacity_multiplier = 1.0f;
		bool is_behind_wall = false;
		if (settings::visuals::debug_wallcheck && has_camera && wallcheck_cached)
		{
			math::vector3 target_pos{};
			bool has_target = false;

			auto head_it = entity.parts.find("Head");
			if (head_it != entity.parts.end() && head_it->second.address != 0)
			{
				rbx::primitive_t head_prim = head_it->second.get_primitive();
				target_pos = head_prim.get_position();
				has_target = true;
			}
			else
			{
				auto hrp_it = entity.parts.find("HumanoidRootPart");
				if (hrp_it != entity.parts.end() && hrp_it->second.address != 0)
				{
					rbx::primitive_t hrp_prim = hrp_it->second.get_primitive();
					target_pos = hrp_prim.get_position();
					has_target = true;
				}
			}

			if (has_target)
			{
				bool is_visible = wallcheck->is_visible(camera_pos, target_pos);
				if (!is_visible)
				{
					is_behind_wall = true;
					opacity_multiplier = 0.3f;
				}
			}
		}

		bool want_box = is_local_player ? settings::visuals::client_box : settings::visuals::box;
		float actual_box_left = roundf(left);
		float actual_box_right = roundf(left) + roundf(right - left);
		
		bool want_box_fill = is_local_player ? settings::visuals::client_box_fill : settings::visuals::box_fill;
		float box_fill_fade = is_local_player ? g_esp_fade_state.client_box_fill_opacity : g_esp_fade_state.box_fill_opacity;
		if (want_box_fill)
		{
			ImU32 fill_col;
			if (is_local_player)
			{
				fill_col = helper::color_with_opacity(
					settings::visuals::client_box_fill_color[0],
					settings::visuals::client_box_fill_color[1],
					settings::visuals::client_box_fill_color[2],
					settings::visuals::client_box_fill_color[3],
					opacity_multiplier * box_fill_fade
				);
			}
			else
			{
				fill_col = helper::color_with_opacity(
					settings::visuals::box_fill_color[0],
					settings::visuals::box_fill_color[1],
					settings::visuals::box_fill_color[2],
					settings::visuals::box_fill_color[3],
					opacity_multiplier * box_fill_fade
				);
			}

			ImRect fill_rect(c1.x, c1.y, c1.x + c2.x, c1.y + c2.y);
			draw->AddRectFilled(fill_rect.Min, fill_rect.Max, fill_col);
		}
		
		if (want_box)
		{
			float box_fade = is_local_player ? g_esp_fade_state.client_box_opacity : g_esp_fade_state.box_opacity;
			ImU32 box_col;
			if (is_local_player)
			{
				box_col = helper::color_with_opacity(
					settings::visuals::client_box_color[0],
					settings::visuals::client_box_color[1],
					settings::visuals::client_box_color[2],
					settings::visuals::client_box_color[3],
					opacity_multiplier * box_fade
				);
			}
			else
			{
				box_col = helper::color_with_opacity(
					settings::visuals::box_color[0],
					settings::visuals::box_color[1],
					settings::visuals::box_color[2],
					settings::visuals::box_color[3],
					opacity_multiplier * box_fade
				);
			}

			if (settings::visuals::box_type == 0)
			{
				helper::box(c1, c2, box_col);
				actual_box_left = c1.x;
				actual_box_right = c1.x + c2.x;
			}
			else
			{
				ImRect rect_bb(c1.x, c1.y, c1.x + c2.x, c1.y + c2.y);
				helper::corner_box(draw, rect_bb.Min, rect_bb.Max, box_col, 1.f);
				actual_box_left = roundf(rect_bb.Min.x);
				actual_box_right = roundf(rect_bb.Max.x);
			}
		}
		float box_center_x = left + (right - left) * 0.5f;
		float box_top = top;
		float box_bottom = bottom;
		float box_left = roundf(left);
		float box_right = roundf(right);
		float box_width = box_right - box_left;
		float box_height = box_bottom - box_top;

		ImFont* font;
		float font_size;
		if (esp_font)
		{
			if (settings::visuals::esp_font == 0)
			{
				font = esp_font_visitor;
				font_size = font_config::visitor::font_size;
			}
			else if (settings::visuals::esp_font == 1)
			{
				font = esp_font_sp7;
				font_size = font_config::sp7::font_size;
			}
			else
			{
				font = esp_font_arial;
				font_size = font_config::arial::font_size;
			}
		}
		else
		{
			font = ImGui::GetFont();
			font_size = ImGui::GetFontSize();
		}

		bool want_healthbar = is_local_player ? settings::visuals::client_healthbar : settings::visuals::healthbar;
		float healthbar_fade = is_local_player ? g_esp_fade_state.client_healthbar_opacity : g_esp_fade_state.healthbar_opacity;
		if (want_healthbar && entity.humanoid.address != 0 && entity.max_health > 0.f)
		{
			float raw_health = entity.health;
			float max_health = entity.max_health;
			
			health_smooth_state_t& state = g_health_smooth_states[entity.humanoid.address];
			state.smooth_health += (raw_health - state.smooth_health) * 0.15f;
			state.smooth_health = max(0.0f, min(state.smooth_health, max_health));
			
			float health_percent = state.smooth_health / max_health;
			if (health_percent < 0.f) health_percent = 0.f;
			if (health_percent > 1.f) health_percent = 1.f;

			float health_percent_actual = health_percent * 100.f;
			bool use_health_based = settings::visuals::health_based_healthbar;

			float color_r, color_g, color_b;
			if (use_health_based)
			{
				if (health_percent_actual > 60.f)
				{
					color_r = 0.f;
					color_g = 1.f;
					color_b = 0.f;
				}
				else if (health_percent_actual >= 50.f)
				{
					color_r = 1.f;
					color_g = 0.647f;
					color_b = 0.f;
				}
				else if (health_percent_actual <= 20.f)
				{
					color_r = 1.f;
					color_g = 0.f;
					color_b = 0.f;
				}
				else
				{
					color_r = 1.f;
					color_g = 0.647f;
					color_b = 0.f;
				}
			}
			else
			{
				if (is_local_player)
				{
					color_r = settings::visuals::client_healthbar_color[0];
					color_g = settings::visuals::client_healthbar_color[1];
					color_b = settings::visuals::client_healthbar_color[2];
				}
				else
				{
					color_r = settings::visuals::healthbar_color[0];
					color_g = settings::visuals::healthbar_color[1];
					color_b = settings::visuals::healthbar_color[2];
				}
			}

			float transparency = 255.f * opacity_multiplier * healthbar_fade;
			float health_bar_height = box_height + 2.f;
			draw->AddRectFilled(ImVec2(box_left - 7.f, box_top - 2.f), ImVec2(box_left - 3.f, box_bottom + 2.f), IM_COL32(0, 0, 0, static_cast<int>(255 * opacity_multiplier * healthbar_fade)));
			if (health_percent > 0.f)
			{
				float health_fill_height = health_bar_height * health_percent;
				ImVec2 fill_min = ImVec2(box_left - 6.f, box_bottom + 1.f - health_fill_height);
				ImVec2 fill_max = ImVec2(box_left - 4.f, box_bottom + 1.f);

					draw->AddRectFilled(fill_min, fill_max, IM_COL32(color_r * 255.f, color_g * 255.f, color_b * 255.f, static_cast<int>(transparency)));
			}
		}

		bool want_health_percent = is_local_player ? settings::visuals::client_health_percent : settings::visuals::health_percent;
		float health_percent_fade = is_local_player ? g_esp_fade_state.client_health_percent_opacity : g_esp_fade_state.health_percent_opacity;
		if (want_health_percent && entity.humanoid.address != 0 && entity.max_health > 0.f)
		{
			float raw_health = entity.health;
			float max_health = entity.max_health;
			
			health_smooth_state_t& state = g_health_smooth_states[entity.humanoid.address];
			state.smooth_health += (raw_health - state.smooth_health) * 0.15f;
			state.smooth_health = max(0.0f, min(state.smooth_health, max_health));
			
			float health_percent = state.smooth_health / max_health;
			if (health_percent < 0.f) health_percent = 0.f;
			if (health_percent > 1.f) health_percent = 1.f;

			float health_percent_actual = health_percent * 100.f;
			int health_ceil = static_cast<int>(std::ceil(health_percent_actual));

			if (health_ceil != static_cast<int>(max_health))
			{
				std::string health_text = std::to_string(health_ceil);
				ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, health_text.c_str());
				
				float health_bar_height = box_height + 2.f;
				float health_fill_height = health_bar_height * health_percent;
				float health_bar_top = box_bottom + 1.f - health_fill_height;
				
				float health_bar_left = box_left - 7.f;
				float health_bar_right = box_left - 3.f;
				float health_bar_center_x = (health_bar_left + health_bar_right) * 0.5f;
				
				ImVec2 health_text_pos = ImVec2(roundf(health_bar_center_x - text_size.x * 0.5f), health_bar_top);

				ImU32 health_text_color;
				if (is_local_player)
				{
					health_text_color = helper::color_with_opacity(
						settings::visuals::client_health_percent_color[0],
						settings::visuals::client_health_percent_color[1],
						settings::visuals::client_health_percent_color[2],
						settings::visuals::client_health_percent_color[3],
						opacity_multiplier * health_percent_fade
					);
				}
				else
				{
					health_text_color = helper::color_with_opacity(
						settings::visuals::health_percent_color[0],
						settings::visuals::health_percent_color[1],
						settings::visuals::health_percent_color[2],
						settings::visuals::health_percent_color[3],
						opacity_multiplier * health_percent_fade
					);
				}

				helper::draw_text_outlined(draw, font, font_size, health_text_pos, health_text_color, health_text.c_str());
			}
		}

		bool want_armorbar = is_local_player ? settings::visuals::client_armorbar : settings::visuals::armorbar;
		float armorbar_fade = is_local_player ? g_esp_fade_state.client_armorbar_opacity : g_esp_fade_state.armorbar_opacity;
		bool armor_bar_rendered = false;
		if (want_armorbar && entity.instance.address != 0)
		{
			float raw_armor_percent = 0.0f;
			int armor_value = -1;
			try
			{
				rbx::player_t player(entity.instance.address);
				rbx::model_instance_t model = player.get_model_instance();
				if (model.address != 0)
				{
					rbx::instance_t body_effects = model.find_first_child("BodyEffects");
					if (body_effects.address != 0)
					{
						const char* armor_candidates[] = { "Armor", "Armour", "Defense", "Defence" };
						for (const char* nm : armor_candidates)
						{
							rbx::instance_t armor_node = body_effects.find_first_child(nm);
							if (armor_node.address == 0)
								continue;
							try
							{
								armor_value = memory->read<int>(armor_node.address + Offsets::Misc::Value);
							}
							catch (...)
							{
								try
								{
									armor_value = static_cast<int>(memory->read<double>(armor_node.address + Offsets::Misc::Value));
								}
								catch (...)
								{
								}
							}
							if (armor_value >= 0)
								break;
						}
						if (armor_value >= 0)
							raw_armor_percent = std::clamp(armor_value / 100.0f, 0.0f, 1.0f);
					}
				}
			}
			catch (...)
			{
			}

			health_smooth_state_t& state = g_health_smooth_states[entity.instance.address];
			state.smooth_armor += (raw_armor_percent - state.smooth_armor) * 0.15f;
			state.smooth_armor = max(0.0f, min(state.smooth_armor, 1.0f));
			
			float armor_percent = state.smooth_armor;

			if (armor_value > 0 && armor_percent > 0.001f)
			{
				armor_bar_rendered = true;
				float bar_height = 2.0f;
				float bar_y = roundf(box_bottom) + 5.0f;
				float bar_left = actual_box_left - 1.0f;
				float bar_right = actual_box_right + 1.0f;
				float bar_width = bar_right - bar_left;
				float fill_width = bar_width * armor_percent;

				ImDrawListFlags old_flags = draw->Flags;
				draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
				draw->Flags |= ImDrawListFlags_AntiAliasedLines;

				draw->AddRectFilled(ImVec2(roundf(bar_left - 1.0f), roundf(bar_y - 1.0f)), ImVec2(roundf(bar_right + 1.0f), roundf(bar_y + bar_height + 1.0f)), IM_COL32(0, 0, 0, 255));
				draw->AddRectFilled(ImVec2(roundf(bar_left), roundf(bar_y)), ImVec2(roundf(bar_right), roundf(bar_y + bar_height)), IM_COL32(45, 45, 45, 220));

				ImU32 armor_col;
				if (is_local_player)
				{
					armor_col = helper::color_with_opacity(
						settings::visuals::client_armorbar_color[0],
						settings::visuals::client_armorbar_color[1],
						settings::visuals::client_armorbar_color[2],
						settings::visuals::client_armorbar_color[3],
						opacity_multiplier * armorbar_fade
					);
				}
				else
				{
					armor_col = helper::color_with_opacity(
						settings::visuals::armorbar_color[0],
						settings::visuals::armorbar_color[1],
						settings::visuals::armorbar_color[2],
						settings::visuals::armorbar_color[3],
						opacity_multiplier * armorbar_fade
					);
				}

				if (fill_width > 0.0f)
				{
					draw->AddRectFilled(ImVec2(roundf(bar_left), roundf(bar_y)), ImVec2(roundf(bar_left + fill_width), roundf(bar_y + bar_height)), armor_col);
				}

				draw->Flags = old_flags;
			}
		}

		bool want_name = is_local_player ? settings::visuals::client_name : settings::visuals::name;
		float name_fade = is_local_player ? g_esp_fade_state.client_name_opacity : g_esp_fade_state.name_opacity;
		if (want_name)
		{
			std::string name_to_display = (settings::visuals::name_display_type == 0) ? entity.display_name : entity.name;
			float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, name_to_display.c_str()).x;
			int text_len = static_cast<int>(name_to_display.length());
			float actual_width = text_width + (text_len > 0 ? (text_len - 1) * 1.0f : 0.0f);
			float name_x = box_center_x - actual_width * 0.5f;
			float name_y = box_top - font_size - 5.f;

			float avatar_size = font_size * 1.5f;
			float avatar_spacing = 5.0f;

			bool want_avatar = is_local_player ? settings::visuals::client_avatar : settings::visuals::avatar;
			if (want_avatar && g_avatar_manager && entity.instance.address != 0)
			{
				std::uint64_t user_id = 0;
				try
				{
					user_id = memory->read<std::uint64_t>(entity.instance.address + Offsets::Player::UserId);
				}
				catch (...)
				{
				}

				if (user_id != 0 && user_id != 0xFFFFFFFFFFFFFFFF)
				{
					g_avatar_manager->requestAvatar(user_id);
					
					ImTextureID avatar_texture = g_avatar_manager->getAvatarTexture(user_id);
					if (avatar_texture)
					{
						float avatar_x = name_x - avatar_size - avatar_spacing;
						float avatar_y = name_y + (font_size - avatar_size) * 0.5f;
						
						ImVec2 avatar_pos(avatar_x, avatar_y);
						
						draw->AddImage(avatar_texture, avatar_pos, ImVec2(avatar_pos.x + avatar_size, avatar_pos.y + avatar_size), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(255 * opacity_multiplier * name_fade)));
					}
				}
			}

			if (settings::visuals::blend)
			{
				ImU32 name_col_start = ImGui::ColorConvertFloat4ToU32(
					{
						settings::visuals::name_color_blend_start[0],
						settings::visuals::name_color_blend_start[1],
						settings::visuals::name_color_blend_start[2],
						settings::visuals::name_color_blend_start[3]
					}
				);
				ImU32 name_col_end = ImGui::ColorConvertFloat4ToU32(
					{
						settings::visuals::name_color_blend_end[0],
						settings::visuals::name_color_blend_end[1],
						settings::visuals::name_color_blend_end[2],
						settings::visuals::name_color_blend_end[3]
					}
				);
				name_col_start = helper::apply_opacity(name_col_start, opacity_multiplier * name_fade);
				name_col_end = helper::apply_opacity(name_col_end, opacity_multiplier * name_fade);

				helper::draw_text_blended(draw, font, font_size,
					ImVec2(name_x, name_y),
					name_col_start, name_col_end, name_to_display.c_str());
			}
			else
			{
				ImU32 name_col;
				if (is_local_player)
				{
					name_col = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::client_name_color[0],
							settings::visuals::client_name_color[1],
							settings::visuals::client_name_color[2],
							settings::visuals::client_name_color[3]
						}
					);
				}
				else
				{
					name_col = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color[0],
							settings::visuals::name_color[1],
							settings::visuals::name_color[2],
							settings::visuals::name_color[3]
						}
					);
				}
				name_col = helper::apply_opacity(name_col, opacity_multiplier * name_fade);

				helper::draw_text_outlined(draw, font, font_size,
					ImVec2(name_x, name_y),
					name_col, name_to_display.c_str());
			}
		}

		bool want_distance = is_local_player ? settings::visuals::client_distance : settings::visuals::distance;
		float distance_fade = is_local_player ? g_esp_fade_state.client_distance_opacity : g_esp_fade_state.distance_opacity;
		if (want_distance && has_local_root_pos && has_target_hrp)
		{
			char distance_str[32];
			math::vector3 diff = { target_hrp_pos.x - local_root_pos.x, target_hrp_pos.y - local_root_pos.y, target_hrp_pos.z - local_root_pos.z };
			float distance = std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
			
			if (settings::visuals::distance_measurement == 1)
			{
				distance *= 0.28f;
				std::snprintf(distance_str, sizeof(distance_str), "%.1fm", distance);
			}
			else
			{
				std::snprintf(distance_str, sizeof(distance_str), "%.1f", distance);
			}

			float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, distance_str).x;
			int text_len = static_cast<int>(std::strlen(distance_str));
			float actual_width = text_width + (text_len > 0 ? (text_len - 1) * 1.0f : 0.0f);

			ImU32 distance_col;
			if (is_local_player)
			{
				distance_col = ImGui::ColorConvertFloat4ToU32(
					{
						settings::visuals::client_distance_color[0],
						settings::visuals::client_distance_color[1],
						settings::visuals::client_distance_color[2],
						settings::visuals::client_distance_color[3]
					}
				);
			}
			else
			{
				distance_col = ImGui::ColorConvertFloat4ToU32(
					{
						settings::visuals::distance_color[0],
						settings::visuals::distance_color[1],
						settings::visuals::distance_color[2],
						settings::visuals::distance_color[3]
					}
				);
			}
			distance_col = helper::apply_opacity(distance_col, opacity_multiplier * distance_fade);

			float distance_y = box_bottom + 5.f;
			if (armor_bar_rendered)
			{
				distance_y = roundf(box_bottom) + 5.0f + 2.0f + 3.0f;
			}

			if (settings::visuals::blend)
			{
				ImU32 distance_col_start = ImGui::ColorConvertFloat4ToU32(
					{
						settings::visuals::name_color_blend_start[0],
						settings::visuals::name_color_blend_start[1],
						settings::visuals::name_color_blend_start[2],
						settings::visuals::name_color_blend_start[3]
					}
				);
				ImU32 distance_col_end = ImGui::ColorConvertFloat4ToU32(
					{
						settings::visuals::name_color_blend_end[0],
						settings::visuals::name_color_blend_end[1],
						settings::visuals::name_color_blend_end[2],
						settings::visuals::name_color_blend_end[3]
					}
				);
				distance_col_start = helper::apply_opacity(distance_col_start, opacity_multiplier * distance_fade);
				distance_col_end = helper::apply_opacity(distance_col_end, opacity_multiplier * distance_fade);

				helper::draw_text_blended(draw, font, font_size,
					ImVec2(box_center_x - actual_width * 0.5f, distance_y),
					distance_col_start, distance_col_end, distance_str);
			}
			else
			{
				helper::draw_text_outlined(draw, font, font_size,
					ImVec2(box_center_x - actual_width * 0.5f, distance_y),
					distance_col, distance_str);
			}
		}

		bool want_tool = is_local_player ? settings::visuals::client_tool : settings::visuals::tool;
		float tool_fade = is_local_player ? g_esp_fade_state.client_tool_opacity : g_esp_fade_state.tool_opacity;
		if (want_tool)
		{
			std::string tool_name;
			if (entity.instance.address != 0)
			{
				rbx::player_t player(entity.instance.address);
				rbx::model_instance_t model = player.get_model_instance();
				if (model.address != 0)
				{
					rbx::instance_t tool_instance = model.find_first_child_by_class("Tool");
					if (tool_instance.address != 0)
					{
						tool_name = tool_instance.get_name();
					}
				}
			}

			if (!tool_name.empty())
			{
				float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, tool_name.c_str()).x;
				int text_len = static_cast<int>(tool_name.length());
				float actual_width = text_width + (text_len > 0 ? (text_len - 1) * 1.0f : 0.0f);
				float tool_y = box_bottom + 5.f;
				
				if (armor_bar_rendered)
				{
					tool_y = roundf(box_bottom) + 5.0f + 2.0f + 3.0f;
				}
				
				if (want_distance)
				{
					tool_y += font_size + 2.f;
				}

				if (settings::visuals::blend)
				{
					ImU32 tool_col_start = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color_blend_start[0],
							settings::visuals::name_color_blend_start[1],
							settings::visuals::name_color_blend_start[2],
							settings::visuals::name_color_blend_start[3]
						}
					);
					ImU32 tool_col_end = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color_blend_end[0],
							settings::visuals::name_color_blend_end[1],
							settings::visuals::name_color_blend_end[2],
							settings::visuals::name_color_blend_end[3]
						}
					);
					tool_col_start = helper::apply_opacity(tool_col_start, opacity_multiplier * tool_fade);
					tool_col_end = helper::apply_opacity(tool_col_end, opacity_multiplier * tool_fade);

					helper::draw_text_blended(draw, font, font_size,
						ImVec2(box_center_x - actual_width * 0.5f, tool_y),
						tool_col_start, tool_col_end, tool_name.c_str());
				}
				else
				{
					ImU32 tool_col;
					if (is_local_player)
					{
						tool_col = helper::color_with_opacity(
							settings::visuals::client_tool_color[0],
							settings::visuals::client_tool_color[1],
							settings::visuals::client_tool_color[2],
							settings::visuals::client_tool_color[3],
							opacity_multiplier * tool_fade
						);
					}
					else
					{
						tool_col = helper::color_with_opacity(
							settings::visuals::tool_color[0],
							settings::visuals::tool_color[1],
							settings::visuals::tool_color[2],
							settings::visuals::tool_color[3],
							opacity_multiplier * tool_fade
						);
					}

					helper::draw_text_outlined(draw, font, font_size,
						ImVec2(box_center_x - actual_width * 0.5f, tool_y),
						tool_col, tool_name.c_str());
				}
			}
		}

		bool want_flags = is_local_player ? settings::visuals::client_flags : settings::visuals::flags;
		float flags_fade = is_local_player ? g_esp_fade_state.client_flags_opacity : g_esp_fade_state.flags_opacity;
		if (want_flags)
		{
			float current_y = box_top;
			
			if (entity.humanoid.address != 0)
			{
				rbx::humanoid_t humanoid(entity.humanoid.address);
				int humanoid_state = static_cast<int>(humanoid.get_state());
				const char* state = "Unknown";
				
				if (humanoid_state == 8)
				{
					auto head_it = entity.parts.find("Head");
					if (head_it != entity.parts.end() && head_it->second.address != 0)
					{
						rbx::primitive_t head_prim = head_it->second.get_primitive();
						if (head_prim.address != 0)
						{
							math::vector3 head_velocity = memory->read<math::vector3>(head_prim.address + Offsets::Primitive::AssemblyLinearVelocity);
							if (head_velocity.length() < 0.1f)
							{
								state = "Idle";
							}
						}
					}
				}
				
				if (std::strcmp(state, "Unknown") == 0)
				{
					static const char* states[] = { "FallingDown", "Ragdoll", "GettingUp", "Jumping", "Swimming", "Freefall", "Flying", "Landed", "Running", "Unknown", "RunningNoPhysics", "StrafingNoPhysics", "Climbing", "Seated", "PlatformStanding", "Dead", "Physics", "Unknown", "None" };
					if (humanoid_state >= 0 && humanoid_state < 19)
					{
						state = states[humanoid_state];
					}
				}
				
				char state_buffer[64];
				std::snprintf(state_buffer, sizeof(state_buffer), "[%s]", state);
				
				float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, state_buffer).x;
				float text_len = static_cast<float>(std::strlen(state_buffer));
				float actual_width = text_width + (text_len > 0 ? (text_len - 1) * 1.0f : 0.0f);
				float flag_x = box_right + 5.f;
				
				if (settings::visuals::blend)
				{
					ImU32 state_col_start = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color_blend_start[0],
							settings::visuals::name_color_blend_start[1],
							settings::visuals::name_color_blend_start[2],
							settings::visuals::name_color_blend_start[3]
						}
					);
					ImU32 state_col_end = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color_blend_end[0],
							settings::visuals::name_color_blend_end[1],
							settings::visuals::name_color_blend_end[2],
							settings::visuals::name_color_blend_end[3]
						}
					);
					state_col_start = helper::apply_opacity(state_col_start, opacity_multiplier * flags_fade);
					state_col_end = helper::apply_opacity(state_col_end, opacity_multiplier * flags_fade);

					helper::draw_text_blended(draw, font, font_size,
						ImVec2(flag_x, current_y),
						state_col_start, state_col_end, state_buffer);
				}
				else
				{
					ImU32 state_col;
					if (is_local_player)
					{
						state_col = helper::color_with_opacity(
							settings::visuals::client_flags_state_colour[0],
							settings::visuals::client_flags_state_colour[1],
							settings::visuals::client_flags_state_colour[2],
							settings::visuals::client_flags_state_colour[3],
							opacity_multiplier * flags_fade
						);
					}
					else
					{
						state_col = helper::color_with_opacity(
							settings::visuals::flags_state_colour[0],
							settings::visuals::flags_state_colour[1],
							settings::visuals::flags_state_colour[2],
							settings::visuals::flags_state_colour[3],
							opacity_multiplier * flags_fade
						);
					}
					
					helper::draw_text_outlined(draw, font, font_size, ImVec2(flag_x, current_y), state_col, state_buffer);
				}
			}
		}



		bool want_chams = is_local_player ? settings::visuals::client_chams : settings::visuals::chams;
		float chams_fade = is_local_player ? g_esp_fade_state.client_chams_opacity : g_esp_fade_state.chams_opacity;
		if (want_chams)
		{
			ImU32 fill_col;
			ImU32 outline_col;
			if (is_local_player)
			{
				fill_col = helper::color_with_opacity(
					settings::visuals::client_chams_fill_color[0],
					settings::visuals::client_chams_fill_color[1],
					settings::visuals::client_chams_fill_color[2],
					settings::visuals::client_chams_fill_color[3],
					opacity_multiplier * chams_fade
				);
				outline_col = helper::color_with_opacity(
					settings::visuals::client_chams_outline_color[0],
					settings::visuals::client_chams_outline_color[1],
					settings::visuals::client_chams_outline_color[2],
					settings::visuals::client_chams_outline_color[3],
					opacity_multiplier * chams_fade
				);
			}
			else
			{
				fill_col = helper::color_with_opacity(
					settings::visuals::chams_fill_color[0],
					settings::visuals::chams_fill_color[1],
					settings::visuals::chams_fill_color[2],
					settings::visuals::chams_fill_color[3],
					opacity_multiplier * chams_fade
				);
				outline_col = helper::color_with_opacity(
					settings::visuals::chams_outline_color[0],
					settings::visuals::chams_outline_color[1],
					settings::visuals::chams_outline_color[2],
					settings::visuals::chams_outline_color[3],
					opacity_multiplier * chams_fade
				);
			}

			if (settings::visuals::chams_type == 2)
			{
				render_mesh_chams(entity, view, dims, fill_col, outline_col, draw, settings::visuals::chams_fill_enabled);
			}
			else if (settings::visuals::chams_type == 0)
			{
				render_highlight_chams(entity, view, dims, fill_col, outline_col, draw);
			}
			else if (settings::visuals::chams_type == 1)
			{
				render_cube_chams(entity, view, dims, fill_col, outline_col, draw);
			}
		}

		draw_hitbox_3d(entity, view, dims, draw, local_player_address);
	}

	if (settings::custom_entities::show_custom_entities)
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

				cache::entity_t entity;
				entity.instance = custom_entity.instance;
				entity.name = custom_entity.name;
				entity.display_name = custom_entity.name;

				for (const auto& part_pair : custom_entity.parts)
				{
					entity.parts[part_pair.first] = part_pair.second.part;
				}

				bool valid = false;
				float left = FLT_MAX, top = FLT_MAX;
				float right = -FLT_MAX, bottom = -FLT_MAX;

				static const math::vector3 corners[8] = {
					{ -1, -1, -1 },
					{ 1, -1, -1 },
					{ -1, 1, -1 },
					{ 1, 1, -1 },
					{ -1, -1, 1 },
					{ 1, -1, 1 },
					{ -1, 1, 1 },
					{ 1, 1, 1 }
				};

				for (const auto& part_pair : custom_entity.parts)
				{
					if (!part_pair.second.part.address) continue;

					try
					{
						rbx::part_t part_copy = part_pair.second.part;
						rbx::primitive_t primitive = part_copy.get_primitive();
						if (!primitive.address) continue;

						math::vector3 pos = primitive.get_position();
						math::vector3 size = primitive.get_size();
						math::matrix3 rot = primitive.get_rotation();

						if (size.x == 0 || size.y == 0 || size.z == 0)
							continue;

						for (const auto& corner : corners)
						{
							math::vector3 world = pos + rot * math::vector3
							{
								corner.x * size.x * 0.5f,
								corner.y * size.y * 0.5f,
								corner.z * size.z * 0.5f
							};

							math::vector2 out{};
							if (game::visengine.world_to_screen(world, out, dims, view))
							{
								valid = true;
								left = min(left, out.x);
								top = min(top, out.y);
								right = max(right, out.x);
								bottom = max(bottom, out.y);
							}
						}
					}
					catch (...)
					{
						continue;
					}
				}

				if (!valid || left >= right || top >= bottom) continue;

				ImDrawList* draw = ImGui::GetBackgroundDrawList();

				if (settings::visuals::box)
				{
					ImVec2 c1 = { left, top };
					ImVec2 c2 = { right - left, bottom - top };
					ImU32 box_col = IM_COL32(
						static_cast<int>(settings::visuals::box_color[0] * 255.f),
						static_cast<int>(settings::visuals::box_color[1] * 255.f),
						static_cast<int>(settings::visuals::box_color[2] * 255.f),
						static_cast<int>(settings::visuals::box_color[3] * 255.f)
					);
					helper::box(c1, c2, box_col);
				}

				if (settings::visuals::name)
				{
					std::string name_to_display = entity.name;
					ImVec2 text_pos = { left, top - 20.f };
					ImU32 name_col = IM_COL32(
						static_cast<int>(settings::visuals::name_color[0] * 255.f),
						static_cast<int>(settings::visuals::name_color[1] * 255.f),
						static_cast<int>(settings::visuals::name_color[2] * 255.f),
						static_cast<int>(settings::visuals::name_color[3] * 255.f)
					);
					helper::draw_text_outlined(draw, nullptr, 13.f, text_pos, name_col, name_to_display.c_str());
				}

				if (settings::visuals::distance)
				{
					char distance_str[32];
					snprintf(distance_str, sizeof(distance_str), "%.0f studs", custom_entity.distance);
					ImVec2 text_pos = { left, bottom + 5.f };
					ImU32 dist_col = IM_COL32(
						static_cast<int>(settings::visuals::distance_color[0] * 255.f),
						static_cast<int>(settings::visuals::distance_color[1] * 255.f),
						static_cast<int>(settings::visuals::distance_color[2] * 255.f),
						static_cast<int>(settings::visuals::distance_color[3] * 255.f)
					);
					helper::draw_text_outlined(draw, nullptr, 13.f, text_pos, dist_col, distance_str);
				}
			}
		}
	}
}
