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

#endif // RENDER_WINDOW_SDL2_HPP_INCLUDED
