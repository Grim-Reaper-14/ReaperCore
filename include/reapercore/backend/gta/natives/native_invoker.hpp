#pragma once

#include "reapercore/backend/gta/natives/native_manager.hpp"

#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace reapercore
{
    class Native_Invoker final
    {
    public:
        explicit Native_Invoker(const Native_Manager& manager) noexcept :
            m_manager(manager)
        {
        }

        template <typename Return, typename... Args>
        [[nodiscard]] std::optional<Return> invoke(
            const std::size_t index,
            const bool fix_vectors,
            Args&&... args) const noexcept
        {
            static_assert(!std::is_void_v<Return>);

            const Native_Handler native_handler = m_manager.handler(index);
            if (native_handler == nullptr)
                return std::nullopt;

            Native_Call_Context context;
            context.reset();

            if (!(context.push(std::forward<Args>(args)) && ...))
                return std::nullopt;

            native_handler(&context);
            if (fix_vectors)
                context.fix_vectors();

            return context.result<Return>();
        }

        template <typename... Args>
        [[nodiscard]] bool invoke_void(
            const std::size_t index,
            const bool fix_vectors,
            Args&&... args) const noexcept
        {
            const Native_Handler native_handler = m_manager.handler(index);
            if (native_handler == nullptr)
                return false;

            Native_Call_Context context;
            context.reset();

            if (!(context.push(std::forward<Args>(args)) && ...))
                return false;

            native_handler(&context);
            if (fix_vectors)
                context.fix_vectors();

            return true;
        }

    private:
        const Native_Manager& m_manager;
    };
}
