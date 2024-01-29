#include "render_window_glfw.hpp"
#include "render_window.hpp"
#include "texture.hpp"
#include "vertex.hpp"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/freetype/imgui_freetype.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <map>
#include <iostream>
#include <toolkit/fs_helpers.hpp>


#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif // __EMSCRIPTEN__

#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif // __EMSCRIPTEN__

namespace
{
    thread_local std::vector<dropped_file> dropped_files;
}

void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

struct glfw_user_data
{
    int max_frames = 0;
};

#ifndef __EMSCRIPTEN__
void maximise_callback(GLFWwindow* window, int maximized)
{
    if(maximized)
    {
        glfw_user_data* data = (glfw_user_data*)glfwGetWindowUserPointer(window);
        data->max_frames = 30;
    }
    else
    {
        glfw_user_data* data = (glfw_user_data*)glfwGetWindowUserPointer(window);
        data->max_frames = 0;
    }
}
#endif // __EMSCRIPTEN__

#ifndef __EMSCRIPTEN__
void drop_callback(GLFWwindow* window, int count, const char** paths)
{
    for(int i=0; i < count; i++)
    {
        std::string name = std::string(paths[i]);
        std::string data = file::read(name, file::mode::TEXT);

        dropped_file fle;
        fle.name = std::filesystem::path(name).filename().string();
        fle.data = data;

        dropped_files.push_back(fle);
    }
}
#endif // __EMSCRIPTEN__

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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, wx, wy, 0, GL_RGBA, GL_FLOAT, NULL);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8, wx, wy, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    #else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wx, wy, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    #endif

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
}

glfw_render_context::glfw_render_context(const render_settings& lsett, const std::string& window_title)
{
    render_settings sett = lsett;

    atlas.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_LCD | ImGuiFreeTypeBuilderFlags_FILTER_DEFAULT | ImGuiFreeTypeBuilderFlags_LoadColor;

    #ifdef __EMSCRIPTEN__
    double width, height;
    emscripten_get_element_css_size("canvas", &width, &height);
    emscripten_set_canvas_size(int(width), int(height));

    sett.width = width;
    sett.height = height;
    sett.viewports = false;
    sett.is_srgb = false;
    #endif // __EMSCRIPTEN__

    glfwSetErrorCallback(glfw_error_callback);

    if(!glfwInit())
        throw std::runtime_error("Could not init glfw");

    #ifndef __EMSCRIPTEN__
    const char* glsl_version = "#version 130";
    #else
    const char* glsl_version = "#version 100";
    #endif
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_DECORATED, !sett.no_decoration);

    if(sett.no_double_buffer)
        glfwWindowHint(GLFW_DOUBLEBUFFER, GL_FALSE);

    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
    //glfwWindowHint(GLFW_SAMPLES, 8);

    if(sett.is_srgb)
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    window = glfwCreateWindow(sett.width, sett.height, window_title.c_str(), nullptr, nullptr);

    {
        glfw_user_data* data = new glfw_user_data;
        glfwSetWindowUserPointer(window, (void*)data);
    }

    #ifndef __EMSCRIPTEN__
    glfwSetWindowMaximizeCallback(window, maximise_callback);
    #endif // __EMSCRIPTEN__

    if(window == nullptr)
        throw std::runtime_error("Nullptr window in glfw");

    glfwMakeContextCurrent(window);

    if(glewInit() != GLEW_OK)
        throw std::runtime_error("Bad Glew");

    ImGui::CreateContext(&atlas);

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

    //ImGuiFreeType::BuildFontAtlas(&atlas, ImGuiFreeTypeBuilderFlags_LCD | ImGuiFreeTypeBuilderFlags_FILTER_DEFAULT | ImGuiFreeTypeBuilderFlags_LoadColor);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
}

glfw_render_context::~glfw_render_context()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}


glfw_backend::glfw_backend(const render_settings& sett, const std::string& window_title) : ctx(sett, window_title)
{
    #ifndef __EMSCRIPTEN__
    glfwSetDropCallback(ctx.window, drop_callback);
    #endif // __EMSCRIPTEN__

    set_vsync(sett.vsync);

    #ifndef NO_OPENCL
    if(sett.opencl)
        clctx = new opencl_context();
    #endif // NO_OPENCL

    #ifdef __EMSCRIPTEN__
    emscripten_drag_drop::init();
    #endif // __EMSCRIPTEN__
}

