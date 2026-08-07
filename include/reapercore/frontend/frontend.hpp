#pragma once

#include <cstddef>
#include <functional>

namespace reapercore
{
    class Backend;
    class Logging_Manager;

    class Frontend final
    {
    public:
        using Unload_Callback = std::function<void()>;

        bool initialize(
            Logging_Manager& logging,
            Backend& backend,
            Unload_Callback unload_callback) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] bool initialized() const noexcept;

    private:
        Logging_Manager* m_logging{};
        Backend* m_backend{};
        Unload_Callback m_unload_callback;
        std::size_t m_draw_callback_id{};
        bool m_initialized{};
    };
}
