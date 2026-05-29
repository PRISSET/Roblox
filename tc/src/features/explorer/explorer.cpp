#include "explorer.h"
#include "globals.h"
#include "decompiler/decompiler.h"

#include <thread>
#include <chrono>
#include <atomic>

#include <game/game.h>
#include "../../../ext/imgui/imgui.h"
#include "../../../ext/imgui/imgui_internal.h"
#include "../../../ext/imgui/addons/imgui_addons.h"
#include <render/render.h>
#include <memory/memory.h>
#include <sdk/offsets.h>

void explorer::explorer_t::free_tree(explorer::node_t* node)
{
	if (!node)
	{
		return;
	}

	for (explorer::node_t* child : node->children)
	{
		if (child)
		{
			free_tree(child);
			delete child;
		}
	}

	node->children.clear();
}

void explorer::run()
{
	if (game::datamodel.address)
	{
		explorer::explorer->cache();
	}
}

void explorer::explorer_t::cache()
{
	node_t* old_root = root;
	node_t* old_selected = selected_node;

	selected_node = nullptr;
	root = nullptr;

	if (old_root)
	{
		free_tree(old_root);
		delete old_root;
	}

	if (!game::datamodel.address)
	{
		is_refreshing = false;
		return;
	}

	root = new node_t{};
	root->parent = nullptr;
	root->self = game::datamodel;
	root->name = root->self.get_name();
	root->class_name = root->self.get_class_name();

	auto build_tree = [&](node_t* parent, rbx::instance_t& instance, auto& self_ref) -> void
		{
			if (!parent)
			{
				return;
			}

			node_t* child_node = new node_t{};
			child_node->parent = parent;
			child_node->self = instance;
			child_node->name = instance.get_name();
			child_node->class_name = instance.get_class_name();

			parent->children.push_back(child_node);

			auto children = instance.get_children();
			for (auto& child_instance : children)
			{
				self_ref(child_node, child_instance, self_ref);
			}
		};

	auto children = game::datamodel.get_children();
	for (auto& instance : children)
	{
		build_tree(root, instance, build_tree);
	}

	is_refreshing = false;
}

void explorer::explorer_t::render_node(node_t* node)
{
	if (!node || !root)
	{
		return;
	}

	if (!textures_loaded)
	{
		load_all_icons();
	}

	icon_texture_t* icon = get_icon_for_classname(node->class_name);
	float icon_size = ImGui::GetFontSize();

	char buf[256];
	snprintf(buf, sizeof(buf), "%s [%s]",
		node->name.c_str(),
		node->class_name.c_str()
	);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

	ImVec4 accent_color = ImGui::GetStyle().Colors[ImGuiCol_SliderGrab];
	ImVec4 hover_color = ImVec4(accent_color.x, accent_color.y, accent_color.z, 0.3f);

	bool is_selected = (node == selected_node);
	int colors_pushed = 0;

	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hover_color);
	colors_pushed++;

	if (node->children.empty())
	{
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}
	if (is_selected)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
		ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_SliderGrab]);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyle().Colors[ImGuiCol_SliderGrabActive]);
		colors_pushed += 2;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 5.f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 1.0f));

	bool open = ImGui::TreeNodeEx((void*)node, flags, "");

	if (ImGui::IsItemClicked())
	{
		selected_node = node;

		if (node->class_name.find("Script") != std::string::npos && ImGui::IsMouseDoubleClicked(0))
		{
			bool found = false;
			for (auto& script : globals::decompiled_scripts)
			{
				if (script.title == ("Script_" + std::to_string(node->self.address)))
				{
					script.open = true;
					found = true;
					break;
				}
			}

			if (!found)
			{
				decompiler::decompiler_t decompiler;
				decompiler.decompile_script(node->self.address, decompiler::LocalScript);
			}
		}
	}

	if (icon && icon->texture)
	{
		ImGui::SameLine(0, 0.0f);
		ImGui::Image((void*)icon->texture, ImVec2(icon_size, icon_size));
		ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
	}

	ImGui::Text("%s", buf);

	ImGui::PopStyleVar(2);

	ImGui::PopStyleColor(colors_pushed);

	if (open)
	{
		for (node_t* child : node->children)
		{
			render_node(child);
		}

		if (!node->children.empty())
		{
			ImGui::TreePop();
		}
	}
}

void explorer::explorer_t::render_properties()
{
	if (this->selected_node != nullptr)
	{
		this->render_path();
		this->render_address();

		std::string class_name = this->selected_node->self.get_class_name();

		if (class_name.find("Part") != std::string::npos)
		{
			this->render_part_properties();
		}
		else if (class_name.find("Script") != std::string::npos)
		{
            ImGui::Spacing();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
            decompiler::script_type script_type = decompiler::script_type::LocalScript;
            if (class_name == "ModuleScript")
            {
                script_type = decompiler::script_type::ModuleScript;
            }
            if (ImAdd::ButtonAccent("Decompile Script"))
            {
                std::thread([&]() { decompiler::decompiler_t decompiler; decompiler.decompile_script(this->selected_node->self.address, script_type); }).detach();
            }
            ImGui::SameLine();
            if (ImAdd::ButtonAccent("Disassemble Script"))
            {
                std::thread([&]() { decompiler::decompiler_t decompiler; decompiler.disassemble_script(this->selected_node->self.address, script_type); }).detach();
            }
            ImGui::PopStyleVar();
            ImGui::Spacing();
			this->render_script_properties(class_name);
		}
	}
	else
	{
		ImGui::TextDisabled("No node selected");
	}
}