void glfw_backend::init_screen(vec2i dim)
{
    if(dim.x() < 32)
        dim.x() = 32;

    if(dim.y() < 32)
        dim.y() = 32;

    if(ctx.screens_init)
    {
        glDeleteTextures(1, &ctx.screen_tex);
        glDeleteTextures(1, &ctx.screen_tex_srgb);

        glDeleteFramebuffers(1, &ctx.fbo);
        glDeleteFramebuffers(1, &ctx.fbo_srgb);

        ctx.screen_tex = 0;
        ctx.screen_tex_srgb = 0;
        ctx.fbo = 0;
        ctx.fbo_srgb = 0;
    }

    make_fbo(&ctx.fbo, &ctx.screen_tex, dim, false);
    make_fbo(&ctx.fbo_srgb, &ctx.screen_tex_srgb, dim, true);
    ctx.screens_init = true;

    #ifndef NO_OPENCL
    #ifndef NO_OPENCL_SCREEN
    if(clctx)
    {
        clctx->cl_screen_tex.create_from_texture(ctx.screen_tex);
        clctx->cl_image.alloc(dim, cl_image_format{CL_RGBA, CL_FLOAT});
    }
    #endif
    #endif // NO_OPENCL
}

void glfw_backend::set_is_hidden(bool is_hidden)
{
    if(is_hidden)
        glfwHideWindow(ctx.window);
    else
        glfwShowWindow(ctx.window);
}

glfw_backend::~glfw_backend()
{
    if(clctx)
    {
        delete clctx;
        clctx = nullptr;
    }
}

vec2i glfw_backend::get_window_size()
{
    assert(ctx.window);

    int display_w = 0;
    int display_h = 0;

    glfwGetFramebufferSize(ctx.window, &display_w, &display_h);

    return {display_w, display_h};
}

vec2i glfw_backend::get_window_position()
{
    assert(ctx.window);

    int wxpos = 0;
    int wypos = 0;

    glfwGetWindowPos(ctx.window, &wxpos, &wypos);

    return {wxpos, wypos};
}

void glfw_backend::set_window_position(vec2i position)
{
    glfwSetWindowPos(ctx.window, position.x(), position.y());
}

#ifdef __EMSCRIPTEN__
namespace{
void check_resize_emscripten(glfw_backend& b)
{
    double width, height;
    emscripten_get_element_css_size("canvas", &width, &height);
    emscripten_set_canvas_size((int)width, (int)height);

    if(width == b.get_window_size().x() && height == b.get_window_size().y())
        return;

    vec2i dim = {width, height};

    b.resize(dim);
}
}
#endif // __EMSCRIPTEN__

bool glfw_backend::is_vsync()
{
    return is_vsync_enabled;
}

void glfw_backend::set_vsync(bool enabled)
{
    if(enabled == is_vsync_enabled)
        return;

    if(enabled)
        glfwSwapInterval(1);
    else
        glfwSwapInterval(0);

    is_vsync_enabled = enabled;
}

