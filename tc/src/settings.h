#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <sdk/sdk.h>
#include <cache/cache.h>

namespace settings
{

	namespace aimbot
	{
		inline bool enabled{ false };
		inline int keybind{ 0 };
		inline int activation_mode{ 1 };

		inline int mode{ 1 };

		inline int target_part{ 1 };
		inline bool air_part_enabled{ false };
		inline int air_part{ 1 };

		inline float fov{ 100.f };
		inline bool use_fov{ false };
		inline bool draw_fov{ false };
		inline float fov_circle_colour[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4]{ 0.f, 0.f, 0.f, 1.f };

		inline bool smoothing{ false };
		inline float smoothingx{ 10.f };
		inline float smoothingy{ 10.f };

		inline bool enable_prediction{ false };
		inline float prediction_x{ 10.f };
		inline float prediction_y{ 10.f };

		inline bool air_prediction_enabled{ false };
		inline float air_prediction_x{ 10.f };
		inline float air_prediction_y{ 10.f };

		inline int smoothing_style{ 0 };

		inline bool teamcheck{ false };
		inline bool knock_check{ false };
		inline bool sticky_aim{ false };

		inline bool health_check_enabled{ false };
		inline float min_health{ 0.0f };

		inline bool offset_enabled{ false };
		inline float offset_x{ 0.0f };
		inline float offset_y{ 0.0f };
	}

	namespace rage
	{
		inline bool hitsounds{ false };
		inline int hitsound_type{ 0 };
		inline int hitsound_method{ 0 };
		inline float hitsound_volume{ 100.0f };
		inline bool rapidfire{ false };
		inline bool noclip{ false };
		inline bool hit_tracers{ false };
		inline float hit_tracers_color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
		inline float hit_tracers_duration{ 1.0f };
		
		namespace hitbox_expander
		{
			inline bool enabled{ false };
			inline int target_part{ 1 };
			inline float size_x{ 2.2f };
			inline float size_y{ 2.2f };
			inline float size_z{ 1.2f };
			inline bool knock_check{ false };
		}

