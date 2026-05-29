#include "wallcheck.h"

#include <iostream>
#include <vector>

#include <game/game.h>
#include <imgui/imgui.h>
#include <settings.h>

void c_wallcheck::find_valid_parts(std::vector<rbx::instance_t> instances, std::vector<rbx::primitive_t>& valid, std::int32_t depth) {
	for (rbx::instance_t child : instances) {
		std::string className = child.get_class_name();
		std::string name = child.get_name();

		if (
			className == "BasePart" ||
			className == "Part" ||
			className == "MeshPart" ||
			className == "WedgePart" ||
			className == "CornerWedgePart" ||
			className == "Ball" ||
			className == "Cylinder" ||
			className == "UnionOperation" ||
			className == "TrussPart"
			//className == "Terrain" fucky, sometimes games have a terrain covering the whole world which makes the ray never hit
			) {
			rbx::part_t part(child.address);
			rbx::primitive_t prim = part.get_primitive();
			valid.push_back(prim);

			math::vector3 center = prim.get_position();
			math::vector3 size = prim.get_size();
			math::matrix3 rotation = prim.get_rotation();
			math::cframe cf(center, rotation);

			if (size.x > 200.f || size.y > 200.f || size.z > 200.f ||
				size.x < 1.f || size.y < 1.f || size.z < 1.f) {
				continue;
			}

			rbx::obb obb(center, size, cf);
			obstacles.push_back(obb);
		}
		if (className == "Folder") {
			find_valid_parts(child.get_children<rbx::instance_t>(), valid, depth + 1);
		}
		if (className == "Model") {
			rbx::instance_t humanoid = child.find_first_child_by_class("Humanoid");
			if (humanoid.address != 0) {
				continue;
			}
			find_valid_parts(child.get_children<rbx::instance_t>(), valid, depth + 1);
		}
	}
}

bool c_wallcheck::cache_workspace() {
	if (game::datamodel.address == 0) {
		return false;
	}

	rbx::instance_t workspace = game::datamodel.find_first_child_by_class("Workspace");
	if (workspace.address == 0) {
		return false;
	}

	obstacles.clear();
	parts.clear();

	std::vector<rbx::instance_t> children = workspace.get_children<rbx::instance_t>();

	std::vector<rbx::primitive_t> valid;
	find_valid_parts(children, valid, -1);

	if (valid.empty()) {
		return false;
	}

	parts = valid;
	return true;
}

bool c_wallcheck::is_visible(const math::vector3& origin, const math::vector3& target) {
	math::vector3 dir = (target - origin).normalized();
	float distance = (target - origin).length();

	const float max_obb_size = 5.f;

	for (const rbx::obb& box : get_obstacles()) {
		float max_obb_extent = max(max(box.half_size.x, box.half_size.y), box.half_size.z);

		float dist_to_center = (box.center - origin).length();
		if (dist_to_center > distance + max_obb_extent) continue;

		if (box.intersects(origin, dir, distance)) {
			return false;
		}
	}

	return true;
}

void c_wallcheck::draw_debug() {
	return;

	const math::matrix4 view = game::visengine.get_viewmatrix();
	const math::vector2 dims = game::visengine.get_dimensions();
	ImDrawList* draw = ImGui::GetBackgroundDrawList();

	const ImU32 box_color = IM_COL32(255, 0, 255, 255);

	for (const rbx::obb& box : obstacles) {
		math::vector3 corners[8];
		
		for (int i = 0; i < 8; ++i) {
			float sign_x = ((i & 1) != 0) ? 1.0f : -1.0f;
			float sign_y = ((i & 2) != 0) ? 1.0f : -1.0f;
			float sign_z = ((i & 4) != 0) ? 1.0f : -1.0f;
			
			corners[i] = box.center 
				+ box.axes[0] * (box.half_size.x * sign_x)
				+ box.axes[1] * (box.half_size.y * sign_y)
				+ box.axes[2] * (box.half_size.z * sign_z);
		}

		math::vector2 screen_corners[8];
		bool valid_corners[8] = { false };
		int valid_count = 0;

		for (int i = 0; i < 8; ++i) {
			if (game::visengine.world_to_screen(corners[i], screen_corners[i], dims, view)) {
				valid_corners[i] = true;
				valid_count++;
			}
		}

		if (valid_count < 2) {
			continue;
		}

		const int edges[12][2] = {
			{0, 1}, {1, 3}, {3, 2}, {2, 0},
			{4, 5}, {5, 7}, {7, 6}, {6, 4},
			{0, 4}, {1, 5}, {2, 6}, {3, 7}
		};

		for (int i = 0; i < 12; ++i) {
			int idx1 = edges[i][0];
			int idx2 = edges[i][1];
			
			if (valid_corners[idx1] && valid_corners[idx2]) {
				ImVec2 p1(screen_corners[idx1].x, screen_corners[idx1].y);
				ImVec2 p2(screen_corners[idx2].x, screen_corners[idx2].y);
				draw->AddLine(p1, p2, box_color, 1.0f);
			}
		}
	}
}

////////// don't allow our internal vectors to be mutated //////////
																  //
const std::vector<rbx::primitive_t>& c_wallcheck::get_parts() {   //
	return parts;                                                 //
}                                                                 //
																  //
const std::vector<rbx::obb>& c_wallcheck::get_obstacles() {       //
	return obstacles;                                             //
}                                                                 //
																  //
////////////////////////////////////////////////////////////////////