void glfw_backend::poll_events_only(double maximum_sleep_s)
{
    assert(ctx.window);

    glfwWaitEventsTimeout(maximum_sleep_s);

    #ifdef __EMSCRIPTEN__
    check_resize_emscripten(*this);
    #endif // __EMSCRIPTEN__

    if(glfwWindowShouldClose(ctx.window))
        closing = true;

    auto next_size = get_window_size();

    if(next_size != last_size)
    {
        resize(next_size);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    #ifdef __EMSCRIPTEN__
    auto vals = emscripten_drag_drop::get_dropped_files();

    for(auto& i : vals)
    {
        dropped.push_back(i);
    }
    #endif // __EMSCRIPTEN__

    #ifdef __EMSCRIPTEN__
    ImGuiIO& io = ImGui::GetIO();

    ///workaround for emscripten scrolling bugs
    io.MouseWheelH /= 100.f;
    io.MouseWheel /= 100.f;

    io.MouseWheelH = -io.MouseWheelH; //?
    io.MouseWheel = -io.MouseWheel;
    #endif // __EMSCRIPTEN__

    #ifndef __EMSCRIPTEN__
    if(dropped_files.size() > 0)
    {
        for(auto& i : dropped_files)
        {
            dropped.push_back(i);
        }

        dropped_files.clear();
    }
    #endif // __EMSCRIPTEN__
}

void glfw_backend::poll_issue_new_frame_only()
{
    ImGui::NewFrame();
}

void glfw_backend::poll(double maximum_sleep_s)
{
    poll_events_only(maximum_sleep_s);
    poll_issue_new_frame_only();
}

void glfw_backend::display_bind_and_clear()
{
    assert(ctx.window);

    if(clctx)
    {
        //ImGui::GetBackgroundDrawList()->AddCallback(post_render, this);
    }

    glfw_user_data* user_data = (glfw_user_data*)glfwGetWindowUserPointer(ctx.window);

    if(user_data->max_frames > 0)
    {
        glfwSetWindowPos(ctx.window, 0, 0);
        user_data->max_frames--;
    }

    vec2i dim = get_window_size();

    glfwMakeContextCurrent(ctx.window);

    glViewport(0, 0, dim.x(), dim.y());

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.fbo);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);
}

void glfw_backend::display_render()
{
    vec2i dim = get_window_size();

    ImGui::Render();
    //glDrawBuffer(GL_BACK);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
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

    glfwSwapBuffers(ctx.window);
}

void glfw_backend::display()
{
    display_bind_and_clear();
    display_render();
}

void glfw_backend::display_last_frame()
{
    assert(ctx.window);

    glfw_user_data* user_data = (glfw_user_data*)glfwGetWindowUserPointer(ctx.window);

    if(user_data->max_frames > 0)
    {
        glfwSetWindowPos(ctx.window, 0, 0);
        user_data->max_frames--;
    }

    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        throw std::runtime_error("Can't do this with viewports");
    }

    vec2i dim = get_window_size();

    glfwMakeContextCurrent(ctx.window);

    if(ImGui::GetCurrentContext()->IsLinearColor)
    {
        glEnable(GL_FRAMEBUFFER_SRGB);

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

    glfwSwapBuffers(ctx.window);
}

bool glfw_backend::should_close()
{
    return closing;
}

void glfw_backend::close()
{
    closing = true;
}

void glfw_backend::resize(vec2i dim)
{
    if(dim == last_size)
        return;

    last_size = dim;

    glfwSetWindowSize(ctx.window, last_size.x(), last_size.y());

    init_screen(dim);
}

