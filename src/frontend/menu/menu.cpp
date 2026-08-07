#include "reapercore/frontend/menu/menu.hpp"

#include <Windows.h>
#include <imgui.h>

#include <utility>

namespace reapercore
{
    void Frontend_Menu::set_unload_callback(Unload_Callback callback)
    {
        m_unload_callback = std::move(callback);
    }

    void Frontend_Menu::draw() noexcept
    {
        if ((GetAsyncKeyState(VK_F5) & 1) != 0)
            m_open = !m_open;

        if (!m_open)
            return;

        apply_default_style();

        ImGui::SetNextWindowSize(ImVec2(980.0F, 650.0F), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(1.0F);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;

        if (!ImGui::Begin("##ReaperCoreFrontend", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        draw_header();

        constexpr float footer_height = 58.0F;
        const float body_height = ImGui::GetContentRegionAvail().y - footer_height;
        draw_sidebar(body_height);
        ImGui::SameLine(0.0F, 0.0F);
        draw_content(body_height);
        draw_footer();
        draw_unload_confirmation();

        ImGui::End();
    }

    bool Frontend_Menu::open() const noexcept
    {
        return m_open;
    }

    void Frontend_Menu::set_open(const bool open) noexcept
    {
        m_open = open;
    }

    Frontend_Menu_Page Frontend_Menu::page() const noexcept
    {
        return m_page;
    }

    void Frontend_Menu::set_page(const Frontend_Menu_Page page) noexcept
    {
        m_page = page;
    }

    void Frontend_Menu::apply_default_style() noexcept
    {
        auto& style = ImGui::GetStyle();
        style.WindowPadding = ImVec2(0.0F, 0.0F);
        style.WindowRounding = 10.0F;
        style.WindowBorderSize = 1.0F;
        style.ChildRounding = 7.0F;
        style.FrameRounding = 6.0F;
        style.ItemSpacing = ImVec2(8.0F, 8.0F);

        auto* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.027F, 0.039F, 0.059F, 1.00F);
        colors[ImGuiCol_ChildBg] = ImVec4(0.039F, 0.059F, 0.090F, 1.00F);
        colors[ImGuiCol_Border] = ImVec4(0.094F, 0.145F, 0.212F, 1.00F);
        colors[ImGuiCol_Text] = ImVec4(0.902F, 0.929F, 0.961F, 1.00F);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.451F, 0.514F, 0.592F, 1.00F);
        colors[ImGuiCol_Button] = ImVec4(0.051F, 0.078F, 0.118F, 1.00F);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.078F, 0.235F, 0.420F, 1.00F);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.137F, 0.553F, 1.000F, 1.00F);
    }

    void Frontend_Menu::draw_header() noexcept
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        constexpr float height = 128.0F;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilledMultiColor(
            start,
            ImVec2(start.x + width, start.y + height),
            IM_COL32(3, 8, 15, 255),
            IM_COL32(7, 31, 58, 255),
            IM_COL32(3, 12, 23, 255),
            IM_COL32(2, 6, 12, 255));

        const ImVec2 reaper_center(start.x + width - 105.0F, start.y + 67.0F);
        draw_list->AddCircleFilled(reaper_center, 42.0F, IM_COL32(4, 7, 12, 225));
        draw_list->AddTriangleFilled(
            ImVec2(reaper_center.x - 60.0F, reaper_center.y + 48.0F),
            ImVec2(reaper_center.x + 60.0F, reaper_center.y + 48.0F),
            ImVec2(reaper_center.x, reaper_center.y - 58.0F),
            IM_COL32(5, 9, 15, 235));
        draw_list->AddCircleFilled(
            ImVec2(reaper_center.x - 13.0F, reaper_center.y - 2.0F),
            4.0F,
            IM_COL32(35, 141, 255, 255));
        draw_list->AddCircleFilled(
            ImVec2(reaper_center.x + 13.0F, reaper_center.y - 2.0F),
            4.0F,
            IM_COL32(35, 141, 255, 255));

        ImGui::SetCursorScreenPos(ImVec2(start.x + 28.0F, start.y + 31.0F));
        ImGui::TextColored(ImVec4(0.137F, 0.553F, 1.000F, 1.0F), "REAPERCORE");
        ImGui::SetCursorScreenPos(ImVec2(start.x + 29.0F, start.y + 60.0F));
        ImGui::TextDisabled("GTA Enhanced Framework");

        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height));
        ImGui::Dummy(ImVec2(width, 1.0F));
    }

    void Frontend_Menu::draw_sidebar(const float body_height) noexcept
    {
        constexpr float sidebar_width = 168.0F;
        ImGui::BeginChild(
            "##FrontendSidebar",
            ImVec2(sidebar_width, body_height),
            ImGuiChildFlags_Borders);

        ImGui::Dummy(ImVec2(0.0F, 10.0F));
        for (const auto& item : s_navigation)
            sidebar_button(item);

        const float settings_y = ImGui::GetWindowHeight() - 58.0F;
        if (ImGui::GetCursorPosY() < settings_y)
            ImGui::SetCursorPosY(settings_y);

        sidebar_button({Frontend_Menu_Page::settings, "G", "Settings"});
        ImGui::EndChild();
    }

    void Frontend_Menu::draw_content(const float body_height) noexcept
    {
        ImGui::BeginChild(
            "##FrontendContent",
            ImVec2(0.0F, body_height),
            ImGuiChildFlags_Borders);

        ImGui::SetCursorPos(ImVec2(24.0F, 22.0F));
        ImGui::TextColored(
            ImVec4(0.137F, 0.553F, 1.000F, 1.0F),
            "%s",
            page_title());
        ImGui::SetCursorPosX(24.0F);
        draw_current_page();

        ImGui::EndChild();
    }

    void Frontend_Menu::draw_footer() noexcept
    {
        ImGui::BeginChild(
            "##FrontendFooter",
            ImVec2(0.0F, 58.0F),
            ImGuiChildFlags_Borders);

        ImGui::SetCursorPos(ImVec2(18.0F, 12.0F));
        ImGui::TextColored(ImVec4(0.137F, 0.553F, 1.000F, 1.0F), "ReaperCore");
        ImGui::SameLine();
        ImGui::TextDisabled("- GTA Enhanced native framework - customizable - scriptable");
        ImGui::SetCursorPos(ImVec2(18.0F, 34.0F));
        ImGui::TextDisabled("v0.3.0  |  F5: Toggle Menu");

        ImGui::EndChild();
    }

    void Frontend_Menu::draw_unload_confirmation() noexcept
    {
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

    void Frontend_Menu::sidebar_button(const Navigation_Item& item) noexcept
    {
        const bool selected = m_page == item.page;
        ImGui::PushID(static_cast<int>(item.page));
        ImGui::SetCursorPosX(10.0F);

        if (selected)
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                cursor,
                ImVec2(cursor.x + 3.0F, cursor.y + 38.0F),
                IM_COL32(35, 141, 255, 255),
                2.0F);
        }

        if (ImGui::Selectable(
                "##nav",
                selected,
                0,
                ImVec2(ImGui::GetContentRegionAvail().x - 10.0F, 38.0F)))
        {
            m_page = item.page;
        }

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImU32 icon_color = selected
            ? IM_COL32(35, 141, 255, 255)
            : IM_COL32(180, 194, 211, 255);
        const ImU32 text_color = selected
            ? IM_COL32(230, 237, 245, 255)
            : IM_COL32(155, 170, 190, 255);

        auto* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddText(ImVec2(min.x + 14.0F, min.y + 10.0F), icon_color, item.icon);
        draw_list->AddText(ImVec2(min.x + 42.0F, min.y + 10.0F), text_color, item.label);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", item.label);

        ImGui::PopID();
    }

    void Frontend_Menu::draw_current_page() noexcept
    {
        if (m_page != Frontend_Menu_Page::settings)
        {
            ImGui::TextDisabled("Frontend page module will render here.");
            return;
        }

        ImGui::TextDisabled("Settings modules will include General, Appearance, Themes, Fonts, Images, and Style Editor.");
        ImGui::Dummy(ImVec2(0.0F, 24.0F));
        ImGui::SeparatorText("System");
        ImGui::TextDisabled("Unload ReaperCore and cleanly release hooks and renderer resources.");

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45F, 0.06F, 0.08F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65F, 0.08F, 0.11F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.78F, 0.10F, 0.13F, 1.0F));
        if (ImGui::Button("Unload ReaperCore", ImVec2(180.0F, 38.0F)))
            ImGui::OpenPopup("Unload ReaperCore?");
        ImGui::PopStyleColor(3);
    }

    const char* Frontend_Menu::page_title() const noexcept
    {
        switch (m_page)
        {
        case Frontend_Menu_Page::home: return "Home";
        case Frontend_Menu_Page::self: return "Self";
        case Frontend_Menu_Page::vehicle: return "Vehicle";
        case Frontend_Menu_Page::world: return "World";
        case Frontend_Menu_Page::network: return "Network";
        case Frontend_Menu_Page::scripts: return "Scripts";
        case Frontend_Menu_Page::misc: return "Misc";
        case Frontend_Menu_Page::settings: return "Settings";
        }

        return "Home";
    }
}
