#include "streamproof.h"
#include "../../../render/render.h"
#include "../../../settings.h"
#include "../../../menu/keybind/keybind.h"
#include <windows.h>

namespace menu
{
	namespace streamproof
	{
		bool is_active()
		{
			if (!settings::menu::streamproof)
			{
				return false;
			}

			if (settings::menu::streamproof_keybind == 0)
			{
				return true;
			}

			static keybind::keybind_t streamproof_kb{};
			streamproof_kb.key = settings::menu::streamproof_keybind;
			streamproof_kb.mode = static_cast<keybind::activation_mode>(settings::menu::streamproof_activation_mode);

			return keybind::is_active(streamproof_kb);
		}

		void run()
		{
			if (!render || !render->detail || !render->detail->window)
				return;

			if (is_active())
			{
				SetWindowDisplayAffinity(render->detail->window, WDA_EXCLUDEFROMCAPTURE);
			}
			else
			{
				SetWindowDisplayAffinity(render->detail->window, WDA_NONE);
			}
		}
	}
}
