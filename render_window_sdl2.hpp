#ifndef RENDER_WINDOW_SDL2_HPP_INCLUDED
#define RENDER_WINDOW_SDL2_HPP_INCLUDED

#include "render_window.hpp"
#include <SDL2/SDL_video.h>

struct sdl2_render_context
{
    unsigned int fbo;
    unsigned int screen_tex;

    unsigned int fbo_srgb;
    unsigned int screen_tex_srgb;

    SDL_Window* window = nullptr;
    SDL_GLContext glcontext;
    ImFontAtlas atlas = {};

    sdl2_render_context(const render_settings& sett, const std::string& window_title);
    ~sdl2_render_context();

    void init_screen(vec2i dim);
};


struct sdl2_backend : generic_backend
{
    sdl2_render_context ctx;
    opencl_context* clctx = nullptr;

    sdl2_backend(const render_settings& sett, const std::string& window_title);
    ~sdl2_backend();

    bool is_vsync() override;
    void set_vsync(bool enabled) override;
    void poll(double maximum_sleep_s = 0) override;
    void poll_events_only(double maximum_sleep_s = 0) override;
    void poll_issue_new_frame_only() override;
    void display() override;
    void display_last_frame() override;
    bool should_close() override;
    void close() override;
    void init_screen(vec2i dim) override;
    opencl_context* get_opencl_context() override;
    vec2i get_window_size() override;
    vec2i get_window_position() override;
    void set_window_position(vec2i position) override;
    void resize(vec2i dim) override;

    bool has_dropped_file() override;
    dropped_file get_next_dropped_file() override;
    void pop_dropped_file() override;

private:
    bool closing = false;
    std::vector<dropped_file> dropped;
};


#endif // RENDER_WINDOW_SDL2_HPP_INCLUDED
