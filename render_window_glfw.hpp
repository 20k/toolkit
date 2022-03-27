#include "render_window.hpp"

#include "config.hpp"
#include <toolkit/clipboard.hpp>

#ifndef NO_OPENCL
#include "opencl.hpp"
#endif // NO_OPENCL

#include <vec/vec.hpp>
#include <imgui/imgui.h>
#include <networking/serialisable_fwd.hpp>
#include "clock.hpp"

struct GLFWwindow;
struct opencl_context;
struct render_settings;

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

struct glfw_backend : generic_backend
{
    vec2i pre_max_pos;
    vec2i pre_max_dim;
    bool was_windowed_ever = false;

    glfw_render_context ctx;
    opencl_context* clctx = nullptr;

    glfw_backend(const render_settings& sett, const std::string& window_title);
    ~glfw_backend();

    //void set_srgb(bool enabled) override;
    bool is_vsync() override;
    void set_vsync(bool enabled) override;
    void poll(double maximum_sleep_s = 0) override;
    void poll_events_only(double maximum_sleep_s = 0) override;
    void poll_issue_new_frame_only() override;

    void display_bind_and_clear() override;
    void display_render() override;
    void display() override;
    void display_last_frame() override;
    bool should_close() override;
    void close() override;
    void init_screen(vec2i dim) override;
    void set_is_hidden(bool is_hidden) override;
    opencl_context* get_opencl_context() override;
    vec2i get_window_size() override;
    vec2i get_window_position() override;
    void set_window_position(vec2i position) override;
    void resize(vec2i dim) override;
    std::string get_key_name(int key_id) override;
    bool is_maximised() override;
    void set_is_maximised(bool set_max) override;
    void clear_demaximise_cache() override;
    bool is_focused() override;

    bool has_dropped_file() override;
    dropped_file get_next_dropped_file() override;
    void pop_dropped_file() override;

    vec2i last_size;
private:
    bool closing = false;
    std::vector<dropped_file> dropped;
    bool is_vsync_enabled = false;
};
