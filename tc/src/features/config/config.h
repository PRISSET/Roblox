#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace config
{
	struct config_info_t
	{
		std::string name;
		std::string path;
		std::string modified_display;
		std::int64_t modified_time{ 0 };
	};

	enum class section_t
	{
		aimbot,
		silentaim,
		visuals,
		rage,
		settings
	};

	std::string get_config_directory();
	std::string get_config_path(const std::string& name);
	bool ensure_config_directory();
	
	std::vector<config_info_t> get_config_list();
	bool save_config(const std::string& name);
	bool load_config(const std::string& name);
	bool delete_config(const std::string& name);
	bool config_exists(const std::string& name);
	void open_file_location();
	bool export_current_config_to_clipboard();
	bool import_config_from_clipboard(const std::string& name, bool save_to_file);
	bool reset_section(section_t section);
}
