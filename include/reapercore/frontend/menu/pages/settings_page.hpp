#pragma once

#include <functional>

namespace reapercore
{
    class Settings_Page final
    {
    public:
        using Unload_Callback = std::function<void()>;

        void set_unload_callback(Unload_Callback callback);
        void draw() noexcept;

    private:
        Unload_Callback m_unload_callback;
    };
}
