#ifndef RENDER_WINDOW_HPP_INCLUDED
#define RENDER_WINDOW_HPP_INCLUDED

#include <vec/vec.hpp>
#include <imgui/imgui.h>
#include "opencl.hpp"

struct GLFWwindow;
struct texture;
struct vertex;

namespace window_flags
{
    enum window_flags
    {
        NONE = 0,
        SRGB = 1,
        DOUBLE_BUFFER = 2,
        VIEWPORTS = 4,
        OPENCL = 8,
    };
}

struct frostable
{
    vec2f pos;
    vec2i dim;
};

struct render_window;

struct render_context
{
    unsigned int fbo;
    unsigned int screen_tex;

    GLFWwindow* window = nullptr;
    ImFontAtlas atlas = {};

    render_context(vec2i dim, const std::string& window_title, window_flags::window_flags flags);
};

struct opencl_context
{
    cl::context ctx;
    cl::gl_rendertexture cl_screen_tex;
    cl::command_queue cqueue;
    cl::image cl_image;

    opencl_context();
};

struct render_window
{
    render_context rctx;
    opencl_context* clctx = nullptr;

    render_window(vec2i dim, const std::string& window_title, window_flags::window_flags flags = window_flags::NONE);
    ~render_window();

    vec2i get_window_size();
    vec2i get_window_position();

    void poll(double maximum_sleep_s = 0);

    std::vector<frostable> get_frostables();

    void display();
    bool should_close();
    void close();

    void render(const std::vector<vertex>& vertices, texture* tex = nullptr);
    void render_texture(unsigned int handle, vec2f p_min, vec2f p_max);

private:
    bool closing = false;
    vec2i last_size;
};

namespace gui
{
    void frost(const std::string& window_name);

    namespace current
    {
        void frost();
    }
}

#endif // RENDER_WINDOW_HPP_INCLUDED
