#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace reapercore
{
    enum class Frontend_Menu_Page : std::uint8_t
    {
        home,
        self,
        vehicle,
        world,
        network,
        scripts,
        misc,
        settings
    };

    class Frontend_Menu final
    {
    public:
        using Unload_Callback = std::function<void()>;

        void set_unload_callback(Unload_Callback callback);
        void draw() noexcept;

        [[nodiscard]] bool open() const noexcept;
        void set_open(bool open) noexcept;

        [[nodiscard]] Frontend_Menu_Page page() const noexcept;
        void set_page(Frontend_Menu_Page page) noexcept;

    private:
        struct Navigation_Item final
        {
            Frontend_Menu_Page page;
            const char* icon;
            const char* label;
        };

        static constexpr std::array<Navigation_Item, 7> s_navigation{{
            {Frontend_Menu_Page::home, "H", "Home"},
            {Frontend_Menu_Page::self, "P", "Self"},
            {Frontend_Menu_Page::vehicle, "V", "Vehicle"},
            {Frontend_Menu_Page::world, "W", "World"},
            {Frontend_Menu_Page::network, "N", "Network"},
            {Frontend_Menu_Page::scripts, "S", "Scripts"},
            {Frontend_Menu_Page::misc, "M", "Misc"},
        }};

        void apply_default_style() noexcept;
        void draw_header() noexcept;
        void draw_sidebar(float body_height) noexcept;
        void draw_content(float body_height) noexcept;
        void draw_footer() noexcept;
        void draw_unload_confirmation() noexcept;
        void sidebar_button(const Navigation_Item& item) noexcept;
        void draw_current_page() noexcept;
        [[nodiscard]] const char* page_title() const noexcept;

        Frontend_Menu_Page m_page{Frontend_Menu_Page::home};
        Unload_Callback m_unload_callback;
        bool m_open{true};
    };
}