std::string glfw_backend::get_key_name(int key_id)
{
    std::map<int, std::string> key_map;

    key_map[GLFW_KEY_ENTER] = "return";
    key_map[GLFW_KEY_BACKSPACE] = "backspace";
    key_map[GLFW_KEY_DELETE] = "delete";
    key_map[GLFW_KEY_INSERT] = "insert";
    key_map[GLFW_KEY_TAB] = "tab";

    key_map[GLFW_KEY_UP] = "up";
    key_map[GLFW_KEY_DOWN] = "down";
    key_map[GLFW_KEY_LEFT] = "left";
    key_map[GLFW_KEY_RIGHT] = "right";
    key_map[GLFW_KEY_HOME] = "home";
    key_map[GLFW_KEY_END] = "end";
    key_map[GLFW_KEY_PAGE_UP] = "pageup";
    key_map[GLFW_KEY_PAGE_DOWN] = "pagedown";
    key_map[GLFW_KEY_LEFT_SHIFT] = "lshift";
    key_map[GLFW_KEY_RIGHT_SHIFT] = "rshift";
    key_map[GLFW_KEY_LEFT_CONTROL] = "lctrl";
    key_map[GLFW_KEY_RIGHT_CONTROL] = "rctrl";
    key_map[GLFW_KEY_LEFT_ALT] = "lalt";
    key_map[GLFW_KEY_RIGHT_ALT] = "ralt";
    key_map[GLFW_KEY_ESCAPE] = "escape";

    key_map[GLFW_KEY_APOSTROPHE] = "'";
    key_map[GLFW_KEY_COMMA] = ",";
    key_map[GLFW_KEY_MINUS] = "-";
    key_map[GLFW_KEY_PERIOD] = ".";
    key_map[GLFW_KEY_SLASH] = "/";
    key_map[GLFW_KEY_SEMICOLON] = ";";
    key_map[GLFW_KEY_EQUAL] = "=";
    key_map[GLFW_KEY_LEFT_BRACKET] = "[";
    key_map[GLFW_KEY_RIGHT_BRACKET] = "]";
    key_map[GLFW_KEY_BACKSLASH] = "\\";
    key_map[GLFW_KEY_0] = "0";
    key_map[GLFW_KEY_1] = "1";
    key_map[GLFW_KEY_2] = "2";
    key_map[GLFW_KEY_3] = "3";
    key_map[GLFW_KEY_4] = "4";
    key_map[GLFW_KEY_5] = "5";
    key_map[GLFW_KEY_6] = "6";
    key_map[GLFW_KEY_7] = "7";
    key_map[GLFW_KEY_8] = "8";
    key_map[GLFW_KEY_9] = "9";

    key_map[GLFW_KEY_A] = "a";
    key_map[GLFW_KEY_B] = "b";
    key_map[GLFW_KEY_C] = "c";
    key_map[GLFW_KEY_D] = "d";
    key_map[GLFW_KEY_E] = "e";
    key_map[GLFW_KEY_F] = "f";
    key_map[GLFW_KEY_G] = "g";
    key_map[GLFW_KEY_H] = "h";
    key_map[GLFW_KEY_I] = "i";
    key_map[GLFW_KEY_J] = "j";
    key_map[GLFW_KEY_K] = "k";
    key_map[GLFW_KEY_L] = "l";
    key_map[GLFW_KEY_M] = "m";
    key_map[GLFW_KEY_N] = "n";
    key_map[GLFW_KEY_O] = "o";
    key_map[GLFW_KEY_P] = "p";
    key_map[GLFW_KEY_Q] = "q";
    key_map[GLFW_KEY_R] = "r";
    key_map[GLFW_KEY_S] = "s";
    key_map[GLFW_KEY_T] = "t";
    key_map[GLFW_KEY_U] = "u";
    key_map[GLFW_KEY_V] = "v";
    key_map[GLFW_KEY_W] = "w";
    key_map[GLFW_KEY_X] = "x";
    key_map[GLFW_KEY_Y] = "y";
    key_map[GLFW_KEY_Z] = "z";

    key_map[GLFW_KEY_KP_0] = "kp0";
    key_map[GLFW_KEY_KP_1] = "kp1";
    key_map[GLFW_KEY_KP_2] = "kp2";
    key_map[GLFW_KEY_KP_3] = "kp3";
    key_map[GLFW_KEY_KP_4] = "kp4";
    key_map[GLFW_KEY_KP_5] = "kp5";
    key_map[GLFW_KEY_KP_6] = "kp6";
    key_map[GLFW_KEY_KP_7] = "kp7";
    key_map[GLFW_KEY_KP_8] = "kp8";
    key_map[GLFW_KEY_KP_9] = "kp9";

    key_map[GLFW_KEY_KP_DECIMAL] = "kp.";
    key_map[GLFW_KEY_KP_DIVIDE] = "kp/";
    key_map[GLFW_KEY_KP_MULTIPLY] = "kp*";
    key_map[GLFW_KEY_KP_SUBTRACT] = "kp-";
    key_map[GLFW_KEY_KP_ADD] = "kp+";
    key_map[GLFW_KEY_KP_EQUAL] = "kp=";
    key_map[GLFW_KEY_KP_ENTER] = "kpenter";

    key_map[GLFW_KEY_SPACE] = "space";

    key_map[GLFW_KEY_F1] = "f1";
    key_map[GLFW_KEY_F2] = "f2";
    key_map[GLFW_KEY_F3] = "f3";
    key_map[GLFW_KEY_F4] = "f4";
    key_map[GLFW_KEY_F5] = "f5";
    key_map[GLFW_KEY_F6] = "f6";
    key_map[GLFW_KEY_F7] = "f7";
    key_map[GLFW_KEY_F8] = "f8";
    key_map[GLFW_KEY_F9] = "f9";
    key_map[GLFW_KEY_F10] = "f10";
    key_map[GLFW_KEY_F11] = "f11";
    key_map[GLFW_KEY_F12] = "f12";

    auto it = key_map.find(key_id);

    if(it == key_map.end())
        return "";
    else
        return it->second;
}

