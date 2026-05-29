#include "fps.h"
#include "../../../render/render.h"
#include "../../../settings.h"
#include <windows.h>
#include <thread>
#include <chrono>

namespace menu
{
	namespace fps
	{
		void run()
		{
			using namespace std::chrono_literals;

			for (;;)
			{
				std::this_thread::sleep_for(16ms);

				if (!render || !render->detail || !render->detail->swap_chain)
					continue;

				if (settings::menu::unlock_fps)
				{
				}
				else if (settings::menu::fps_cap > 0)
				{
					static auto last_frame = std::chrono::steady_clock::now();
					auto now = std::chrono::steady_clock::now();
					auto frame_time = std::chrono::milliseconds(1000 / settings::menu::fps_cap);
					auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame);

					if (elapsed < frame_time)
					{
						std::this_thread::sleep_for(frame_time - elapsed);
					}

					last_frame = std::chrono::steady_clock::now();
				}
			}
		}
	}
}
