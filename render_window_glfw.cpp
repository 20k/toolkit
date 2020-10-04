#include "render_window_glfw.hpp"
#include "render_window.hpp"
#include "texture.hpp"
#include "vertex.hpp"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/freetype/imgui_freetype.h>
#include <imgui/examples/imgui_impl_glfw.h>
#include <imgui/examples/imgui_impl_opengl3.h>
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

glfw_render_context::glfw_render_context(const render_settings& sett, const std::string& window_title)
{
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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    printf("ImGui finished creation\n");
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
}

void glfw_backend::init_screen(vec2i dim)
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
#endif // __EMSCRIPTEN__

#ifdef __EMSCRIPTEN__
EM_JS(int, num_dropped_files, (),
{
    return Module.dropped.length;
});

EM_JS(int, dropped_array_member_length, (int idx, int member),
{
    return Module.dropped[idx][member].length;
});

EM_JS(void, dropped_array_member, (int idx, int member, char* out),
{
    var member = Module.dropped[idx][member];

    stringToUTF8(member, out, member.length+1);
});

EM_JS(void, clear_dropped, (),
{
    Module.dropped = [];
});

EM_JS(void, drag_drop_init, (),
{
    Module.dropped = [];

    function dragenter(e)
    {
        e.stopPropagation();
        e.preventDefault();
    }

    function dragover(e)
    {
        e.stopPropagation();
        e.preventDefault();
    }

    function drop(e)
    {
        e.stopPropagation();
        e.preventDefault();

        const dt = e.dataTransfer;
        const all_files = dt.files;

        for(var i=0; i < all_files.length; i++)
        {
            const file = all_files[i];

            var read = new FileReader();

            read.readAsBinaryString(file);

            read.onloadend = function()
            {
                Module.dropped.push([file.name, read.result]);
                console.log(Module.dropped[Module.dropped.length-1]);
            }
        }
    }

    let elem = document.getElementById("canvas");
    elem.addEventListener("dragenter", dragenter, false);
    elem.addEventListener("dragover", dragover, false);
    elem.addEventListener("drop", drop, false);

    console.log("registered");
});
#endif // __EMSCRIPTEN__

std::string fixup_string(std::string in)
{
    if(in.size() == 0)
        return in;

    int clen = strlen(in.c_str());

    in.resize(clen);

    return in;
}

bool glfw_backend::is_vsync()
{
    return is_vsync_enabled;
}

void glfw_backend::set_vsync(bool enabled)
{
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
    int num_files = num_dropped_files();

    for(int array_idx = 0; array_idx < num_files; array_idx++)
    {
        int name_length = dropped_array_member_length(array_idx, 0);
        int data_length = dropped_array_member_length(array_idx, 1);

        std::string name;
        name.resize(name_length + 1);

        std::string data;
        data.resize(data_length + 1);

        char* nptr = &name[0];
        char* dptr = &data[0];

        dropped_array_member(array_idx, 0, nptr);
        dropped_array_member(array_idx, 1, dptr);

        dropped_file next;
        next.name = fixup_string(name);
        next.data = fixup_string(data);

        dropped.push_back(next);
    }

    clear_dropped();

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


void glfw_backend::display()
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

    ImGui::Render();

    vec2i dim = get_window_size();

    glfwMakeContextCurrent(ctx.window);

    glViewport(0, 0, dim.x(), dim.y());

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.fbo);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);

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
