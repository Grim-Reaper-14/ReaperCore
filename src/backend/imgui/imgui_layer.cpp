#include "reapercore/backend/imgui/imgui_layer.hpp"
#include "reapercore/core/logging/logging_manager.hpp"

#include <imgui.h>

#include <exception>
#include <utility>
#include <vector>

namespace reapercore
{
    ImGui_Layer::~ImGui_Layer()
    {
        shutdown();
    }

    bool ImGui_Layer::initialize(Logging_Manager& logging) noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (m_initialized.load(std::memory_order_acquire))
            return true;

        IMGUI_CHECKVERSION();
        m_context = ImGui::CreateContext();
        if (m_context == nullptr)
            return false;

        m_logging = &logging;
        ImGui::SetCurrentContext(m_context);

        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;

        m_frame_active.store(false, std::memory_order_release);
        m_initialized.store(true, std::memory_order_release);

        m_logging->info("imgui", "Dear ImGui context initialized.", {
            {"version", IMGUI_VERSION}
        });
        return true;
    }

    void ImGui_Layer::shutdown() noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (!m_initialized.exchange(false, std::memory_order_acq_rel))
            return;

        ImGui::SetCurrentContext(m_context);
        if (m_frame_active.exchange(false, std::memory_order_acq_rel))
            ImGui::EndFrame();

        clear_draw_callbacks();
        ImGui::DestroyContext(m_context);
        m_context = nullptr;

        if (m_logging != nullptr)
            m_logging->info("imgui", "Dear ImGui context stopped.");
        m_logging = nullptr;
    }

    bool ImGui_Layer::begin_frame() noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        return begin_frame_unlocked();
    }

    bool ImGui_Layer::begin_frame(
        const float delta_seconds,
        const float display_width,
        const float display_height) noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (!initialized() || m_context == nullptr ||
            display_width <= 0.0F || display_height <= 0.0F)
        {
            return false;
        }

        ImGui::SetCurrentContext(m_context);
        auto& io = ImGui::GetIO();
        io.DeltaTime = delta_seconds > 0.0F ? delta_seconds : (1.0F / 60.0F);
        io.DisplaySize = ImVec2(display_width, display_height);
        return begin_frame_unlocked();
    }

    bool ImGui_Layer::begin_frame_unlocked() noexcept
    {
        if (!initialized() || m_context == nullptr)
            return false;

        bool expected = false;
        if (!m_frame_active.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel))
        {
            return false;
        }

        ImGui::SetCurrentContext(m_context);
        ImGui::NewFrame();
        return true;
    }

    void ImGui_Layer::end_frame() noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (!initialized() || m_context == nullptr ||
            !m_frame_active.load(std::memory_order_acquire))
        {
            return;
        }

        ImGui::SetCurrentContext(m_context);
        dispatch_draw_callbacks();
        ImGui::Render();
        m_frame_active.store(false, std::memory_order_release);
    }

    void ImGui_Layer::cancel_frame() noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (!initialized() || m_context == nullptr ||
            !m_frame_active.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        ImGui::SetCurrentContext(m_context);
        ImGui::EndFrame();
    }

    ImGui_Layer::Draw_Callback_Id ImGui_Layer::add_draw_callback(
        Draw_Callback callback)
    {
        if (!callback)
            return 0;

        const auto id = m_next_callback_id.fetch_add(1, std::memory_order_relaxed);
        std::scoped_lock lock(m_callback_mutex);
        m_draw_callbacks.emplace(id, std::move(callback));
        return id;
    }

    bool ImGui_Layer::remove_draw_callback(const Draw_Callback_Id id) noexcept
    {
        if (id == 0)
            return false;

        std::scoped_lock lock(m_callback_mutex);
        return m_draw_callbacks.erase(id) != 0;
    }

    void ImGui_Layer::clear_draw_callbacks() noexcept
    {
        std::scoped_lock lock(m_callback_mutex);
        m_draw_callbacks.clear();
    }

    void ImGui_Layer::dispatch_draw_callbacks() noexcept
    {
        std::vector<Draw_Callback> callbacks;
        {
            std::scoped_lock lock(m_callback_mutex);
            callbacks.reserve(m_draw_callbacks.size());
            for (const auto& [_, callback] : m_draw_callbacks)
                callbacks.push_back(callback);
        }

        for (auto& callback : callbacks)
        {
            try
            {
                callback();
            }
            catch (const std::exception& exception)
            {
                if (m_logging != nullptr)
                    m_logging->log_exception(
                        "imgui",
                        exception,
                        "Dear ImGui draw callback failed.");
            }
            catch (...)
            {
                if (m_logging != nullptr)
                    m_logging->error(
                        "imgui",
                        "Dear ImGui draw callback failed with an unknown exception.");
            }
        }
    }

    void ImGui_Layer::make_current() noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (m_context != nullptr)
            ImGui::SetCurrentContext(m_context);
    }

    bool ImGui_Layer::initialized() const noexcept
    {
        return m_initialized.load(std::memory_order_acquire);
    }

    bool ImGui_Layer::frame_active() const noexcept
    {
        return m_frame_active.load(std::memory_order_acquire);
    }

    bool ImGui_Layer::wants_mouse() const noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (!initialized() || m_context == nullptr)
            return false;

        ImGui::SetCurrentContext(m_context);
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool ImGui_Layer::wants_keyboard() const noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (!initialized() || m_context == nullptr)
            return false;

        ImGui::SetCurrentContext(m_context);
        return ImGui::GetIO().WantCaptureKeyboard;
    }

    bool ImGui_Layer::wants_text_input() const noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (!initialized() || m_context == nullptr)
            return false;

        ImGui::SetCurrentContext(m_context);
        return ImGui::GetIO().WantTextInput;
    }

    std::size_t ImGui_Layer::callback_count() const noexcept
    {
        std::scoped_lock lock(m_callback_mutex);
        return m_draw_callbacks.size();
    }

    ImGuiContext* ImGui_Layer::context() noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        return m_context;
    }

    const ImGuiContext* ImGui_Layer::context() const noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        return m_context;
    }

    ImDrawData* ImGui_Layer::draw_data() noexcept
    {
        std::scoped_lock lock(m_context_mutex);
        if (!initialized() || m_context == nullptr || frame_active())
            return nullptr;

        ImGui::SetCurrentContext(m_context);
        return ImGui::GetDrawData();
    }

    std::string_view ImGui_Layer::version() const noexcept
    {
        return IMGUI_VERSION;
    }
}
