#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace reapercore
{
    enum class Menu_Page : std::uint8_t
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

    class Menu final
    {
    public:
        using Unload_Callback = std::function<void()>;

        void set_unload_callback(Unload_Callback callback);
        void draw() noexcept;

        [[nodiscard]] bool open() const noexcept;
        void set_open(bool open) noexcept;
        [[nodiscard]] Menu_Page page() const noexcept;
        void set_page(Menu_Page page) noexcept;

    private:
        struct Navigation_Item final
        {
            Menu_Page page;
            const char* icon;
            const char* label;
        };

        static constexpr std::array<Navigation_Item, 7> s_main_navigation{{
            {Menu_Page::home, "H", "Home"},
            {Menu_Page::self, "P", "Self"},
            {Menu_Page::vehicle, "V", "Vehicle"},
            {Menu_Page::world, "W", "World"},
            {Menu_Page::network, "N", "Network"},
            {Menu_Page::scripts, "S", "Scripts"},
            {Menu_Page::misc, "M", "Misc"},
        }};

        void apply_default_style() noexcept;
        void draw_header() noexcept;
        void draw_body() noexcept;
        void draw_footer() noexcept;
        void draw_unload_confirmation() noexcept;
        void sidebar_button(const Navigation_Item& item) noexcept;
        [[nodiscard]] const char* page_title() const noexcept;

        Menu_Page m_page{Menu_Page::home};
        Unload_Callback m_unload_callback;
        bool m_open{true};
    };
}