struct monitor_info
{
    vec2i pos = {0,0};
    vec2i dim = {0,0};

    monitor_info(GLFWmonitor* win)
    {
        glfwGetMonitorPos(win, &pos.x(), &pos.y());

        const GLFWvidmode* mode = glfwGetVideoMode(win);

        dim = {mode->width, mode->height};
    }
};

bool glfw_backend::is_maximised()
{
    int count = 0;

    vec2i pos = get_window_position();
    vec2i dim = get_window_size();

    GLFWmonitor** mons = glfwGetMonitors(&count);

    for(int i=0; i < count; i++)
    {
        monitor_info inf(mons[i]);

        if(pos.x() == inf.pos.x() && pos.y() == inf.pos.y() &&
           dim.x() == inf.dim.x() && dim.y() == inf.dim.y())
            return true;
    }

    return false;
}

GLFWmonitor* get_monitor_of(vec2i pos)
{
    int count = 0;

    GLFWmonitor** mons = glfwGetMonitors(&count);

    for(int i=0; i < count; i++)
    {
        monitor_info inf(mons[i]);

        if(pos.x() >= inf.pos.x() && pos.x() <= inf.pos.x() + inf.dim.x() &&
           pos.y() >= inf.pos.y() && pos.y() <= inf.pos.y() + inf.dim.y())
            return mons[i];
    }

    return nullptr;
}

void glfw_backend::set_is_maximised(bool set_max)
{
    bool is_max = is_maximised();

    if(is_max == set_max)
        return;

    ///was windowed
    if(set_max)
    {
        pre_max_pos = get_window_position();
        pre_max_dim = get_window_size();

        was_windowed_ever = true;

        GLFWmonitor* found = get_monitor_of(get_window_position());

        if(found == nullptr)
            found = glfwGetPrimaryMonitor();

        monitor_info inf(found);

        glfwSetWindowPos(ctx.window, inf.pos.x(), inf.pos.y());
        glfwSetWindowSize(ctx.window, inf.dim.x(), inf.dim.y());
    }
    else
    {
        if(was_windowed_ever)
        {
            glfwSetWindowPos(ctx.window, pre_max_pos.x(), pre_max_pos.y());
            glfwSetWindowSize(ctx.window, pre_max_dim.x(), pre_max_dim.y());
        }
        else
        {
            GLFWmonitor* found = get_monitor_of(get_window_position());

            if(found == nullptr)
                found = glfwGetPrimaryMonitor();

            monitor_info inf(found);

            vec2i new_start = inf.pos + inf.dim / 4;
            vec2i new_end = inf.pos + inf.dim - inf.dim / 4;

            vec2i new_dim = new_end - new_start;

            glfwSetWindowPos(ctx.window, new_start.x(), new_start.y());
            glfwSetWindowSize(ctx.window, new_dim.x(), new_dim.y());
        }
    }
}

void glfw_backend::clear_demaximise_cache()
{
    was_windowed_ever = false;
}

bool glfw_backend::is_focused()
{
    return glfwGetWindowAttrib(ctx.window, GLFW_FOCUSED);
}

bool glfw_backend::has_dropped_file()
{
    return dropped.size() > 0;
}

dropped_file glfw_backend::get_next_dropped_file()
{
    if(dropped.size() == 0)
        return dropped_file();

    return dropped[0];
}

void glfw_backend::pop_dropped_file()
{
    if(dropped.size() == 0)
        return;

    dropped.erase(dropped.begin());
}

opencl_context* glfw_backend::get_opencl_context()
{
    return clctx;
}
