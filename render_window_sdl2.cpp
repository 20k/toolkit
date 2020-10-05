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
#include <SDL.h>
#include "clock.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif // __EMSCRIPTEN__

#ifndef __EMSCRIPTEN__
#include <filesystem>
#include <windows.h>
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

sdl2_render_context::sdl2_render_context(const render_settings& lsett, const std::string& window_title)
{
    render_settings sett = lsett;

    #ifdef __EMSCRIPTEN__
    double width, height;
    emscripten_get_element_css_size("canvas", &width, &height);
    emscripten_set_canvas_size(int(width), int(height));

    sett.width = width;
    sett.height = height;
    sett.viewports = false;
    sett.is_srgb = false;
    #endif // __EMSCRIPTEN__

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);

    if(sett.is_srgb)
    {
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
    }

    #ifdef __APPLE__
    // GL 3.2 Core + GLSL 150
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG); // Always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    #elif defined(__EMSCRIPTEN__)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    printf("Emscripten init\n");
    #else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    #endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    window = SDL_CreateWindow(window_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, sett.width, sett.height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if(window == nullptr)
    {
        printf("Could not create window: %s\n", SDL_GetError());
        throw std::runtime_error("Couldn't create window");
    }

    glcontext = SDL_GL_CreateContext(window);

    SDL_GL_MakeCurrent(window, glcontext);

    if(glewInit() != GLEW_OK)
        throw std::runtime_error("Bad Glew");

    if(sett.no_decoration)
    {
        SDL_SetWindowBordered(window, SDL_FALSE);
    }

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
    ImGui_ImplOpenGL3_Init(glsl_version);

    printf("ImGui finished creation, SDL2\n");
}

sdl2_render_context::~sdl2_render_context()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glcontext);

    SDL_DestroyWindow(window);
    SDL_Quit();
}

sdl2_backend::sdl2_backend(const render_settings& sett, const std::string& window_title) : ctx(sett, window_title)
{
    set_vsync(sett.vsync);

    #ifndef NO_OPENCL
    if(sett.opencl)
        clctx = new opencl_context;
    #endif // NO_OPENCL
}

sdl2_backend::~sdl2_backend()
{
    if(clctx)
    {
        delete clctx;
        clctx = nullptr;
    }
}

void sdl2_backend::init_screen(vec2i dim)
{
    make_fbo(&ctx.fbo, &ctx.screen_tex, dim, false);
    make_fbo(&ctx.fbo_srgb, &ctx.screen_tex_srgb, dim, true);

    #ifndef NO_OPENCL
    if(clctx)
    {
        clctx->cl_screen_tex.create_from_texture(ctx.screen_tex);
        clctx->cl_image.alloc(dim, cl_image_format{CL_RGBA, CL_FLOAT});
    }
    #endif // NO_OPENCL
}

///TODO: High dpi support makes this a bit sketchy
vec2i sdl2_backend::get_window_size()
{
    int w = 0;
    int h = 0;

    SDL_GL_GetDrawableSize(ctx.window, &w, &h);

    return {w, h};
}

vec2i sdl2_backend::get_window_position()
{
    int x = 0;
    int y = 0;

    SDL_GetWindowPosition(ctx.window, &x, &y);

    return {x, y};
}

void sdl2_backend::set_window_position(vec2i pos)
{
    SDL_SetWindowPosition(ctx.window, pos.x(), pos.y());
}

#ifdef __EMSCRIPTEN__
namespace{
vec2i check_resize_emscripten(sdl2_backend& b)
{
    double width, height;
    emscripten_get_element_css_size("canvas", &width, &height);
    emscripten_set_canvas_size((int)width, (int)height);

    return {width, height};
}
}
#endif // __EMSCRIPTEN__

bool sdl2_backend::is_vsync()
{
    return SDL_GL_GetSwapInterval() != 0;
}

void sdl2_backend::set_vsync(bool enabled)
{
    int err = 0;

    if(enabled)
        err = SDL_GL_SetSwapInterval(1);
    else
        err = SDL_GL_SetSwapInterval(0);

    if(err != 0)
    {
        printf("Err in set_vsync");
    }
}

