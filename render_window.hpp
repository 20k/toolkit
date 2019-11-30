#ifndef RENDER_WINDOW_HPP_INCLUDED
#define RENDER_WINDOW_HPP_INCLUDED

#include <vec/vec.hpp>
#include <imgui/imgui.h>

struct GLFWwindow;

namespace window_flags
{
    enum window_flags
    {
        NONE = 0,
        SRGB = 1,
        DOUBLE_BUFFER = 2,
        VIEWPORTS = 4,
    };
}

struct render_window
{
    render_window(vec2i dim, const std::string& window_title, window_flags::window_flags flags = window_flags::NONE);

    vec2i get_window_size();
    vec2i get_window_position();

    void poll();
    void render();
    bool should_close();

    GLFWwindow* window = nullptr;
    ImFontAtlas atlas = {};

private:
    bool closing = false;
};

#endif // RENDER_WINDOW_HPP_INCLUDED
