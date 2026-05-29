#pragma once
#include <vector>
#include <string>

#include "../../ext/imgui/texteditor.hpp"

namespace globals
{
    struct decompiled_script_t final
    {
        std::string title;
        std::string code;
        TextEditor editor;
        bool open;
    };

    inline std::vector<decompiled_script_t> decompiled_scripts;
}