		namespace spin360
		{
			inline bool enabled{ false };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 };
			inline float speed{ 10.0f };
			inline int mode{ 0 };
		}
	}

	namespace custom_entities
	{
		struct custom_entity_t
		{
			rbx::instance_t instance;
			std::string name;
			std::string container_path;
			float distance = 0.f;
			std::unordered_map<std::string, cache::part_data_t> parts;
			cache::part_data_t root_part;
			cache::part_data_t head;
			bool enabled = true;
		};

		struct custom_container_t
		{
			std::string path;
			std::string name;
			bool enabled = true;
			std::vector<custom_entity_t> entities;
		};

		inline std::vector<custom_container_t> containers;
		inline std::string current_input = "Workspace.Bots";
		inline bool show_custom_entities = false;
		inline bool auto_refresh = false;
		inline float refresh_rate = 0.005f;
	}

	namespace silentaim
	{
		inline bool enabled{ false };
		inline int keybind{ 0 };
		inline int activation_mode{ 1 };

		inline int target_part{ 1 };

		inline float fov{ 100.f };
		inline bool use_fov{ false };
		inline bool draw_fov{ false };
		inline bool lerp_fov{ false };
		inline bool attach_fov_to_target{ false };
		inline float fov_circle_colour[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4]{ 0.f, 0.f, 0.f, 1.f };

		inline bool enable_prediction{ false };
		inline float prediction_x{ 10.f };
		inline float prediction_y{ 10.f };

		inline bool sticky_aim{ false };
		inline bool auto_switch{ false };
		inline bool spoof_mouse{ true };
		inline bool use_aimbot_target{ false };

		inline bool teamcheck{ false };
		inline bool guncheck{ false };
		inline bool knock_check{ false };

		inline int priorities{ 0 };
		inline bool health_check_enabled{ false };
		inline float min_health{ 0.0f };

		inline bool draw_target_dot{ false };
		inline float target_dot_color[4]{ 1.f, 0.f, 0.f, 1.f };
		inline float target_dot_size{ 4.0f };

		inline bool draw_snap_line{ false };
		inline float snap_line_color[4]{ 1.f, 1.f, 1.f, 1.f };
	}

	
	namespace visuals
	{
		inline bool enable_enemies{ false };
		inline bool enable_client{ false };
		inline bool teamcheck{ false };
		inline bool distance_check{ false };
		inline float max_distance{ 250.0f };
		
		inline bool box{ false };
		inline int box_type{ 0 };
		inline float box_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool box_fill{ false };
		inline float box_fill_color[4]{ 0.2f, 0.2f, 0.2f, 0.3f };

		inline bool name{ false };
		inline int name_type{ 0 };
		inline int name_display_type{ 0 };
		inline float name_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float name_color_blend_start[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float name_color_blend_end[4]{ 0.f, 0.f, 1.f, 1.f };
		inline bool blend{ false };
		inline bool avatar{ false };

		inline bool healthbar{ false };
		inline float healthbar_color[4]{ 0.f, 1.f, 0.f, 1.f };
		inline bool health_based_healthbar{ false };
		inline bool gradient_healthbar{ false };
		inline float gradient_healthbar_color_start[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float gradient_healthbar_color_end[4]{ 0.f, 1.f, 0.f, 1.f };
		inline bool health_percent{ false };
		inline float health_percent_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool armorbar{ false };
		inline float armorbar_color[4]{ 0.275f, 0.627f, 1.f, 1.f };

		inline bool distance{ false };
		inline int distance_measurement{ 0 };
		inline float distance_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool tool{ false };
		inline float tool_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline int esp_font{ 0 };
		inline bool local_player{ false };
		inline bool chams{ false };
		inline int chams_type{ 1 };
		inline float chams_fill_color[4]{ 1.f, 0.f, 0.f, 0.5f };
		inline float chams_outline_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool chams_fill_enabled{ true };
		inline bool chams_outline_enabled{ true };


		inline bool target_warning_icon{ false };
		inline float target_warning_icon_size{ 24.0f };

		inline bool flags{ false };
		inline float flags_state_colour[4]{ 1.f, 1.f, 1.f, 1.f };


		inline bool client_box{ false };
		inline float client_box_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool client_box_fill{ false };
		inline float client_box_fill_color[4]{ 0.2f, 0.2f, 0.2f, 0.3f };

		inline bool client_name{ false };
		inline float client_name_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool client_avatar{ false };

		inline bool client_healthbar{ false };
		inline float client_healthbar_color[4]{ 0.f, 1.f, 0.f, 1.f };
		inline bool client_health_percent{ false };
		inline float client_health_percent_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_armorbar{ false };
		inline float client_armorbar_color[4]{ 0.275f, 0.627f, 1.f, 1.f };

		inline bool client_distance{ false };
		inline float client_distance_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_tool{ false };
		inline float client_tool_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_chams{ false };
		inline float client_chams_fill_color[4]{ 1.f, 0.f, 0.f, 0.5f };
		inline float client_chams_outline_color[4]{ 1.f, 1.f, 1.f, 1.f };


		inline bool client_flags{ false };
		inline float client_flags_state_colour[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_material{ false };
		inline float client_material_color[3]{ 1.f, 0.f, 0.f };
		inline int16_t client_material_type{ 1584 };

		inline bool client_headless{ false };
		inline bool client_korblox{ false };

		inline bool esp_preview_auto_rotate{ true };
		inline bool esp_preview_enabled{ false };

		inline bool debug_wallcheck{ false };

		inline bool view_hitbox{ false };
		inline float view_hitbox_color[4]{ 1.f, 0.f, 0.f, 1.f };

		inline float fade_in_speed{ 5.0f };
		inline float fade_out_speed{ 5.0f };

		inline bool knock_check{ false };

		inline bool hit_tracers_enabled{ false };
		inline int hit_tracers_method{ 0 };
		inline int hit_tracers_type{ 0 };
		inline float hit_tracers_color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
		inline float hit_tracers_duration{ 1.0f };
	}

	namespace movement
	{
		namespace speedhack
		{
			inline bool enabled{ false };
			inline int mode{ 0 };
			inline float speed{ 50.0f };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 };
		}

		namespace flyhack
		{
			inline bool enabled{ false };
			inline int mode{ 0 };
			inline float speed{ 50.0f };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 };
		}

		namespace tickrate
		{
			inline bool enabled{ false };
			inline float value{ 240.0f };
		}

		namespace orbit
		{
			inline bool enabled{ false };
			inline int orbit_type{ 0 };
			inline float speed{ 30.0f };
			inline float radius{ 10.0f };
			inline float height_offset{ 10.0f };
			inline bool spectate_target{ false };
			inline bool randomize{ false };
			inline float randomize_x{ 5.0f };
			inline float randomize_y{ 5.0f };
		}
	}

	namespace ui
	{
		inline bool watermark{ true };
		inline bool keybinds{ true };
		inline bool notifications{ true };
		inline bool hit_logs{ true };
		inline bool tooltips{ true };
		inline bool streamproof_indicator{ true };
		inline int keybinds_mode{ 1 };
		inline int overlay_preset{ 0 };
		inline float ui_scale{ 1.0f };
		inline float watermark_position[2]{ 20.0f, 20.0f };
		inline float keybinds_position[2]{ 20.0f, 100.0f };
		inline float hitlog_position[2]{ 20.0f, 180.0f };
		inline std::uint32_t favorites_mask{ 0 };
	}

	namespace menu
	{
		inline int theme{ 0 };
		inline float custom_theme_color[4]{ 0.72f, 0.94f, 0.72f, 1.0f };
		inline int menu_keybind{ VK_INSERT };
		inline bool watermark{ false };
		inline bool streamproof{ false };
		inline int streamproof_keybind{ 0 };
		inline int streamproof_activation_mode{ 2 };
		inline bool vsync{ false };
		inline bool hide_console{ false };
		inline bool performance_mode{ false };
	}

	namespace lighting
	{
		namespace fog
		{
			inline bool enabled{ false };
			inline float fog_start{ 0.0f };
			inline float fog_end{ 500.0f };
			inline float fog_r{ 0.75f };
			inline float fog_g{ 0.75f };
			inline float fog_b{ 0.75f };
		}
		
		namespace exposure
		{
			inline bool enabled{ false };
			inline float exposure{ 0.0f };
		}

		namespace skybox
		{
			inline bool enabled{ false };
			inline bool apply_skybox{ false };
			inline int preset_index{ 0 };
			inline std::string skybox_bk{};
			inline std::string skybox_dn{};
			inline std::string skybox_ft{};
			inline std::string skybox_lf{};
			inline std::string skybox_rt{};
			inline std::string skybox_up{};
		}

		namespace rain
		{
			inline bool enabled{ false };
			inline float density{ 100.0f };
			inline float speed{ 650.0f };
			inline float length{ 18.0f };
			inline float opacity{ 0.30f };
		}

		namespace clocktime
		{
			inline bool enabled{ false };
			inline float clock_time{ 12.0f };
		}
	}

	namespace globals
	{
		inline bool is_game_active{ true };
	}
}
