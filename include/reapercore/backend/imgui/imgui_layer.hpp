#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string_view>

struct ImDrawData;
struct ImGuiContext;

namespace reapercore
{
    class Logging_Manager;

    class ImGui_Layer final
    {
    public:
        using Draw_Callback_Id = std::uint64_t;
        using Draw_Callback = std::function<void()>;

        ImGui_Layer() = default;
        ~ImGui_Layer();

        ImGui_Layer(const ImGui_Layer&) = delete;
        ImGui_Layer& operator=(const ImGui_Layer&) = delete;

        bool initialize(Logging_Manager& logging) noexcept;
        void shutdown() noexcept;

        bool begin_frame(
            float delta_seconds,
            float display_width,
            float display_height) noexcept;
        void end_frame() noexcept;

        [[nodiscard]] Draw_Callback_Id add_draw_callback(Draw_Callback callback);
        [[nodiscard]] bool remove_draw_callback(Draw_Callback_Id id) noexcept;
        void clear_draw_callbacks() noexcept;
        void dispatch_draw_callbacks() noexcept;

        void make_current() noexcept;

        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] bool frame_active() const noexcept;
        [[nodiscard]] std::size_t callback_count() const noexcept;
        [[nodiscard]] ImGuiContext* context() noexcept;
        [[nodiscard]] const ImGuiContext* context() const noexcept;
        [[nodiscard]] ImDrawData* draw_data() noexcept;
        [[nodiscard]] std::string_view version() const noexcept;

    private:
        Logging_Manager* m_logging{};
        ImGuiContext* m_context{};

        mutable std::mutex m_context_mutex;
        mutable std::mutex m_callback_mutex;
        std::map<Draw_Callback_Id, Draw_Callback> m_draw_callbacks;

        std::atomic_bool m_initialized{false};
        std::atomic_bool m_frame_active{false};
        std::atomic_uint64_t m_next_callback_id{1};
    };
}
