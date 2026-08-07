#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace reapercore
{
    struct Native_Vector3 final
    {
        float x{};
        float y{};
        float z{};
    };

    struct Native_Call_Context_Layout;

    class Native_Call_Context
    {
    public:
        Native_Call_Context() noexcept
        {
            m_return_value = m_return_stack.data();
            m_arguments = m_argument_stack.data();
        }

        void reset() noexcept
        {
            m_argument_count = 0;
            m_vector_reference_count = 0;
            m_return_stack.fill(0);
            m_argument_stack.fill(0);
        }

        template <typename T>
        [[nodiscard]] bool push(T value) noexcept
        {
            using Value = std::remove_cv_t<std::remove_reference_t<T>>;
            static_assert(sizeof(Value) <= sizeof(std::uint64_t));
            static_assert(std::is_trivially_copyable_v<Value>);

            if (m_argument_count >= m_argument_stack.size())
                return false;

            std::memcpy(
                &m_argument_stack[m_argument_count],
                &value,
                sizeof(Value));
            ++m_argument_count;
            return true;
        }

        template <typename T>
        [[nodiscard]] T result() const noexcept
        {
            using Value = std::remove_cv_t<std::remove_reference_t<T>>;
            static_assert(sizeof(Value) <= sizeof(std::uint64_t));
            static_assert(std::is_trivially_copyable_v<Value>);

            Value value{};
            std::memcpy(&value, m_return_stack.data(), sizeof(Value));
            return value;
        }

        void fix_vectors() noexcept
        {
            for (std::int32_t index = 0;
                index < m_vector_reference_count && index < 4;
                ++index)
            {
                if (m_vector_reference_targets[index] != nullptr)
                {
                    *m_vector_reference_targets[index] =
                        m_vector_reference_sources[index];
                }
            }
            m_vector_reference_count = 0;
        }

    protected:
        friend struct Native_Call_Context_Layout;

        void* m_return_value{};                              // 0x00
        std::uint32_t m_argument_count{};                    // 0x08
        std::uint32_t m_padding_0C{};                        // 0x0C
        void* m_arguments{};                                 // 0x10
        std::int32_t m_vector_reference_count{};             // 0x18
        std::uint32_t m_padding_1C{};                        // 0x1C
        Native_Vector3* m_vector_reference_targets[4]{};     // 0x20
        Native_Vector3 m_vector_reference_sources[4]{};      // 0x40
        std::array<std::uint64_t, 10> m_return_stack{};
        std::array<std::uint64_t, 40> m_argument_stack{};
    };

    struct Native_Call_Context_Layout final
    {
        static constexpr std::size_t return_value =
            offsetof(Native_Call_Context, m_return_value);
        static constexpr std::size_t argument_count =
            offsetof(Native_Call_Context, m_argument_count);
        static constexpr std::size_t arguments =
            offsetof(Native_Call_Context, m_arguments);
        static constexpr std::size_t vector_reference_count =
            offsetof(Native_Call_Context, m_vector_reference_count);
        static constexpr std::size_t vector_reference_targets =
            offsetof(Native_Call_Context, m_vector_reference_targets);
        static constexpr std::size_t vector_reference_sources =
            offsetof(Native_Call_Context, m_vector_reference_sources);
    };

    static_assert(std::is_standard_layout_v<Native_Call_Context>);
    static_assert(Native_Call_Context_Layout::return_value == 0x00);
    static_assert(Native_Call_Context_Layout::argument_count == 0x08);
    static_assert(Native_Call_Context_Layout::arguments == 0x10);
    static_assert(Native_Call_Context_Layout::vector_reference_count == 0x18);
    static_assert(Native_Call_Context_Layout::vector_reference_targets == 0x20);
    static_assert(Native_Call_Context_Layout::vector_reference_sources == 0x40);

    using Native_Hash = std::uint64_t;
    using Native_Handler = void (*)(Native_Call_Context*);
}