void explorer::explorer_t::render_settings()
{
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
	
	bool refreshing = is_refreshing.load();
	if (refreshing)
	{
		ImGui::BeginDisabled();
	}
	
	if (ImAdd::ButtonAccent("Refresh Explorer"))
	{
		if (!is_refreshing.exchange(true))
		{
			std::thread([&]() 
			{ 
				explorer::explorer->cache();
			}).detach();
		}
	}
	
	if (refreshing)
	{
		ImGui::EndDisabled();
	}
	
	ImGui::PopStyleVar();
}

void explorer::explorer_t::render_path()
{
	std::string path = this->selected_node->get_path();
	
	ImGuiStyle& style = ImGui::GetStyle();
	float available_width = ImGui::GetContentRegionAvail().x;
	
	const char* path_label = "Path: ";
	ImVec2 label_size = ImGui::CalcTextSize(path_label);
	
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
	ImVec2 button_size = {
		ImGui::CalcTextSize("copy").x + ImVec2(style.FramePadding.x * 2.0f, style.FramePadding.y * 2.0f).x,
		ImGui::CalcTextSize("copy").y + ImVec2(style.FramePadding.x * 2.0f, style.FramePadding.y * 2.0f).y
	};
	ImGui::PopStyleVar();
	
	float max_path_width = available_width - label_size.x - button_size.x - style.ItemSpacing.x;
	
	std::string display_path = path;
	ImVec2 path_text_size = ImGui::CalcTextSize(path.c_str());
	
	if (path_text_size.x > max_path_width)
	{
		display_path.clear();
		const char* path_cstr = path.c_str();
		int path_len = static_cast<int>(path.length());
		
		for (int i = path_len; i > 0; --i)
		{
			std::string test_path = "..." + std::string(path_cstr + (path_len - i));
			ImVec2 test_size = ImGui::CalcTextSize(test_path.c_str());
			
			if (test_size.x <= max_path_width)
			{
				display_path = test_path;
				break;
			}
		}
		
		if (display_path.empty())
		{
			display_path = "...";
		}
	}

	ImGui::Text("%s%s", path_label, display_path.c_str());
	ImGui::SameLine();

	if (ImAdd::ButtonAccent("copy##path"))
	{
		ImGui::SetClipboardText(path.c_str());
	}
}

void explorer::explorer_t::render_address()
{
	std::uint64_t address = this->selected_node->self.address;

	ImGui::Text("Address: 0x%llx", address);
	ImGui::SameLine();

	if (ImAdd::ButtonAccent("copy##address"))
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "0x%llx", address);
		ImGui::SetClipboardText(buf);
	}
}

void explorer::explorer_t::render_part_properties()
{
	rbx::instance_t instance = this->selected_node->self;
	rbx::part_t part = { instance.address };
	rbx::primitive_t primitive = part.get_primitive();
	
	if (primitive.address == 0)
	{
		return;
	}

	math::vector3 position = primitive.get_position();
	math::vector3 size = primitive.get_size();

	ImGui::Text("Position: %.2f, %.2f, %.2f", position.x, position.y, position.z);
	ImGui::SameLine();

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0));
	if (ImGui::Button("copy##position"))
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f", position.x, position.y, position.z);
		ImGui::SetClipboardText(buf);
	}
	ImGui::PopStyleVar();

	ImGui::Text("Size: %.2f, %.2f, %.2f", size.x, size.y, size.z);
	ImGui::SameLine();

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0));
	if (ImGui::Button("copy##size"))
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f", size.x, size.y, size.z);
		ImGui::SetClipboardText(buf);
	}
	ImGui::PopStyleVar();
}

void explorer::explorer_t::render_special_mesh_properties()
{
}

void explorer::explorer_t::render_script_properties(std::string_view class_name)
{
    if (globals::decompiled_scripts.empty())
        return;

    for (size_t i = 0; i < globals::decompiled_scripts.size(); ++i)
    {
        auto& script = globals::decompiled_scripts[i];

        if (!script.open)
            continue;

        ImGui::SetNextWindowSize(ImVec2(700, 800), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(script.title.c_str(), &script.open, ImGuiWindowFlags_HorizontalScrollbar))
        {
            script.editor.Render("TextEditor");
        }
        ImGui::End();
    }
}

void explorer::explorer_t::render_value_holder_properties(std::string_view class_name)
{
}

