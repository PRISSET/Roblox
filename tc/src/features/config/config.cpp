#include "config.h"
#include "../../settings.h"
#include "../../menu/keybind/keybind.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <imgui/imgui.h>

namespace config
{
	std::string get_config_directory()
	{
		char appdata_path[MAX_PATH];
		if (SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata_path) == S_OK)
		{
			std::string path = std::string(appdata_path) + "\\cigar\\Configs";
			return path;
		}
		return "";
	}

	std::string get_config_path(const std::string& name)
	{
		std::string dir = get_config_directory();
		if (dir.empty())
			return "";
		
		std::string filename = name;
		if (filename.find(".json") == std::string::npos)
			filename += ".json";
		
		return dir + "\\" + filename;
	}

	bool ensure_config_directory()
	{
		std::string dir = get_config_directory();
		if (dir.empty())
			return false;
		
		try
		{
			std::filesystem::create_directories(dir);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	std::vector<config_info_t> get_config_list()
	{
		std::vector<config_info_t> configs;
		std::string dir = get_config_directory();
		
		if (dir.empty() || !std::filesystem::exists(dir))
			return configs;
		
		try
		{
			for (const auto& entry : std::filesystem::directory_iterator(dir))
			{
				if (entry.is_regular_file() && entry.path().extension() == ".json")
				{
					config_info_t info;
					info.name = entry.path().stem().string();
					info.path = entry.path().string();

					auto file_time = entry.last_write_time();
					const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
						file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
					);
					const std::time_t modified_time = std::chrono::system_clock::to_time_t(system_time);
					info.modified_time = static_cast<std::int64_t>(modified_time);

					std::tm local_tm{};
					localtime_s(&local_tm, &modified_time);
					std::ostringstream modified_ss;
					modified_ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M");
					info.modified_display = modified_ss.str();

					configs.push_back(info);
				}
			}

			std::sort(configs.begin(), configs.end(), [](const config_info_t& lhs, const config_info_t& rhs)
			{
				if (lhs.modified_time != rhs.modified_time)
					return lhs.modified_time > rhs.modified_time;
				return lhs.name < rhs.name;
			});
		}
		catch (...)
		{
		}
		
		return configs;
	}

	static void write_color(std::ostringstream& ss, const char* name, const float col[4])
	{
		ss << "    \"" << name << "\": [" << col[0] << ", " << col[1] << ", " << col[2] << ", " << col[3] << "],\n";
	}

	static std::string extract_section(const std::string& json, const char* section_name)
	{
		std::string search = "\"" + std::string(section_name) + "\": {";
		size_t start = json.find(search);
		if (start == std::string::npos)
			return "";
		
		start = json.find("{", start);
		if (start == std::string::npos)
			return "";
		
		int depth = 0;
		size_t pos = start;
		for (; pos < json.length(); pos++)
		{
			if (json[pos] == '{')
				depth++;
			else if (json[pos] == '}')
			{
				depth--;
				if (depth == 0)
					break;
			}
		}
		
		if (pos < json.length())
			return json.substr(start, pos - start + 1);
		return "";
	}

	static void read_color(const std::string& json, const char* name, float col[4])
	{
		std::string search = "\"" + std::string(name) + "\": [";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find("]", pos);
			if (end != std::string::npos)
			{
				std::string values = json.substr(pos, end - pos);
				sscanf_s(values.c_str(), "%f, %f, %f, %f", &col[0], &col[1], &col[2], &col[3]);
			}
		}
	}

	static void write_float(std::ostringstream& ss, const char* name, float value)
	{
		ss << "    \"" << name << "\": " << value << ",\n";
	}

	static float read_float(const std::string& json, const char* name, float default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find_first_of(",\n}", pos);
			if (end != std::string::npos)
			{
				std::string value = json.substr(pos, end - pos);
				return static_cast<float>(atof(value.c_str()));
			}
		}
		return default_value;
	}

	static void write_int(std::ostringstream& ss, const char* name, int value)
	{
		ss << "    \"" << name << "\": " << value << ",\n";
	}

	static int read_int(const std::string& json, const char* name, int default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find_first_of(",\n}", pos);
			if (end != std::string::npos)
			{
				std::string value = json.substr(pos, end - pos);
				return atoi(value.c_str());
			}
		}
		return default_value;
	}

	static void write_bool(std::ostringstream& ss, const char* name, bool value)
	{
		ss << "    \"" << name << "\": " << (value ? "true" : "false") << ",\n";
	}

	static bool read_bool(const std::string& json, const char* name, bool default_value)
	{
		std::string search = "\"" + std::string(name) + "\": ";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			std::string value = json.substr(pos, 4);
			if (value.find("true") == 0)
				return true;
			if (value.find("false") == 0)
				return false;
		}
		return default_value;
	}

	static void write_string(std::ostringstream& ss, const char* name, const char* value)
	{
		ss << "    \"" << name << "\": \"" << value << "\",\n";
	}

	static std::string read_string(const std::string& json, const char* name, const std::string& default_value)
	{
		std::string search = "\"" + std::string(name) + "\": \"";
		size_t pos = json.find(search);
		if (pos != std::string::npos)
		{
			pos += search.length();
			size_t end = json.find("\"", pos);
			if (end != std::string::npos)
			{
				return json.substr(pos, end - pos);
			}
		}
		return default_value;
	}

	static int key_name_to_code(const std::string& key_name)
	{
		for (int i = 0; i < keybind::key_count; i++)
		{
			if (keybind::key_names[i].name && std::string(keybind::key_names[i].name) == key_name)
			{
				return keybind::key_names[i].value;
			}
		}
		return 0;
	}

	bool save_config(const std::string& name)
	{
		if (!ensure_config_directory())
			return false;
		
		std::string path = get_config_path(name);
		if (path.empty())
			return false;
		
		std::ostringstream ss;
		ss << "{\n";
		
		ss << "  \"menu\": {\n";
		write_int(ss, "theme", settings::menu::theme);
		write_color(ss, "custom_theme_color", settings::menu::custom_theme_color);
		write_string(ss, "menu_keybind", keybind::get_key_name(settings::menu::menu_keybind));
		write_bool(ss, "watermark", settings::menu::watermark);
		write_bool(ss, "streamproof", settings::menu::streamproof);
		write_int(ss, "streamproof_keybind", settings::menu::streamproof_keybind);
		write_int(ss, "streamproof_activation_mode", settings::menu::streamproof_activation_mode);
		write_bool(ss, "show_keybinds", settings::ui::keybinds);
		write_bool(ss, "notifications", settings::ui::notifications);
		write_bool(ss, "hit_logs", settings::ui::hit_logs);
		write_bool(ss, "tooltips", settings::ui::tooltips);
		write_bool(ss, "streamproof_indicator", settings::ui::streamproof_indicator);
		write_int(ss, "keybinds_mode", settings::ui::keybinds_mode);
		write_int(ss, "overlay_preset", settings::ui::overlay_preset);
		write_float(ss, "ui_scale", settings::ui::ui_scale);
		write_int(ss, "favorites_mask", static_cast<int>(settings::ui::favorites_mask));
		write_float(ss, "watermark_pos_x", settings::ui::watermark_position[0]);
		write_float(ss, "watermark_pos_y", settings::ui::watermark_position[1]);
		write_float(ss, "keybinds_pos_x", settings::ui::keybinds_position[0]);
		write_float(ss, "keybinds_pos_y", settings::ui::keybinds_position[1]);
		write_float(ss, "hitlog_pos_x", settings::ui::hitlog_position[0]);
		write_float(ss, "hitlog_pos_y", settings::ui::hitlog_position[1]);
		write_bool(ss, "vsync", settings::menu::vsync);
		write_bool(ss, "hide_console", settings::menu::hide_console);
		write_bool(ss, "performance_mode", settings::menu::performance_mode);
		ss << "  },\n";
		
		ss << "  \"aimbot\": {\n";
		write_bool(ss, "enabled", settings::aimbot::enabled);
		write_int(ss, "keybind", settings::aimbot::keybind);
		write_int(ss, "activation_mode", settings::aimbot::activation_mode);
		write_int(ss, "mode", settings::aimbot::mode);
		write_int(ss, "target_part", settings::aimbot::target_part);
		write_float(ss, "fov", settings::aimbot::fov);
		write_bool(ss, "use_fov", settings::aimbot::use_fov);
		write_bool(ss, "draw_fov", settings::aimbot::draw_fov);
		write_color(ss, "fov_circle_colour", settings::aimbot::fov_circle_colour);
		write_color(ss, "fov_outline_colour", settings::aimbot::fov_outline_colour);
		write_bool(ss, "smoothing", settings::aimbot::smoothing);
		write_float(ss, "smoothingx", settings::aimbot::smoothingx);
		write_float(ss, "smoothingy", settings::aimbot::smoothingy);
		write_int(ss, "smoothing_style", settings::aimbot::smoothing_style);
		write_bool(ss, "enable_prediction", settings::aimbot::enable_prediction);
		write_float(ss, "prediction_x", settings::aimbot::prediction_x);
		write_float(ss, "prediction_y", settings::aimbot::prediction_y);
		write_bool(ss, "air_part_enabled", settings::aimbot::air_part_enabled);
		write_int(ss, "air_part", settings::aimbot::air_part);
		write_bool(ss, "air_prediction_enabled", settings::aimbot::air_prediction_enabled);
		write_float(ss, "air_prediction_x", settings::aimbot::air_prediction_x);
		write_float(ss, "air_prediction_y", settings::aimbot::air_prediction_y);
		write_bool(ss, "teamcheck", settings::aimbot::teamcheck);
		write_bool(ss, "knock_check", settings::aimbot::knock_check);
		write_bool(ss, "sticky_aim", settings::aimbot::sticky_aim);
		write_bool(ss, "health_check_enabled", settings::aimbot::health_check_enabled);
		write_float(ss, "min_health", settings::aimbot::min_health);
		ss << "  },\n";
		
		ss << "  \"silentaim\": {\n";
		write_bool(ss, "enabled", settings::silentaim::enabled);
		write_int(ss, "keybind", settings::silentaim::keybind);
		write_int(ss, "activation_mode", settings::silentaim::activation_mode);
		write_int(ss, "target_part", settings::silentaim::target_part);
		write_float(ss, "fov", settings::silentaim::fov);
		write_bool(ss, "use_fov", settings::silentaim::use_fov);
		write_bool(ss, "draw_fov", settings::silentaim::draw_fov);
		write_bool(ss, "lerp_fov", settings::silentaim::lerp_fov);
		write_bool(ss, "attach_fov_to_target", settings::silentaim::attach_fov_to_target);
		write_color(ss, "fov_circle_colour", settings::silentaim::fov_circle_colour);
		write_color(ss, "fov_outline_colour", settings::silentaim::fov_outline_colour);
		write_bool(ss, "enable_prediction", settings::silentaim::enable_prediction);
		write_float(ss, "prediction_x", settings::silentaim::prediction_x);
		write_float(ss, "prediction_y", settings::silentaim::prediction_y);
		write_bool(ss, "sticky_aim", settings::silentaim::sticky_aim);
		write_bool(ss, "auto_switch", settings::silentaim::auto_switch);
		write_bool(ss, "spoof_mouse", settings::silentaim::spoof_mouse);
		write_bool(ss, "use_aimbot_target", settings::silentaim::use_aimbot_target);
		write_bool(ss, "teamcheck", settings::silentaim::teamcheck);
		write_bool(ss, "guncheck", settings::silentaim::guncheck);
		write_bool(ss, "knock_check", settings::silentaim::knock_check);
		write_int(ss, "priorities", settings::silentaim::priorities);
		write_bool(ss, "health_check_enabled", settings::silentaim::health_check_enabled);
		write_float(ss, "min_health", settings::silentaim::min_health);
		write_bool(ss, "draw_target_dot", settings::silentaim::draw_target_dot);
		write_color(ss, "target_dot_color", settings::silentaim::target_dot_color);
		write_float(ss, "target_dot_size", settings::silentaim::target_dot_size);
		ss << "  },\n";
		
		ss << "  \"rage\": {\n";
		write_bool(ss, "hitsounds", settings::rage::hitsounds);
		write_int(ss, "hitsound_type", settings::rage::hitsound_type);
		write_int(ss, "hitsound_method", settings::rage::hitsound_method);
		write_float(ss, "hitsound_volume", settings::rage::hitsound_volume);
		write_bool(ss, "rapidfire", settings::rage::rapidfire);
		write_bool(ss, "noclip", settings::rage::noclip);
		ss << "    \"spin360\": {\n";
		write_bool(ss, "enabled", settings::rage::spin360::enabled);
		write_int(ss, "keybind", settings::rage::spin360::keybind);
		write_int(ss, "activation_mode", settings::rage::spin360::activation_mode);
		write_float(ss, "speed", settings::rage::spin360::speed);
		write_int(ss, "mode", settings::rage::spin360::mode);
		ss << "    }\n";
		ss << "  },\n";
		
		ss << "  \"visuals\": {\n";
		write_bool(ss, "enable_enemies", settings::visuals::enable_enemies);
		write_bool(ss, "enable_client", settings::visuals::enable_client);
		write_bool(ss, "teamcheck", settings::visuals::teamcheck);
		write_bool(ss, "distance_check", settings::visuals::distance_check);
		write_float(ss, "max_distance", settings::visuals::max_distance);
		write_bool(ss, "box", settings::visuals::box);
		write_int(ss, "box_type", settings::visuals::box_type);
		write_color(ss, "box_color", settings::visuals::box_color);
		write_bool(ss, "box_fill", settings::visuals::box_fill);
		write_color(ss, "box_fill_color", settings::visuals::box_fill_color);
		write_bool(ss, "name", settings::visuals::name);
		write_int(ss, "name_type", settings::visuals::name_type);
		write_int(ss, "name_display_type", settings::visuals::name_display_type);
		write_color(ss, "name_color", settings::visuals::name_color);
		write_bool(ss, "healthbar", settings::visuals::healthbar);
		write_color(ss, "healthbar_color", settings::visuals::healthbar_color);
		write_bool(ss, "health_based_healthbar", settings::visuals::health_based_healthbar);
		write_bool(ss, "health_percent", settings::visuals::health_percent);
		write_color(ss, "health_percent_color", settings::visuals::health_percent_color);
		write_bool(ss, "armorbar", settings::visuals::armorbar);
		write_color(ss, "armorbar_color", settings::visuals::armorbar_color);
		write_bool(ss, "distance", settings::visuals::distance);
		write_int(ss, "distance_measurement", settings::visuals::distance_measurement);
		write_color(ss, "distance_color", settings::visuals::distance_color);
		write_bool(ss, "tool", settings::visuals::tool);
		write_color(ss, "tool_color", settings::visuals::tool_color);
		write_int(ss, "esp_font", settings::visuals::esp_font);
		write_bool(ss, "local_player", settings::visuals::local_player);
		write_bool(ss, "chams", settings::visuals::chams);
		write_int(ss, "chams_type", settings::visuals::chams_type);
		write_color(ss, "chams_fill_color", settings::visuals::chams_fill_color);
		write_color(ss, "chams_outline_color", settings::visuals::chams_outline_color);
		write_bool(ss, "chams_fill_enabled", settings::visuals::chams_fill_enabled);
		write_bool(ss, "chams_outline_enabled", settings::visuals::chams_outline_enabled);
		write_bool(ss, "target_warning_icon", settings::visuals::target_warning_icon);
		write_float(ss, "target_warning_icon_size", settings::visuals::target_warning_icon_size);
		write_bool(ss, "flags", settings::visuals::flags);
		write_color(ss, "flags_state_colour", settings::visuals::flags_state_colour);
		write_bool(ss, "blend", settings::visuals::blend);
		write_color(ss, "colorblend_start", settings::visuals::name_color_blend_start);
		write_color(ss, "colorblend_end", settings::visuals::name_color_blend_end);
		write_bool(ss, "client_box", settings::visuals::client_box);
		write_color(ss, "client_box_color", settings::visuals::client_box_color);
		write_bool(ss, "client_box_fill", settings::visuals::client_box_fill);
		write_color(ss, "client_box_fill_color", settings::visuals::client_box_fill_color);
		write_bool(ss, "client_name", settings::visuals::client_name);
		write_color(ss, "client_name_color", settings::visuals::client_name_color);
		write_bool(ss, "client_healthbar", settings::visuals::client_healthbar);
		write_color(ss, "client_healthbar_color", settings::visuals::client_healthbar_color);
		write_bool(ss, "client_health_percent", settings::visuals::client_health_percent);
		write_color(ss, "client_health_percent_color", settings::visuals::client_health_percent_color);
		write_bool(ss, "client_armorbar", settings::visuals::client_armorbar);
		write_color(ss, "client_armorbar_color", settings::visuals::client_armorbar_color);
		write_bool(ss, "client_distance", settings::visuals::client_distance);
		write_color(ss, "client_distance_color", settings::visuals::client_distance_color);
		write_bool(ss, "client_tool", settings::visuals::client_tool);
		write_color(ss, "client_tool_color", settings::visuals::client_tool_color);
		write_bool(ss, "client_chams", settings::visuals::client_chams);
		write_color(ss, "client_chams_fill_color", settings::visuals::client_chams_fill_color);
		write_color(ss, "client_chams_outline_color", settings::visuals::client_chams_outline_color);
		write_bool(ss, "client_flags", settings::visuals::client_flags);
		write_color(ss, "client_flags_state_colour", settings::visuals::client_flags_state_colour);
		write_bool(ss, "client_material", settings::visuals::client_material);
		float material_color[3] = { settings::visuals::client_material_color[0], settings::visuals::client_material_color[1], settings::visuals::client_material_color[2] };
		write_color(ss, "client_material_color", material_color);
		write_int(ss, "client_material_type", settings::visuals::client_material_type);
		write_bool(ss, "client_headless", settings::visuals::client_headless);
		write_bool(ss, "client_korblox", settings::visuals::client_korblox);
		write_bool(ss, "esp_preview_auto_rotate", settings::visuals::esp_preview_auto_rotate);
		write_bool(ss, "esp_preview_enabled", settings::visuals::esp_preview_enabled);
		write_bool(ss, "debug_wallcheck", settings::visuals::debug_wallcheck);
		write_float(ss, "fade_in_speed", settings::visuals::fade_in_speed);
		write_float(ss, "fade_out_speed", settings::visuals::fade_out_speed);
		ss << "  },\n";
		
		ss << "  \"movement\": {\n";
		ss << "    \"speedhack\": {\n";
		write_bool(ss, "enabled", settings::movement::speedhack::enabled);
		write_int(ss, "mode", settings::movement::speedhack::mode);
		write_float(ss, "speed", settings::movement::speedhack::speed);
		write_int(ss, "keybind", settings::movement::speedhack::keybind);
		write_int(ss, "activation_mode", settings::movement::speedhack::activation_mode);
		ss << "    },\n";
		ss << "    \"flyhack\": {\n";
		write_bool(ss, "enabled", settings::movement::flyhack::enabled);
		write_int(ss, "mode", settings::movement::flyhack::mode);
		write_float(ss, "speed", settings::movement::flyhack::speed);
		write_int(ss, "keybind", settings::movement::flyhack::keybind);
		write_int(ss, "activation_mode", settings::movement::flyhack::activation_mode);
		ss << "    },\n";
		ss << "    \"tickrate\": {\n";
		write_bool(ss, "enabled", settings::movement::tickrate::enabled);
		write_float(ss, "value", settings::movement::tickrate::value);
		ss << "    },\n";
		ss << "    \"orbit\": {\n";
		write_bool(ss, "enabled", settings::movement::orbit::enabled);
		write_int(ss, "orbit_type", settings::movement::orbit::orbit_type);
		write_float(ss, "speed", settings::movement::orbit::speed);
		write_float(ss, "radius", settings::movement::orbit::radius);
		write_float(ss, "height_offset", settings::movement::orbit::height_offset);
		write_bool(ss, "spectate_target", settings::movement::orbit::spectate_target);
		ss << "    }\n";
		ss << "  },\n";
		
		ss << "  \"lighting\": {\n";
		ss << "    \"fog\": {\n";
		write_bool(ss, "enabled", settings::lighting::fog::enabled);
		write_float(ss, "fog_start", settings::lighting::fog::fog_start);
		write_float(ss, "fog_end", settings::lighting::fog::fog_end);
		write_float(ss, "fog_r", settings::lighting::fog::fog_r);
		write_float(ss, "fog_g", settings::lighting::fog::fog_g);
		write_float(ss, "fog_b", settings::lighting::fog::fog_b);
		ss << "    },\n";
		ss << "    \"exposure\": {\n";
		write_bool(ss, "enabled", settings::lighting::exposure::enabled);
		write_float(ss, "exposure", settings::lighting::exposure::exposure);
		ss << "    },\n";
		ss << "    \"skybox\": {\n";
		write_bool(ss, "enabled", settings::lighting::skybox::enabled);
		write_int(ss, "preset_index", settings::lighting::skybox::preset_index);
		write_string(ss, "skybox_bk", settings::lighting::skybox::skybox_bk.c_str());
		write_string(ss, "skybox_dn", settings::lighting::skybox::skybox_dn.c_str());
		write_string(ss, "skybox_ft", settings::lighting::skybox::skybox_ft.c_str());
		write_string(ss, "skybox_lf", settings::lighting::skybox::skybox_lf.c_str());
		write_string(ss, "skybox_rt", settings::lighting::skybox::skybox_rt.c_str());
		write_string(ss, "skybox_up", settings::lighting::skybox::skybox_up.c_str());
		ss << "    },\n";
		ss << "    \"rain\": {\n";
		write_bool(ss, "enabled", settings::lighting::rain::enabled);
		write_float(ss, "density", settings::lighting::rain::density);
		write_float(ss, "speed", settings::lighting::rain::speed);
		write_float(ss, "length", settings::lighting::rain::length);
		write_float(ss, "opacity", settings::lighting::rain::opacity);
		ss << "    },\n";
		ss << "    \"clocktime\": {\n";
		write_bool(ss, "enabled", settings::lighting::clocktime::enabled);
		write_float(ss, "clock_time", settings::lighting::clocktime::clock_time);
		ss << "    }\n";
		ss << "  },\n";
		
		ss << "  }\n";
		
		ss << "}\n";
		
		std::ofstream file(path);
		if (!file.is_open())
			return false;
		
		file << ss.str();
		file.close();
		return true;
	}

	static void reset_to_defaults()
	{
		settings::menu::theme = 0;
		settings::menu::custom_theme_color[0] = 0.72f; settings::menu::custom_theme_color[1] = 0.94f; settings::menu::custom_theme_color[2] = 0.72f; settings::menu::custom_theme_color[3] = 1.0f;
		settings::menu::menu_keybind = VK_INSERT;
		settings::menu::watermark = false;
		settings::menu::streamproof = false;
		settings::menu::streamproof_keybind = 0;
		settings::menu::streamproof_activation_mode = 2;
		settings::menu::vsync = false;
		settings::menu::hide_console = false;
		settings::menu::performance_mode = false;
		settings::ui::keybinds = true;
		settings::ui::notifications = true;
		settings::ui::hit_logs = true;
		settings::ui::tooltips = true;
		settings::ui::streamproof_indicator = true;
		settings::ui::keybinds_mode = 1;
		settings::ui::overlay_preset = 0;
		settings::ui::ui_scale = 1.0f;
		settings::ui::watermark_position[0] = 20.0f; settings::ui::watermark_position[1] = 20.0f;
		settings::ui::keybinds_position[0] = 20.0f; settings::ui::keybinds_position[1] = 100.0f;
		settings::ui::hitlog_position[0] = 20.0f; settings::ui::hitlog_position[1] = 180.0f;
		settings::ui::favorites_mask = 0;

		settings::aimbot::enabled = false;
		settings::aimbot::keybind = 0;
		settings::aimbot::activation_mode = 1;
		settings::aimbot::mode = 1;
		settings::aimbot::target_part = 1;
		settings::aimbot::fov = 100.f;
		settings::aimbot::use_fov = false;
		settings::aimbot::draw_fov = false;
		settings::aimbot::fov_circle_colour[0] = 1.f; settings::aimbot::fov_circle_colour[1] = 1.f; settings::aimbot::fov_circle_colour[2] = 1.f; settings::aimbot::fov_circle_colour[3] = 1.f;
		settings::aimbot::fov_outline_colour[0] = 0.f; settings::aimbot::fov_outline_colour[1] = 0.f; settings::aimbot::fov_outline_colour[2] = 0.f; settings::aimbot::fov_outline_colour[3] = 1.f;
		settings::aimbot::smoothing = false;
		settings::aimbot::smoothingx = 10.f;
		settings::aimbot::smoothingy = 10.f;
		settings::aimbot::smoothing_style = 0;
		settings::aimbot::enable_prediction = false;
		settings::aimbot::prediction_x = 10.f;
		settings::aimbot::prediction_y = 10.f;
		settings::aimbot::air_part_enabled = false;
		settings::aimbot::air_part = 1;
		settings::aimbot::air_prediction_enabled = false;
		settings::aimbot::air_prediction_x = 10.f;
		settings::aimbot::air_prediction_y = 10.f;
		settings::aimbot::teamcheck = false;
		settings::aimbot::knock_check = false;
		settings::aimbot::sticky_aim = false;
		settings::aimbot::health_check_enabled = false;
		settings::aimbot::min_health = 0.0f;

		settings::silentaim::enabled = false;
		settings::silentaim::keybind = 0;
		settings::silentaim::activation_mode = 1;
		settings::silentaim::target_part = 1;
		settings::silentaim::fov = 100.f;
		settings::silentaim::use_fov = false;
		settings::silentaim::draw_fov = false;
		settings::silentaim::fov_circle_colour[0] = 1.f; settings::silentaim::fov_circle_colour[1] = 1.f; settings::silentaim::fov_circle_colour[2] = 1.f; settings::silentaim::fov_circle_colour[3] = 1.f;
		settings::silentaim::fov_outline_colour[0] = 0.f; settings::silentaim::fov_outline_colour[1] = 0.f; settings::silentaim::fov_outline_colour[2] = 0.f; settings::silentaim::fov_outline_colour[3] = 1.f;
		settings::silentaim::enable_prediction = false;
		settings::silentaim::prediction_x = 10.f;
		settings::silentaim::prediction_y = 10.f;
		settings::silentaim::sticky_aim = false;
		settings::silentaim::auto_switch = false;
		settings::silentaim::spoof_mouse = true;
		settings::silentaim::use_aimbot_target = false;
		settings::silentaim::teamcheck = false;
		settings::silentaim::guncheck = false;
		settings::silentaim::knock_check = false;
		settings::silentaim::priorities = 0;
		settings::silentaim::health_check_enabled = false;
		settings::silentaim::min_health = 0.0f;

		settings::rage::hitsounds = false;
		settings::rage::hitsound_type = 0;
		settings::rage::hitsound_method = 0;
		settings::rage::hitsound_volume = 100.0f;
		settings::rage::rapidfire = false;
		settings::rage::noclip = false;
		settings::rage::spin360::enabled = false;
		settings::rage::spin360::keybind = 0;
		settings::rage::spin360::activation_mode = 1;
		settings::rage::spin360::speed = 10.0f;
		settings::rage::spin360::mode = 0;

		settings::visuals::enable_enemies = false;
		settings::visuals::enable_client = false;
		settings::visuals::teamcheck = false;
		settings::visuals::distance_check = false;
		settings::visuals::max_distance = 250.0f;
		settings::visuals::box = false;
		settings::visuals::box_type = 0;
		settings::visuals::box_color[0] = 1.f; settings::visuals::box_color[1] = 1.f; settings::visuals::box_color[2] = 1.f; settings::visuals::box_color[3] = 1.f;
		settings::visuals::box_fill = false;
		settings::visuals::box_fill_color[0] = 0.2f; settings::visuals::box_fill_color[1] = 0.2f; settings::visuals::box_fill_color[2] = 0.2f; settings::visuals::box_fill_color[3] = 0.3f;
		settings::visuals::name = false;
		settings::visuals::name_type = 0;
		settings::visuals::name_display_type = 0;
		settings::visuals::name_color[0] = 1.f; settings::visuals::name_color[1] = 1.f; settings::visuals::name_color[2] = 1.f; settings::visuals::name_color[3] = 1.f;
		settings::visuals::healthbar = false;
		settings::visuals::healthbar_color[0] = 0.f; settings::visuals::healthbar_color[1] = 1.f; settings::visuals::healthbar_color[2] = 0.f; settings::visuals::healthbar_color[3] = 1.f;
		settings::visuals::health_based_healthbar = false;
		settings::visuals::blend = false;
		settings::visuals::name_color_blend_start[0] = 1.f; settings::visuals::name_color_blend_start[1] = 1.f; settings::visuals::name_color_blend_start[2] = 1.f; settings::visuals::name_color_blend_start[3] = 1.f;
		settings::visuals::name_color_blend_end[0] = 0.f; settings::visuals::name_color_blend_end[1] = 0.f; settings::visuals::name_color_blend_end[2] = 1.f; settings::visuals::name_color_blend_end[3] = 1.f;
		settings::visuals::health_percent = false;
		settings::visuals::health_percent_color[0] = 1.f; settings::visuals::health_percent_color[1] = 1.f; settings::visuals::health_percent_color[2] = 1.f; settings::visuals::health_percent_color[3] = 1.f;
		settings::visuals::armorbar = false;
		settings::visuals::armorbar_color[0] = 0.275f; settings::visuals::armorbar_color[1] = 0.627f; settings::visuals::armorbar_color[2] = 1.f; settings::visuals::armorbar_color[3] = 1.f;
		settings::visuals::distance = false;
		settings::visuals::distance_measurement = 0;
		settings::visuals::distance_color[0] = 1.f; settings::visuals::distance_color[1] = 1.f; settings::visuals::distance_color[2] = 1.f; settings::visuals::distance_color[3] = 1.f;
		settings::visuals::tool = false;
		settings::visuals::tool_color[0] = 1.f; settings::visuals::tool_color[1] = 1.f; settings::visuals::tool_color[2] = 1.f; settings::visuals::tool_color[3] = 1.f;
		settings::visuals::esp_font = 0;
		settings::visuals::local_player = false;
		settings::visuals::chams = false;
		settings::visuals::chams_type = 1;
		settings::visuals::chams_fill_color[0] = 1.f; settings::visuals::chams_fill_color[1] = 0.f; settings::visuals::chams_fill_color[2] = 0.f; settings::visuals::chams_fill_color[3] = 0.5f;
		settings::visuals::chams_outline_color[0] = 1.f; settings::visuals::chams_outline_color[1] = 1.f; settings::visuals::chams_outline_color[2] = 1.f; settings::visuals::chams_outline_color[3] = 1.f;
		settings::visuals::chams_fill_enabled = true;
		settings::visuals::chams_outline_enabled = true;
		settings::visuals::flags = false;
		settings::visuals::flags_state_colour[0] = 1.f; settings::visuals::flags_state_colour[1] = 1.f; settings::visuals::flags_state_colour[2] = 1.f; settings::visuals::flags_state_colour[3] = 1.f;
		settings::visuals::client_box = false;
		settings::visuals::client_box_color[0] = 1.f; settings::visuals::client_box_color[1] = 1.f; settings::visuals::client_box_color[2] = 1.f; settings::visuals::client_box_color[3] = 1.f;
		settings::visuals::client_box_fill = false;
		settings::visuals::client_box_fill_color[0] = 0.2f; settings::visuals::client_box_fill_color[1] = 0.2f; settings::visuals::client_box_fill_color[2] = 0.2f; settings::visuals::client_box_fill_color[3] = 0.3f;
		settings::visuals::client_name = false;
		settings::visuals::client_name_color[0] = 1.f; settings::visuals::client_name_color[1] = 1.f; settings::visuals::client_name_color[2] = 1.f; settings::visuals::client_name_color[3] = 1.f;
		settings::visuals::client_healthbar = false;
		settings::visuals::client_healthbar_color[0] = 0.f; settings::visuals::client_healthbar_color[1] = 1.f; settings::visuals::client_healthbar_color[2] = 0.f; settings::visuals::client_healthbar_color[3] = 1.f;
		settings::visuals::client_health_percent = false;
		settings::visuals::client_health_percent_color[0] = 1.f; settings::visuals::client_health_percent_color[1] = 1.f; settings::visuals::client_health_percent_color[2] = 1.f; settings::visuals::client_health_percent_color[3] = 1.f;
		settings::visuals::client_armorbar = false;
		settings::visuals::client_armorbar_color[0] = 0.275f; settings::visuals::client_armorbar_color[1] = 0.627f; settings::visuals::client_armorbar_color[2] = 1.f; settings::visuals::client_armorbar_color[3] = 1.f;
		settings::visuals::client_distance = false;
		settings::visuals::client_distance_color[0] = 1.f; settings::visuals::client_distance_color[1] = 1.f; settings::visuals::client_distance_color[2] = 1.f; settings::visuals::client_distance_color[3] = 1.f;
		settings::visuals::client_tool = false;
		settings::visuals::client_tool_color[0] = 1.f; settings::visuals::client_tool_color[1] = 1.f; settings::visuals::client_tool_color[2] = 1.f; settings::visuals::client_tool_color[3] = 1.f;
		settings::visuals::client_chams = false;
		settings::visuals::client_chams_fill_color[0] = 1.f; settings::visuals::client_chams_fill_color[1] = 0.f; settings::visuals::client_chams_fill_color[2] = 0.f; settings::visuals::client_chams_fill_color[3] = 0.5f;
		settings::visuals::client_chams_outline_color[0] = 1.f; settings::visuals::client_chams_outline_color[1] = 1.f; settings::visuals::client_chams_outline_color[2] = 1.f; settings::visuals::client_chams_outline_color[3] = 1.f;
		settings::visuals::client_flags = false;
		settings::visuals::client_flags_state_colour[0] = 1.f; settings::visuals::client_flags_state_colour[1] = 1.f; settings::visuals::client_flags_state_colour[2] = 1.f; settings::visuals::client_flags_state_colour[3] = 1.f;
		settings::visuals::client_material = false;
		settings::visuals::client_material_color[0] = 1.f; settings::visuals::client_material_color[1] = 0.f; settings::visuals::client_material_color[2] = 0.f;
		settings::visuals::client_material_type = 1584;
		settings::visuals::esp_preview_auto_rotate = true;
		settings::visuals::esp_preview_enabled = false;
		settings::visuals::debug_wallcheck = false;
		settings::visuals::fade_in_speed = 5.0f;
		settings::visuals::fade_out_speed = 5.0f;

		settings::movement::speedhack::enabled = false;
		settings::movement::speedhack::mode = 0;
		settings::movement::speedhack::speed = 50.0f;
		settings::movement::speedhack::keybind = 0;
		settings::movement::speedhack::activation_mode = 1;

		settings::movement::flyhack::enabled = false;
		settings::movement::flyhack::mode = 0;
		settings::movement::flyhack::speed = 50.0f;
		settings::movement::flyhack::keybind = 0;
		settings::movement::flyhack::activation_mode = 1;

		settings::movement::tickrate::enabled = false;
		settings::movement::tickrate::value = 240.0f;

		settings::movement::orbit::enabled = false;
		settings::movement::orbit::orbit_type = 0;
		settings::movement::orbit::speed = 30.0f;
		settings::movement::orbit::radius = 10.0f;
		settings::movement::orbit::height_offset = 10.0f;
		settings::movement::orbit::spectate_target = false;

		settings::lighting::fog::enabled = false;
		settings::lighting::fog::fog_start = 0.0f;
		settings::lighting::fog::fog_end = 500.0f;
		settings::lighting::fog::fog_r = 0.75f;
		settings::lighting::fog::fog_g = 0.75f;
		settings::lighting::fog::fog_b = 0.75f;

		settings::lighting::exposure::enabled = false;
		settings::lighting::exposure::exposure = 0.0f;

		settings::lighting::skybox::enabled = false;
		settings::lighting::skybox::apply_skybox = false;
		settings::lighting::skybox::preset_index = 0;
		settings::lighting::skybox::skybox_bk.clear();
		settings::lighting::skybox::skybox_dn.clear();
		settings::lighting::skybox::skybox_ft.clear();
		settings::lighting::skybox::skybox_lf.clear();
		settings::lighting::skybox::skybox_rt.clear();
		settings::lighting::skybox::skybox_up.clear();

		settings::lighting::rain::enabled = false;
		settings::lighting::rain::density = 100.0f;
		settings::lighting::rain::speed = 650.0f;
		settings::lighting::rain::length = 18.0f;
		settings::lighting::rain::opacity = 0.30f;

		settings::lighting::clocktime::enabled = false;
		settings::lighting::clocktime::clock_time = 12.0f;
	}

	bool load_config(const std::string& name)
	{
		std::string path = get_config_path(name);
		if (path.empty() || !std::filesystem::exists(path))
			return false;
		
		std::ifstream file(path);
		if (!file.is_open())
			return false;
		
		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();

		reset_to_defaults();
	
		std::string menu_section = extract_section(json, "menu");
		std::string aimbot_section = extract_section(json, "aimbot");
		std::string silentaim_section = extract_section(json, "silentaim");
		std::string rage_section = extract_section(json, "rage");
		std::string visuals_section = extract_section(json, "visuals");
		std::string movement_section = extract_section(json, "movement");
		std::string lighting_section = extract_section(json, "lighting");

		std::string speedhack_section = extract_section(movement_section, "speedhack");
		std::string flyhack_section = extract_section(movement_section, "flyhack");
		std::string tickrate_section = extract_section(movement_section, "tickrate");
		std::string orbit_section = extract_section(movement_section, "orbit");
		std::string fog_section = extract_section(lighting_section, "fog");
		std::string exposure_section = extract_section(lighting_section, "exposure");
		std::string skybox_section = extract_section(lighting_section, "skybox");
		std::string rain_section = extract_section(lighting_section, "rain");
		std::string clocktime_section = extract_section(lighting_section, "clocktime");
		std::string auth_section = extract_section(json, "auth");

		if (!menu_section.empty())
		{
			settings::menu::theme = read_int(menu_section, "theme", settings::menu::theme);
			read_color(menu_section, "custom_theme_color", settings::menu::custom_theme_color);
			std::string menu_keybind_str = read_string(menu_section, "menu_keybind", "");
			if (!menu_keybind_str.empty())
			{
				settings::menu::menu_keybind = key_name_to_code(menu_keybind_str);
			}
			else
			{
				settings::menu::menu_keybind = read_int(menu_section, "menu_keybind", settings::menu::menu_keybind);
			}
			settings::menu::watermark = read_bool(menu_section, "watermark", settings::menu::watermark);
			settings::menu::streamproof = read_bool(menu_section, "streamproof", settings::menu::streamproof);
			settings::menu::streamproof_keybind = read_int(menu_section, "streamproof_keybind", settings::menu::streamproof_keybind);
			settings::menu::streamproof_activation_mode = read_int(menu_section, "streamproof_activation_mode", settings::menu::streamproof_activation_mode);
			settings::ui::keybinds = read_bool(menu_section, "show_keybinds", settings::ui::keybinds);
			settings::ui::notifications = read_bool(menu_section, "notifications", settings::ui::notifications);
			settings::ui::hit_logs = read_bool(menu_section, "hit_logs", settings::ui::hit_logs);
			settings::ui::tooltips = read_bool(menu_section, "tooltips", settings::ui::tooltips);
			settings::ui::streamproof_indicator = read_bool(menu_section, "streamproof_indicator", settings::ui::streamproof_indicator);
			settings::ui::keybinds_mode = read_int(menu_section, "keybinds_mode", settings::ui::keybinds_mode);
			settings::ui::overlay_preset = read_int(menu_section, "overlay_preset", settings::ui::overlay_preset);
			settings::ui::ui_scale = read_float(menu_section, "ui_scale", settings::ui::ui_scale);
			settings::ui::favorites_mask = static_cast<std::uint32_t>(read_int(menu_section, "favorites_mask", static_cast<int>(settings::ui::favorites_mask)));
			settings::ui::watermark_position[0] = read_float(menu_section, "watermark_pos_x", settings::ui::watermark_position[0]);
			settings::ui::watermark_position[1] = read_float(menu_section, "watermark_pos_y", settings::ui::watermark_position[1]);
			settings::ui::keybinds_position[0] = read_float(menu_section, "keybinds_pos_x", settings::ui::keybinds_position[0]);
			settings::ui::keybinds_position[1] = read_float(menu_section, "keybinds_pos_y", settings::ui::keybinds_position[1]);
			settings::ui::hitlog_position[0] = read_float(menu_section, "hitlog_pos_x", settings::ui::hitlog_position[0]);
			settings::ui::hitlog_position[1] = read_float(menu_section, "hitlog_pos_y", settings::ui::hitlog_position[1]);
			settings::menu::vsync = read_bool(menu_section, "vsync", settings::menu::vsync);
			settings::menu::hide_console = read_bool(menu_section, "hide_console", settings::menu::hide_console);
			settings::menu::performance_mode = read_bool(menu_section, "performance_mode", settings::menu::performance_mode);
		}

		if (!aimbot_section.empty())
		{
			settings::aimbot::enabled = read_bool(aimbot_section, "enabled", settings::aimbot::enabled);
			settings::aimbot::keybind = read_int(aimbot_section, "keybind", settings::aimbot::keybind);
			settings::aimbot::activation_mode = read_int(aimbot_section, "activation_mode", settings::aimbot::activation_mode);
			settings::aimbot::mode = read_int(aimbot_section, "mode", settings::aimbot::mode);
			settings::aimbot::target_part = read_int(aimbot_section, "target_part", settings::aimbot::target_part);
			settings::aimbot::fov = read_float(aimbot_section, "fov", settings::aimbot::fov);
			settings::aimbot::use_fov = read_bool(aimbot_section, "use_fov", settings::aimbot::use_fov);
			settings::aimbot::draw_fov = read_bool(aimbot_section, "draw_fov", settings::aimbot::draw_fov);
			read_color(aimbot_section, "fov_circle_colour", settings::aimbot::fov_circle_colour);
			read_color(aimbot_section, "fov_outline_colour", settings::aimbot::fov_outline_colour);
			settings::aimbot::smoothing = read_bool(aimbot_section, "smoothing", settings::aimbot::smoothing);
			settings::aimbot::smoothingx = read_float(aimbot_section, "smoothingx", settings::aimbot::smoothingx);
			settings::aimbot::smoothingy = read_float(aimbot_section, "smoothingy", settings::aimbot::smoothingy);
			settings::aimbot::smoothing_style = read_int(aimbot_section, "smoothing_style", settings::aimbot::smoothing_style);
			settings::aimbot::enable_prediction = read_bool(aimbot_section, "enable_prediction", settings::aimbot::enable_prediction);
			settings::aimbot::prediction_x = read_float(aimbot_section, "prediction_x", settings::aimbot::prediction_x);
			settings::aimbot::prediction_y = read_float(aimbot_section, "prediction_y", settings::aimbot::prediction_y);
			settings::aimbot::air_part_enabled = read_bool(aimbot_section, "air_part_enabled", settings::aimbot::air_part_enabled);
			settings::aimbot::air_part = read_int(aimbot_section, "air_part", settings::aimbot::air_part);
			settings::aimbot::air_prediction_enabled = read_bool(aimbot_section, "air_prediction_enabled", settings::aimbot::air_prediction_enabled);
			settings::aimbot::air_prediction_x = read_float(aimbot_section, "air_prediction_x", settings::aimbot::air_prediction_x);
			settings::aimbot::air_prediction_y = read_float(aimbot_section, "air_prediction_y", settings::aimbot::air_prediction_y);
			settings::aimbot::teamcheck = read_bool(aimbot_section, "teamcheck", settings::aimbot::teamcheck);
			settings::aimbot::knock_check = read_bool(aimbot_section, "knock_check", settings::aimbot::knock_check);
			settings::aimbot::sticky_aim = read_bool(aimbot_section, "sticky_aim", settings::aimbot::sticky_aim);
			settings::aimbot::health_check_enabled = read_bool(aimbot_section, "health_check_enabled", settings::aimbot::health_check_enabled);
			settings::aimbot::min_health = read_float(aimbot_section, "min_health", settings::aimbot::min_health);
		}

		if (!silentaim_section.empty())
		{
			settings::silentaim::enabled = read_bool(silentaim_section, "enabled", settings::silentaim::enabled);
			settings::silentaim::keybind = read_int(silentaim_section, "keybind", settings::silentaim::keybind);
			settings::silentaim::activation_mode = read_int(silentaim_section, "activation_mode", settings::silentaim::activation_mode);
			settings::silentaim::target_part = read_int(silentaim_section, "target_part", settings::silentaim::target_part);
			settings::silentaim::fov = read_float(silentaim_section, "fov", settings::silentaim::fov);
			settings::silentaim::use_fov = read_bool(silentaim_section, "use_fov", settings::silentaim::use_fov);
			settings::silentaim::draw_fov = read_bool(silentaim_section, "draw_fov", settings::silentaim::draw_fov);
			settings::silentaim::lerp_fov = read_bool(silentaim_section, "lerp_fov", settings::silentaim::lerp_fov);
			settings::silentaim::attach_fov_to_target = read_bool(silentaim_section, "attach_fov_to_target", settings::silentaim::attach_fov_to_target);
			read_color(silentaim_section, "fov_circle_colour", settings::silentaim::fov_circle_colour);
			read_color(silentaim_section, "fov_outline_colour", settings::silentaim::fov_outline_colour);
			settings::silentaim::enable_prediction = read_bool(silentaim_section, "enable_prediction", settings::silentaim::enable_prediction);
			settings::silentaim::prediction_x = read_float(silentaim_section, "prediction_x", settings::silentaim::prediction_x);
			settings::silentaim::prediction_y = read_float(silentaim_section, "prediction_y", settings::silentaim::prediction_y);
			settings::silentaim::sticky_aim = read_bool(silentaim_section, "sticky_aim", settings::silentaim::sticky_aim);
			settings::silentaim::auto_switch = read_bool(silentaim_section, "auto_switch", settings::silentaim::auto_switch);
			settings::silentaim::spoof_mouse = read_bool(silentaim_section, "spoof_mouse", settings::silentaim::spoof_mouse);
			settings::silentaim::use_aimbot_target = read_bool(silentaim_section, "use_aimbot_target", settings::silentaim::use_aimbot_target);
			settings::silentaim::teamcheck = read_bool(silentaim_section, "teamcheck", settings::silentaim::teamcheck);
			settings::silentaim::guncheck = read_bool(silentaim_section, "guncheck", settings::silentaim::guncheck);
			settings::silentaim::knock_check = read_bool(silentaim_section, "knock_check", settings::silentaim::knock_check);
			settings::silentaim::priorities = read_int(silentaim_section, "priorities", settings::silentaim::priorities);
			settings::silentaim::health_check_enabled = read_bool(silentaim_section, "health_check_enabled", settings::silentaim::health_check_enabled);
			settings::silentaim::min_health = read_float(silentaim_section, "min_health", settings::silentaim::min_health);
			settings::silentaim::draw_target_dot = read_bool(silentaim_section, "draw_target_dot", settings::silentaim::draw_target_dot);
			read_color(silentaim_section, "target_dot_color", settings::silentaim::target_dot_color);
			settings::silentaim::target_dot_size = read_float(silentaim_section, "target_dot_size", settings::silentaim::target_dot_size);
			settings::silentaim::draw_snap_line = read_bool(silentaim_section, "draw_snap_line", settings::silentaim::draw_snap_line);
			read_color(silentaim_section, "snap_line_color", settings::silentaim::snap_line_color);
		}

		if (!rage_section.empty())
		{
			settings::rage::hitsounds = read_bool(rage_section, "hitsounds", settings::rage::hitsounds);
			settings::rage::hitsound_type = read_int(rage_section, "hitsound_type", settings::rage::hitsound_type);
			settings::rage::hitsound_method = read_int(rage_section, "hitsound_method", settings::rage::hitsound_method);
			settings::rage::hitsound_volume = read_float(rage_section, "hitsound_volume", settings::rage::hitsound_volume);
			settings::rage::rapidfire = read_bool(rage_section, "rapidfire", settings::rage::rapidfire);
			settings::rage::noclip = read_bool(rage_section, "noclip", settings::rage::noclip);

			std::string spin360_section = extract_section(rage_section, "spin360");
			if (!spin360_section.empty())
			{
				settings::rage::spin360::enabled = read_bool(spin360_section, "enabled", settings::rage::spin360::enabled);
				settings::rage::spin360::keybind = read_int(spin360_section, "keybind", settings::rage::spin360::keybind);
				settings::rage::spin360::activation_mode = read_int(spin360_section, "activation_mode", settings::rage::spin360::activation_mode);
				settings::rage::spin360::speed = read_float(spin360_section, "speed", settings::rage::spin360::speed);
				settings::rage::spin360::mode = read_int(spin360_section, "mode", settings::rage::spin360::mode);
			}
		}

		if (!visuals_section.empty())
		{
			settings::visuals::enable_enemies = read_bool(visuals_section, "enable_enemies", settings::visuals::enable_enemies);
			settings::visuals::enable_client = read_bool(visuals_section, "enable_client", settings::visuals::enable_client);
			settings::visuals::teamcheck = read_bool(visuals_section, "teamcheck", settings::visuals::teamcheck);
			settings::visuals::distance_check = read_bool(visuals_section, "distance_check", settings::visuals::distance_check);
			settings::visuals::max_distance = read_float(visuals_section, "max_distance", settings::visuals::max_distance);
			settings::visuals::box = read_bool(visuals_section, "box", settings::visuals::box);
			settings::visuals::box_type = read_int(visuals_section, "box_type", settings::visuals::box_type);
			read_color(visuals_section, "box_color", settings::visuals::box_color);
			settings::visuals::box_fill = read_bool(visuals_section, "box_fill", settings::visuals::box_fill);
			read_color(visuals_section, "box_fill_color", settings::visuals::box_fill_color);
			settings::visuals::name = read_bool(visuals_section, "name", settings::visuals::name);
			settings::visuals::name_type = read_int(visuals_section, "name_type", settings::visuals::name_type);
			settings::visuals::name_display_type = read_int(visuals_section, "name_display_type", settings::visuals::name_display_type);
			read_color(visuals_section, "name_color", settings::visuals::name_color);
			settings::visuals::healthbar = read_bool(visuals_section, "healthbar", settings::visuals::healthbar);
			read_color(visuals_section, "healthbar_color", settings::visuals::healthbar_color);
			settings::visuals::health_based_healthbar = read_bool(visuals_section, "health_based_healthbar", settings::visuals::health_based_healthbar);
			settings::visuals::health_percent = read_bool(visuals_section, "health_percent", settings::visuals::health_percent);
			read_color(visuals_section, "health_percent_color", settings::visuals::health_percent_color);
			settings::visuals::armorbar = read_bool(visuals_section, "armorbar", settings::visuals::armorbar);
			read_color(visuals_section, "armorbar_color", settings::visuals::armorbar_color);
			settings::visuals::distance = read_bool(visuals_section, "distance", settings::visuals::distance);
			settings::visuals::distance_measurement = read_int(visuals_section, "distance_measurement", settings::visuals::distance_measurement);
			read_color(visuals_section, "distance_color", settings::visuals::distance_color);
			settings::visuals::tool = read_bool(visuals_section, "tool", settings::visuals::tool);
			read_color(visuals_section, "tool_color", settings::visuals::tool_color);
			settings::visuals::esp_font = read_int(visuals_section, "esp_font", settings::visuals::esp_font);
			settings::visuals::local_player = read_bool(visuals_section, "local_player", settings::visuals::local_player);
			settings::visuals::chams = read_bool(visuals_section, "chams", settings::visuals::chams);
			settings::visuals::chams_type = read_int(visuals_section, "chams_type", settings::visuals::chams_type);
			read_color(visuals_section, "chams_fill_color", settings::visuals::chams_fill_color);
			read_color(visuals_section, "chams_outline_color", settings::visuals::chams_outline_color);
			settings::visuals::chams_fill_enabled = read_bool(visuals_section, "chams_fill_enabled", settings::visuals::chams_fill_enabled);
			settings::visuals::chams_outline_enabled = read_bool(visuals_section, "chams_outline_enabled", settings::visuals::chams_outline_enabled);
			settings::visuals::target_warning_icon = read_bool(visuals_section, "target_warning_icon", settings::visuals::target_warning_icon);
			settings::visuals::target_warning_icon_size = read_float(visuals_section, "target_warning_icon_size", settings::visuals::target_warning_icon_size);
			settings::visuals::flags = read_bool(visuals_section, "flags", settings::visuals::flags);
			read_color(visuals_section, "flags_state_colour", settings::visuals::flags_state_colour);
			settings::visuals::blend = read_bool(visuals_section, "blend", settings::visuals::blend);
			read_color(visuals_section, "name_color_blend_start", settings::visuals::name_color_blend_start);
			read_color(visuals_section, "name_color_blend_end", settings::visuals::name_color_blend_end);
			read_color(visuals_section, "colorblend_start", settings::visuals::name_color_blend_start);
			read_color(visuals_section, "colorblend_end", settings::visuals::name_color_blend_end);
			
			{
				bool old_blend_name = read_bool(visuals_section, "blend_name", false);
				if (old_blend_name && !settings::visuals::blend)
					settings::visuals::blend = true;
			}
			
			settings::visuals::client_box = read_bool(visuals_section, "client_box", settings::visuals::client_box);
			read_color(visuals_section, "client_box_color", settings::visuals::client_box_color);
			settings::visuals::client_box_fill = read_bool(visuals_section, "client_box_fill", settings::visuals::client_box_fill);
			read_color(visuals_section, "client_box_fill_color", settings::visuals::client_box_fill_color);
			settings::visuals::client_name = read_bool(visuals_section, "client_name", settings::visuals::client_name);
			read_color(visuals_section, "client_name_color", settings::visuals::client_name_color);
			settings::visuals::client_healthbar = read_bool(visuals_section, "client_healthbar", settings::visuals::client_healthbar);
			read_color(visuals_section, "client_healthbar_color", settings::visuals::client_healthbar_color);
			settings::visuals::client_health_percent = read_bool(visuals_section, "client_health_percent", settings::visuals::client_health_percent);
			read_color(visuals_section, "client_health_percent_color", settings::visuals::client_health_percent_color);
			settings::visuals::client_armorbar = read_bool(visuals_section, "client_armorbar", settings::visuals::client_armorbar);
			read_color(visuals_section, "client_armorbar_color", settings::visuals::client_armorbar_color);
			settings::visuals::client_distance = read_bool(visuals_section, "client_distance", settings::visuals::client_distance);
			read_color(visuals_section, "client_distance_color", settings::visuals::client_distance_color);
			settings::visuals::client_tool = read_bool(visuals_section, "client_tool", settings::visuals::client_tool);
			read_color(visuals_section, "client_tool_color", settings::visuals::client_tool_color);
			settings::visuals::client_chams = read_bool(visuals_section, "client_chams", settings::visuals::client_chams);
			read_color(visuals_section, "client_chams_fill_color", settings::visuals::client_chams_fill_color);
			read_color(visuals_section, "client_chams_outline_color", settings::visuals::client_chams_outline_color);
			settings::visuals::client_flags = read_bool(visuals_section, "client_flags", settings::visuals::client_flags);
			read_color(visuals_section, "client_flags_state_colour", settings::visuals::client_flags_state_colour);
			settings::visuals::client_material = read_bool(visuals_section, "client_material", settings::visuals::client_material);
			float material_color[3];
			read_color(visuals_section, "client_material_color", material_color);
			settings::visuals::client_material_color[0] = material_color[0];
			settings::visuals::client_material_color[1] = material_color[1];
			settings::visuals::client_material_color[2] = material_color[2];
			settings::visuals::client_material_type = read_int(visuals_section, "client_material_type", settings::visuals::client_material_type);
			settings::visuals::client_headless = read_bool(visuals_section, "client_headless", settings::visuals::client_headless);
			settings::visuals::client_korblox = read_bool(visuals_section, "client_korblox", settings::visuals::client_korblox);
			settings::visuals::esp_preview_auto_rotate = read_bool(visuals_section, "esp_preview_auto_rotate", settings::visuals::esp_preview_auto_rotate);
			settings::visuals::esp_preview_enabled = read_bool(visuals_section, "esp_preview_enabled", settings::visuals::esp_preview_enabled);
			settings::visuals::debug_wallcheck = read_bool(visuals_section, "debug_wallcheck", settings::visuals::debug_wallcheck);
			settings::visuals::fade_in_speed = read_float(visuals_section, "fade_in_speed", settings::visuals::fade_in_speed);
			settings::visuals::fade_out_speed = read_float(visuals_section, "fade_out_speed", settings::visuals::fade_out_speed);
		}

		if (!speedhack_section.empty())
		{
			settings::movement::speedhack::enabled = read_bool(speedhack_section, "enabled", settings::movement::speedhack::enabled);
			settings::movement::speedhack::mode = read_int(speedhack_section, "mode", settings::movement::speedhack::mode);
			settings::movement::speedhack::speed = read_float(speedhack_section, "speed", settings::movement::speedhack::speed);
			settings::movement::speedhack::keybind = read_int(speedhack_section, "keybind", settings::movement::speedhack::keybind);
			settings::movement::speedhack::activation_mode = read_int(speedhack_section, "activation_mode", settings::movement::speedhack::activation_mode);
		}

		if (!flyhack_section.empty())
		{
			settings::movement::flyhack::enabled = read_bool(flyhack_section, "enabled", settings::movement::flyhack::enabled);
			settings::movement::flyhack::mode = read_int(flyhack_section, "mode", settings::movement::flyhack::mode);
			settings::movement::flyhack::speed = read_float(flyhack_section, "speed", settings::movement::flyhack::speed);
			settings::movement::flyhack::keybind = read_int(flyhack_section, "keybind", settings::movement::flyhack::keybind);
			settings::movement::flyhack::activation_mode = read_int(flyhack_section, "activation_mode", settings::movement::flyhack::activation_mode);
		}

		if (!tickrate_section.empty())
		{
			settings::movement::tickrate::enabled = read_bool(tickrate_section, "enabled", settings::movement::tickrate::enabled);
			settings::movement::tickrate::value = read_float(tickrate_section, "value", settings::movement::tickrate::value);
		}

		if (!orbit_section.empty())
		{
			settings::movement::orbit::enabled = read_bool(orbit_section, "enabled", settings::movement::orbit::enabled);
			settings::movement::orbit::orbit_type = read_int(orbit_section, "orbit_type", settings::movement::orbit::orbit_type);
			settings::movement::orbit::speed = read_float(orbit_section, "speed", settings::movement::orbit::speed);
			settings::movement::orbit::radius = read_float(orbit_section, "radius", settings::movement::orbit::radius);
			settings::movement::orbit::height_offset = read_float(orbit_section, "height_offset", settings::movement::orbit::height_offset);
			settings::movement::orbit::spectate_target = read_bool(orbit_section, "spectate_target", settings::movement::orbit::spectate_target);
		}

		if (!fog_section.empty())
		{
			settings::lighting::fog::enabled = read_bool(fog_section, "enabled", settings::lighting::fog::enabled);
			settings::lighting::fog::fog_start = read_float(fog_section, "fog_start", settings::lighting::fog::fog_start);
			settings::lighting::fog::fog_end = read_float(fog_section, "fog_end", settings::lighting::fog::fog_end);
			settings::lighting::fog::fog_r = read_float(fog_section, "fog_r", settings::lighting::fog::fog_r);
			settings::lighting::fog::fog_g = read_float(fog_section, "fog_g", settings::lighting::fog::fog_g);
			settings::lighting::fog::fog_b = read_float(fog_section, "fog_b", settings::lighting::fog::fog_b);
		}

		if (!exposure_section.empty())
		{
			settings::lighting::exposure::enabled = read_bool(exposure_section, "enabled", settings::lighting::exposure::enabled);
			settings::lighting::exposure::exposure = read_float(exposure_section, "exposure", settings::lighting::exposure::exposure);
		}

		if (!skybox_section.empty())
		{
			settings::lighting::skybox::enabled = read_bool(skybox_section, "enabled", settings::lighting::skybox::enabled);
			settings::lighting::skybox::preset_index = read_int(skybox_section, "preset_index", settings::lighting::skybox::preset_index);
			settings::lighting::skybox::skybox_bk = read_string(skybox_section, "skybox_bk", settings::lighting::skybox::skybox_bk);
			settings::lighting::skybox::skybox_dn = read_string(skybox_section, "skybox_dn", settings::lighting::skybox::skybox_dn);
			settings::lighting::skybox::skybox_ft = read_string(skybox_section, "skybox_ft", settings::lighting::skybox::skybox_ft);
			settings::lighting::skybox::skybox_lf = read_string(skybox_section, "skybox_lf", settings::lighting::skybox::skybox_lf);
			settings::lighting::skybox::skybox_rt = read_string(skybox_section, "skybox_rt", settings::lighting::skybox::skybox_rt);
			settings::lighting::skybox::skybox_up = read_string(skybox_section, "skybox_up", settings::lighting::skybox::skybox_up);
			settings::lighting::skybox::apply_skybox = settings::lighting::skybox::enabled;
		}

		if (!rain_section.empty())
		{
			settings::lighting::rain::enabled = read_bool(rain_section, "enabled", settings::lighting::rain::enabled);
			settings::lighting::rain::density = read_float(rain_section, "density", settings::lighting::rain::density);
			settings::lighting::rain::speed = read_float(rain_section, "speed", settings::lighting::rain::speed);
			settings::lighting::rain::length = read_float(rain_section, "length", settings::lighting::rain::length);
			settings::lighting::rain::opacity = read_float(rain_section, "opacity", settings::lighting::rain::opacity);
		}

		if (!clocktime_section.empty())
		{
			settings::lighting::clocktime::enabled = read_bool(clocktime_section, "enabled", settings::lighting::clocktime::enabled);
			settings::lighting::clocktime::clock_time = read_float(clocktime_section, "clock_time", settings::lighting::clocktime::clock_time);
		}


		return true;
	}

	bool delete_config(const std::string& name)
	{
		std::string path = get_config_path(name);
		if (path.empty() || !std::filesystem::exists(path))
			return false;
		
		try
		{
			return std::filesystem::remove(path);
		}
		catch (...)
		{
			return false;
		}
	}

	bool config_exists(const std::string& name)
	{
		std::string path = get_config_path(name);
		return !path.empty() && std::filesystem::exists(path);
	}

	void open_file_location()
	{
		std::string dir = get_config_directory();
		if (!dir.empty() && std::filesystem::exists(dir))
		{
			ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
		}
	}

	bool export_current_config_to_clipboard()
	{
		static const std::string temp_name = "__clipboard_export_temp";
		if (!save_config(temp_name))
			return false;

		const std::string temp_path = get_config_path(temp_name);
		std::ifstream file(temp_path);
		if (!file.is_open())
		{
			std::filesystem::remove(temp_path);
			return false;
		}

		std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		file.close();
		std::filesystem::remove(temp_path);

		if (json.empty())
			return false;

		ImGui::SetClipboardText(json.c_str());
		return true;
	}

	bool import_config_from_clipboard(const std::string& name, bool save_to_file)
	{
		const char* clipboard_text = ImGui::GetClipboardText();
		if (!clipboard_text || !clipboard_text[0])
			return false;

		if (save_to_file)
		{
			if (!ensure_config_directory())
				return false;

			const std::string target_path = get_config_path(name);
			if (target_path.empty())
				return false;

			std::ofstream target_file(target_path, std::ios::trunc);
			if (!target_file.is_open())
				return false;

			target_file << clipboard_text;
			target_file.close();
		}

		const std::string temp_name = save_to_file ? name : "__clipboard_import_temp";
		const std::string temp_path = save_to_file ? get_config_path(name) : get_config_path(temp_name);
		if (temp_path.empty())
			return false;

		if (!save_to_file)
		{
			if (!ensure_config_directory())
				return false;

			std::ofstream temp_file(temp_path, std::ios::trunc);
			if (!temp_file.is_open())
				return false;

			temp_file << clipboard_text;
			temp_file.close();
		}

		const bool loaded = load_config(temp_name);

		if (!save_to_file)
		{
			std::filesystem::remove(temp_path);
		}

		return loaded;
	}

	bool reset_section(section_t section)
	{
		switch (section)
		{
		case section_t::aimbot:
			settings::aimbot::enabled = false;
			settings::aimbot::keybind = 0;
			settings::aimbot::activation_mode = 1;
			settings::aimbot::mode = 1;
			settings::aimbot::target_part = 1;
			settings::aimbot::air_part_enabled = false;
			settings::aimbot::air_part = 1;
			settings::aimbot::fov = 100.f;
			settings::aimbot::use_fov = false;
			settings::aimbot::draw_fov = false;
			settings::aimbot::fov_circle_colour[0] = 1.f; settings::aimbot::fov_circle_colour[1] = 1.f; settings::aimbot::fov_circle_colour[2] = 1.f; settings::aimbot::fov_circle_colour[3] = 1.f;
			settings::aimbot::fov_outline_colour[0] = 0.f; settings::aimbot::fov_outline_colour[1] = 0.f; settings::aimbot::fov_outline_colour[2] = 0.f; settings::aimbot::fov_outline_colour[3] = 1.f;
			settings::aimbot::smoothing = false;
			settings::aimbot::smoothingx = 10.f;
			settings::aimbot::smoothingy = 10.f;
			settings::aimbot::enable_prediction = false;
			settings::aimbot::prediction_x = 10.f;
			settings::aimbot::prediction_y = 10.f;
			settings::aimbot::air_prediction_enabled = false;
			settings::aimbot::air_prediction_x = 10.f;
			settings::aimbot::air_prediction_y = 10.f;
			settings::aimbot::smoothing_style = 0;
			settings::aimbot::teamcheck = false;
			settings::aimbot::knock_check = false;
			settings::aimbot::sticky_aim = false;
			settings::aimbot::health_check_enabled = false;
			settings::aimbot::min_health = 0.0f;
			settings::aimbot::offset_enabled = false;
			settings::aimbot::offset_x = 0.0f;
			settings::aimbot::offset_y = 0.0f;
			return true;

		case section_t::silentaim:
			settings::silentaim::enabled = false;
			settings::silentaim::keybind = 0;
			settings::silentaim::activation_mode = 1;
			settings::silentaim::target_part = 1;
			settings::silentaim::fov = 100.f;
			settings::silentaim::use_fov = false;
			settings::silentaim::draw_fov = false;
			settings::silentaim::lerp_fov = false;
			settings::silentaim::attach_fov_to_target = false;
			settings::silentaim::fov_circle_colour[0] = 1.f; settings::silentaim::fov_circle_colour[1] = 1.f; settings::silentaim::fov_circle_colour[2] = 1.f; settings::silentaim::fov_circle_colour[3] = 1.f;
			settings::silentaim::fov_outline_colour[0] = 0.f; settings::silentaim::fov_outline_colour[1] = 0.f; settings::silentaim::fov_outline_colour[2] = 0.f; settings::silentaim::fov_outline_colour[3] = 1.f;
			settings::silentaim::enable_prediction = false;
			settings::silentaim::prediction_x = 10.f;
			settings::silentaim::prediction_y = 10.f;
			settings::silentaim::sticky_aim = false;
			settings::silentaim::auto_switch = false;
			settings::silentaim::spoof_mouse = true;
			settings::silentaim::use_aimbot_target = false;
			settings::silentaim::teamcheck = false;
			settings::silentaim::guncheck = false;
			settings::silentaim::knock_check = false;
			settings::silentaim::priorities = 0;
			settings::silentaim::health_check_enabled = false;
			settings::silentaim::min_health = 0.0f;
			settings::silentaim::draw_target_dot = false;
			settings::silentaim::target_dot_color[0] = 1.f; settings::silentaim::target_dot_color[1] = 0.f; settings::silentaim::target_dot_color[2] = 0.f; settings::silentaim::target_dot_color[3] = 1.f;
			settings::silentaim::target_dot_size = 4.0f;
			settings::silentaim::draw_snap_line = false;
			settings::silentaim::snap_line_color[0] = 1.f; settings::silentaim::snap_line_color[1] = 1.f; settings::silentaim::snap_line_color[2] = 1.f; settings::silentaim::snap_line_color[3] = 1.f;
			return true;

		case section_t::visuals:
			settings::visuals::enable_enemies = false;
			settings::visuals::enable_client = false;
			settings::visuals::teamcheck = false;
			settings::visuals::distance_check = false;
			settings::visuals::max_distance = 250.0f;
			settings::visuals::box = false;
			settings::visuals::box_type = 0;
			settings::visuals::box_fill = false;
			settings::visuals::name = false;
			settings::visuals::avatar = false;
			settings::visuals::healthbar = false;
			settings::visuals::health_based_healthbar = false;
			settings::visuals::health_percent = false;
			settings::visuals::armorbar = false;
			settings::visuals::distance = false;
			settings::visuals::tool = false;
			settings::visuals::flags = false;
			settings::visuals::chams = false;
			settings::visuals::target_warning_icon = false;
			settings::visuals::client_box = false;
			settings::visuals::client_box_fill = false;
			settings::visuals::client_name = false;
			settings::visuals::client_avatar = false;
			settings::visuals::client_healthbar = false;
			settings::visuals::client_health_percent = false;
			settings::visuals::client_armorbar = false;
			settings::visuals::client_distance = false;
			settings::visuals::client_tool = false;
			settings::visuals::client_flags = false;
			settings::visuals::client_chams = false;
			settings::visuals::client_material = false;
			settings::visuals::client_headless = false;
			settings::visuals::client_korblox = false;
			settings::visuals::esp_font = 0;
			settings::visuals::name_display_type = 0;
			settings::visuals::chams_type = 1;
			settings::visuals::blend = false;
			settings::visuals::knock_check = false;
			settings::visuals::debug_wallcheck = false;
			settings::visuals::fade_in_speed = 5.0f;
			settings::visuals::fade_out_speed = 5.0f;
			settings::lighting::fog::enabled = false;
			settings::lighting::fog::fog_start = 0.0f;
			settings::lighting::fog::fog_end = 500.0f;
			settings::lighting::fog::fog_r = 0.75f;
			settings::lighting::fog::fog_g = 0.75f;
			settings::lighting::fog::fog_b = 0.75f;
			settings::lighting::exposure::enabled = false;
			settings::lighting::exposure::exposure = 0.0f;
			settings::lighting::skybox::enabled = false;
			settings::lighting::skybox::apply_skybox = false;
			settings::lighting::skybox::preset_index = 0;
			settings::lighting::skybox::skybox_bk.clear();
			settings::lighting::skybox::skybox_dn.clear();
			settings::lighting::skybox::skybox_ft.clear();
			settings::lighting::skybox::skybox_lf.clear();
			settings::lighting::skybox::skybox_rt.clear();
			settings::lighting::skybox::skybox_up.clear();
			settings::lighting::rain::enabled = false;
			settings::lighting::rain::density = 100.0f;
			settings::lighting::rain::speed = 650.0f;
			settings::lighting::rain::length = 18.0f;
			settings::lighting::rain::opacity = 0.30f;
			settings::lighting::clocktime::enabled = false;
			settings::lighting::clocktime::clock_time = 12.0f;
			return true;

		case section_t::rage:
			settings::movement::speedhack::enabled = false;
			settings::movement::speedhack::mode = 0;
			settings::movement::speedhack::speed = 50.0f;
			settings::movement::speedhack::keybind = 0;
			settings::movement::speedhack::activation_mode = 1;
			settings::movement::flyhack::enabled = false;
			settings::movement::flyhack::mode = 0;
			settings::movement::flyhack::speed = 50.0f;
			settings::movement::flyhack::keybind = 0;
			settings::movement::flyhack::activation_mode = 1;
			settings::movement::tickrate::enabled = false;
			settings::movement::tickrate::value = 240.0f;
			settings::movement::orbit::enabled = false;
			settings::movement::orbit::orbit_type = 0;
			settings::movement::orbit::speed = 30.0f;
			settings::movement::orbit::radius = 10.0f;
			settings::movement::orbit::height_offset = 10.0f;
			settings::movement::orbit::spectate_target = false;
			settings::movement::orbit::randomize = false;
			settings::movement::orbit::randomize_x = 5.0f;
			settings::movement::orbit::randomize_y = 5.0f;
			settings::rage::hitsounds = false;
			settings::rage::hitsound_type = 0;
			settings::rage::hitsound_method = 0;
			settings::rage::hitsound_volume = 100.0f;
			settings::rage::rapidfire = false;
			settings::rage::noclip = false;
			settings::rage::hit_tracers = false;
			settings::rage::hit_tracers_duration = 1.0f;
			settings::rage::hitbox_expander::enabled = false;
			settings::rage::hitbox_expander::size_x = 2.2f;
			settings::rage::hitbox_expander::size_y = 2.2f;
			settings::rage::hitbox_expander::size_z = 1.2f;
			settings::rage::hitbox_expander::knock_check = false;
			settings::rage::spin360::enabled = false;
			settings::rage::spin360::keybind = 0;
			settings::rage::spin360::activation_mode = 1;
			settings::rage::spin360::speed = 10.0f;
			settings::rage::spin360::mode = 0;
			settings::visuals::view_hitbox = false;
			return true;

		case section_t::settings:
			settings::menu::theme = 0;
			settings::menu::custom_theme_color[0] = 0.72f; settings::menu::custom_theme_color[1] = 0.94f; settings::menu::custom_theme_color[2] = 0.72f; settings::menu::custom_theme_color[3] = 1.0f;
			settings::menu::menu_keybind = VK_INSERT;
			settings::menu::watermark = false;
			settings::menu::streamproof = false;
			settings::menu::streamproof_keybind = 0;
			settings::menu::streamproof_activation_mode = 2;
			settings::menu::vsync = false;
			settings::menu::hide_console = false;
			settings::menu::performance_mode = false;
			settings::ui::keybinds = true;
			settings::ui::notifications = true;
			settings::ui::hit_logs = true;
			settings::ui::tooltips = true;
			settings::ui::streamproof_indicator = true;
			settings::ui::keybinds_mode = 1;
			settings::ui::overlay_preset = 0;
			settings::ui::ui_scale = 1.0f;
			settings::ui::watermark_position[0] = 20.0f; settings::ui::watermark_position[1] = 20.0f;
			settings::ui::keybinds_position[0] = 20.0f; settings::ui::keybinds_position[1] = 100.0f;
			settings::ui::hitlog_position[0] = 20.0f; settings::ui::hitlog_position[1] = 180.0f;
			settings::ui::favorites_mask = 0;
			settings::custom_entities::show_custom_entities = false;
			settings::custom_entities::auto_refresh = false;
			settings::custom_entities::refresh_rate = 0.005f;
			settings::custom_entities::current_input = "Workspace.Bots";
			return true;
		}

		return false;
	}
}
