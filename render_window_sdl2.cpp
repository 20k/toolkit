#include "render_window_sdl2.hpp"
#include "render_window.hpp"
#include "texture.hpp"
#include "vertex.hpp"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/freetype/imgui_freetype.h>
#include <imgui/examples/imgui_impl_sdl.h>
#include <imgui/examples/imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <map>
#include <iostream>
#include <toolkit/fs_helpers.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_video.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif // __EMSCRIPTEN__

#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif // __EMSCRIPTEN__

namespace
{
void make_fbo(unsigned int* fboptr, unsigned int* tex, vec2i dim, bool is_srgb)
{
    int wx = dim.x();
    int wy = dim.y();

    glGenFramebuffers(1, fboptr);
    glBindFramebuffer(GL_FRAMEBUFFER, *fboptr);

    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);

    #ifndef __EMSCRIPTEN__
    if(!is_srgb)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wx, wy, 0, GL_RGBA, GL_FLOAT, NULL);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8, wx, wy, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    #else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wx, wy, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    #endif

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
}
}

sdl2_render_context::sdl2_render_context(const render_settings& sett, const std::string& window_title)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    window = SDL_CreateWindow(window_title.c_str(), 0, 0, sett.width, sett.height, SDL_WINDOW_OPENGL);

    if (window == nullptr)
    {
        printf("Could not create window: %s\n", SDL_GetError());
        throw std::runtime_error("Couldn't create window");
    }

    if(glewInit() != GLEW_OK)
        throw std::runtime_error("Bad Glew");

    glcontext = SDL_GL_CreateContext(window);

    ImGui::CreateContext(&atlas);

    printf("ImGui create context\n");

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    if(sett.viewports)
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGuiStyle& style = ImGui::GetStyle();

    style.FrameRounding = 0;
    style.WindowRounding = 0;
    style.ChildRounding = 0;
    style.ChildBorderSize = 0;
    style.FrameBorderSize = 0;
    style.WindowBorderSize = 1;

    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if(sett.is_srgb)
        ImGui::SetStyleLinearColor(true);

    io.Fonts->Clear();
    io.Fonts->AddFontDefault();

    ImGuiFreeType::BuildFontAtlas(&atlas, 0, 1);

    ImGui_ImplSDL2_InitForOpenGL(window, glcontext);
    //ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    printf("ImGui finished creation\n");
}
