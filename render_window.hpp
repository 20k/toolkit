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

struct glfw_render_context
{
    unsigned int fbo;
    unsigned int screen_tex;

    unsigned int fbo_srgb;
    unsigned int screen_tex_srgb;

    GLFWwindow* window = nullptr;
    ImFontAtlas atlas = {};

    glfw_render_context(const render_settings& sett, const std::string& window_title);
    ~glfw_render_context();

    void init_screen(vec2i dim);
};

struct opencl_context
{
    cl::context ctx;
    cl::gl_rendertexture cl_screen_tex;
    cl::command_queue cqueue;
    cl::image cl_image;

    opencl_context();
};

struct generic_backend
{
    //virtual void set_srgb(bool enabled){}
    virtual void poll(double maximum_sleep_s = 0){}
    virtual void display(){}
    virtual bool should_close(){return true;}
    virtual void close(){}
    virtual void init_screen(vec2i dim){}
    virtual opencl_context* get_opencl_context(){return nullptr;}
    virtual vec2i get_window_size(){return {0,0};}
    virtual vec2i get_window_position(){return {0,0};}

    virtual ~generic_backend(){}
};

struct glfw_backend : generic_backend
{
    glfw_render_context ctx;
    opencl_context* clctx = nullptr;

    glfw_backend(const render_settings& sett, const std::string& window_title);

    //void set_srgb(bool enabled) override;
    void poll(double maximum_sleep_s = 0) override;
    void display() override;
    bool should_close() override;
    void close() override;
    void init_screen(vec2i dim) override;
    opencl_context* get_opencl_context() override;
    vec2i get_window_size() override;
    vec2i get_window_position() override;

    ~glfw_backend();

private:
    bool closing = false;
    vec2i last_size;
};

struct render_window
{
    generic_backend* backend = nullptr;
    opencl_context* clctx = nullptr;

    render_window(const render_settings& sett, const std::string& window_title);
    ~render_window();

    render_settings get_render_settings();

    vec2i get_window_size(){return backend->get_window_size();}
    vec2i get_window_position(){return backend->get_window_position();}

    void set_srgb(bool enabled);

    void poll(double maximum_sleep_s = 0){return backend->poll(maximum_sleep_s);}

    std::vector<frostable> get_frostables();

    void display(){return backend->display();}
    bool should_close(){return backend->should_close();}
    void close(){return backend->close();}

    void render(const std::vector<vertex>& vertices, texture* tex = nullptr);
    void render_texture(unsigned int handle, vec2f p_min, vec2f p_max);

private:
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
