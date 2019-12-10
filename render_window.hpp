#ifndef RENDER_WINDOW_HPP_INCLUDED
#define RENDER_WINDOW_HPP_INCLUDED

#include <vec/vec.hpp>
#include <imgui/imgui.h>
#include "opencl.hpp"
#include <networking/serialisable_fwd.hpp>

struct GLFWwindow;
struct texture;
struct vertex;

/*namespace window_flags
{
    enum window_flags
    {
        NONE = 0,
        SRGB = 1,
        NO_DOUBLE_BUFFER = 2,
        VIEWPORTS = 4,
        OPENCL = 8,
    };
}*/

struct render_settings : serialisable, free_function
{
    int width = 0;
    int height = 0;

    bool is_srgb = false;
    bool no_double_buffer = false;
    bool viewports = false;
    bool opencl = false;
};

struct frostable
{
    vec2f pos;
    vec2i dim;
};

struct render_context
{
    unsigned int fbo;
    unsigned int screen_tex;

    GLFWwindow* window = nullptr;
    ImFontAtlas atlas = {};

    render_context(const render_settings& sett, const std::string& window_title);
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

    render_window(const render_settings& sett, const std::string& window_title);
    ~render_window();

    render_settings get_render_settings();

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
    render_settings settings;
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
