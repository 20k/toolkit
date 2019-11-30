#ifndef RENDER_WINDOW_HPP_INCLUDED
#define RENDER_WINDOW_HPP_INCLUDED

#include <vec/vec.hpp>

struct GLFWwindow;

struct render_window
{
    render_window(vec2i dim, const std::string& window_title);

    vec2i get_window_size();

    GLFWwindow* window = nullptr;
};

#endif // RENDER_WINDOW_HPP_INCLUDED
