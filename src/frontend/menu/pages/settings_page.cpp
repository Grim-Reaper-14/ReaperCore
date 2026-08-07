#include "reapercore/frontend/menu/pages/settings_page.hpp"

#include <imgui.h>

#include <utility>

namespace reapercore
{
    void Settings_Page::set_unload_callback(Unload_Callback callback)
    {
        m_unload_callback = std::move(callback);
    }

    void Settings_Page::draw() noexcept
    {
        ImGui::TextDisabled("Settings modules will include General, Appearance, Themes, Fonts, Images, Style Editor, and Configs.");
        ImGui::Dummy(ImVec2(0.0F, 24.0F));
        ImGui::SeparatorText("System");
        ImGui::TextDisabled("Unload ReaperCore and cleanly release hooks and renderer resources.");

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45F, 0.06F, 0.08F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65F, 0.08F, 0.11F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78F, 0.10F, 0.13F, 1.0F));
        if (ImGui::Button("Unload ReaperCore", ImVec2(180.0F, 38.0F)))
            ImGui::OpenPopup("Unload ReaperCore?");
        ImGui::PopStyleColor(3);

        if (!ImGui::BeginPopupModal(
                "Unload ReaperCore?",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextUnformatted("Are you sure you want to unload ReaperCore?");
        ImGui::TextDisabled("Hooks, native callbacks, ImGui, and renderer resources will be shut down cleanly.");
        ImGui::Separator();

        if (ImGui::Button("Unload", ImVec2(120.0F, 0.0F)))
        {
            ImGui::CloseCurrentPopup();
            if (m_unload_callback)
                m_unload_callback();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0F, 0.0F)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}