void sdl2_backend::poll_events_only(double maximum_sleep_s)
{
    auto next_size = get_window_size();

    #ifdef __EMSCRIPTEN__
    next_size = check_resize_emscripten(*this);
    #endif // __EMSCRIPTEN__

    while(1)
    {
        SDL_Event e;

        #ifndef __EMSCRIPTEN__
        int res = SDL_PollEvent(&e);

        if(res == 0)
        {
            if(maximum_sleep_s != 0)
                MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD) (maximum_sleep_s * 1e3), QS_ALLEVENTS);

            break;
        }
        #else
        int res = SDL_PollEvent(&e);

        if(res == 0)
            break;
        #endif

        ImGui_ImplSDL2_ProcessEvent(&e);

        if(e.type == SDL_QUIT)
        {
            closing = true;
        }

        if(e.type == SDL_DROPFILE && e.drop.type == SDL_DROPFILE)
        {
            std::string name = e.drop.file;
            SDL_free(e.drop.file);

            std::string data = file::read(name, file::mode::TEXT);

            dropped_file fle;
            fle.name = name;

            std::cout << "Dropped file " << name << std::endl;

            //fle.name = std::filesystem::path(name).filename().string();
            fle.data = data;

            dropped.push_back(fle);
        }

        #ifndef __EMSCRIPTEN__
        if(e.type == SDL_WINDOWEVENT && e.window.windowID == SDL_GetWindowID(ctx.window))
        {
            if(e.window.event == SDL_WINDOWEVENT_CLOSE)
                closing = true;

            if(e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                int width = e.window.data1;
                int height = e.window.data2;

                next_size = {width, height};
            }
        }
        #endif // __EMSCRIPTEN__
    }

    if(next_size != last_size)
    {
        resize(next_size);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(ctx.window);
}

void sdl2_backend::poll_issue_new_frame_only()
{
    ImGui::NewFrame();
}

void sdl2_backend::poll(double maximum_sleep_s)
{
    poll_events_only(maximum_sleep_s);
    poll_issue_new_frame_only();
}

void sdl2_backend::display()
{
    assert(ctx.window);

    ImGui::Render();

    vec2i dim = get_window_size();

    SDL_GL_MakeCurrent(ctx.window, ctx.glcontext);

    glViewport(0, 0, dim.x(), dim.y());

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.fbo);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        SDL_Window* backup_current_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
    }

    if(ImGui::GetCurrentContext()->IsLinearColor)
    {
        glEnable(GL_FRAMEBUFFER_SRGB);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.fbo_srgb);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.fbo);

        glBlitFramebuffer(0, 0, dim.x(), dim.y(), 0, 0, dim.x(), dim.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.fbo_srgb);

        glBlitFramebuffer(0, 0, dim.x(), dim.y(), 0, 0, dim.x(), dim.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glDisable(GL_FRAMEBUFFER_SRGB);
    }
    else
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.fbo);

        glBlitFramebuffer(0, 0, dim.x(), dim.y(), 0, 0, dim.x(), dim.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    SDL_GL_SwapWindow(ctx.window);
}

void sdl2_backend::display_last_frame()
{

}

bool sdl2_backend::should_close()
{
    return closing;
}

void sdl2_backend::close()
{
    closing = true;
}

void sdl2_backend::resize(vec2i dim)
{
    if(dim == last_size)
        return;

    last_size = dim;

    SDL_SetWindowSize(ctx.window, dim.x(), dim.y());
    init_screen(dim);
}

bool sdl2_backend::has_dropped_file()
{
    return dropped.size() > 0;
}

dropped_file sdl2_backend::get_next_dropped_file()
{
    if(dropped.size() == 0)
        return dropped_file();

    return dropped[0];
}

void sdl2_backend::pop_dropped_file()
{
    if(dropped.size() == 0)
        return;

    dropped.erase(dropped.begin());
}

opencl_context* sdl2_backend::get_opencl_context()
{
    return clctx;
}
