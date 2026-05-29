#pragma once
#include <vector>

#include "obb.h"
#include <sdk/sdk.h>
#include <sdk/math/math.h>

class c_wallcheck final {
public:
	c_wallcheck() = default;
	~c_wallcheck() = default;

	bool cache_workspace();

	void find_valid_parts(std::vector<rbx::instance_t> instances, std::vector<rbx::primitive_t>& validParts, std::int32_t depth);
	bool is_visible(const math::vector3& origin, const math::vector3& target);
	void draw_debug();

	const std::vector<rbx::primitive_t>& get_parts();
	const std::vector<rbx::obb>& get_obstacles();
private:
	std::vector<rbx::primitive_t> parts;
	std::vector<rbx::obb> obstacles;
};

inline std::unique_ptr<c_wallcheck> wallcheck = std::make_unique<c_wallcheck>();