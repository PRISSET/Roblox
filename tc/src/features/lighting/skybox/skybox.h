#pragma once

namespace lighting
{
	namespace skybox
	{
		struct skybox_preset_t
		{
			const char* bk;
			const char* dn;
			const char* ft;
			const char* lf;
			const char* rt;
			const char* up;
		};

		inline constexpr skybox_preset_t PRESETS[] = {
			{ "", "", "", "", "", "" },
			{
				"rbxassetid://12064107",
				"rbxassetid://12064152",
				"rbxassetid://12064121",
				"rbxassetid://12063984",
				"rbxassetid://12064115",
				"rbxassetid://12064131"
			},
			{
				"rbxassetid://12635309703",
				"rbxassetid://12635311686",
				"rbxassetid://12635312870",
				"rbxassetid://12635313718",
				"rbxassetid://12635315817",
				"rbxassetid://12635316856"
			},
			{
				"rbxassetid://599982473",
				"rbxassetid://599982473",
				"rbxassetid://599982473",
				"rbxassetid://599982473",
				"rbxassetid://599982473",
				"rbxassetid://599982473"
			},
			{
				"rbxassetid://116758234",
				"rbxassetid://116758314",
				"rbxassetid://116758367",
				"rbxassetid://116758446",
				"rbxassetid://116758478",
				"rbxassetid://116758496"
			},
			{
				"rbxassetid://1233158420",
				"rbxassetid://1233158838",
				"rbxassetid://1233157105",
				"rbxassetid://1233157640",
				"rbxassetid://1233157995",
				"rbxassetid://1233159158"
			},
			{
				"rbxassetid://1327358",
				"rbxassetid://1327359",
				"rbxassetid://1327355",
				"rbxassetid://1327357",
				"rbxassetid://1327356",
				"rbxassetid://1327360"
			},
			{
				"rbxassetid://570555736",
				"rbxassetid://570555964",
				"rbxassetid://570555800",
				"rbxassetid://570555840",
				"rbxassetid://570555882",
				"rbxassetid://570555929"
			},
			{
				"rbxassetid://95020137072033",
				"rbxassetid://92862258103959",
				"rbxassetid://107665368823185",
				"rbxassetid://126542804346203",
				"rbxassetid://103716549795832",
				"rbxassetid://131036626982613"
			},
			{
				"rbxassetid://169210090",
				"rbxassetid://169210108",
				"rbxassetid://169210121",
				"rbxassetid://169210133",
				"rbxassetid://169210143",
				"rbxassetid://169210149"
			},
			{
				"rbxassetid://4832115161",
				"rbxassetid://4832115161",
				"rbxassetid://4832115161",
				"rbxassetid://4832115161",
				"rbxassetid://4832115161",
				"rbxassetid://4832115161"
			}
		};

		void run();
	}
}
