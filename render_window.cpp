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

#ifdef USE_IMTUI
#include <imtui/imtui.h>
#include <imtui/imtui-impl-ncurses.h>
#endif // USE_IMTUI

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif // __EMSCRIPTEN__

#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif // __EMSCRIPTEN__

namespace
{
    thread_local std::map<std::string, bool> frost_map;
    thread_local std::vector<dropped_file> dropped_files;
}

void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

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

    if(sett.no_double_buffer)
        glfwWindowHint( GLFW_DOUBLEBUFFER, GL_FALSE );

    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
    //glfwWindowHint(GLFW_SAMPLES, 8);

    if(sett.is_srgb)
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    window = glfwCreateWindow(sett.width, sett.height, window_title.c_str(), nullptr, nullptr);

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
}

glfw_render_context::~glfw_render_context()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}

#ifdef __EMSCRIPTEN__
EM_BOOL on_emscripten_resize(int eventType, const EmscriptenUiEvent *uiEvent, void *userData)
{
    if(eventType == EMSCRIPTEN_EVENT_RESIZE)
    {
        glfw_backend& b = *(glfw_backend*)userData;

        vec2i dim = {uiEvent->windowInnerWidth, uiEvent->windowInnerHeight};

        b.resize(dim);

        return false;
    }

    return false;
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

glfw_backend::glfw_backend(const render_settings& sett, const std::string& window_title) : ctx(sett, window_title)
{
    #ifdef __EMSCRIPTEN__
    emscripten_set_resize_callback(nullptr, (void*)this, false, on_emscripten_resize);
    #endif // __EMSCRIPTEN__

    #ifndef __EMSCRIPTEN__
    glfwSetDropCallback(ctx.window, drop_callback);
    #endif // __EMSCRIPTEN__
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

void glfw_backend::poll_events_only(double maximum_sleep_s)
{
    assert(ctx.window);

    glfwWaitEventsTimeout(maximum_sleep_s);

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

///just realised a much faster version of this
///unconditionally blur whole screen, then only clip bits we want
#ifndef NO_OPENCL
void blur_buffer(render_window& win, cl::gl_rendertexture& tex)
{
    std::vector<frostable> frosty = win.get_frostables();

    if(frosty.size() == 0)
        return;

    glFinish();

    tex.acquire(win.clctx->cqueue);

    win.clctx->cl_image.clear(win.clctx->cqueue);

    for(int i=0; i < 80; i++)
    for(frostable& f : frosty)
    {
        int ix = f.pos.x();
        int iy = win.get_window_size().y() - f.pos.y() - f.dim.y();

        int dx = f.dim.x();
        int dy = f.dim.y();

        cl::args blur;

        blur.push_back(tex);
        blur.push_back(win.clctx->cl_image);
        blur.push_back(dx);
        blur.push_back(dy);
        blur.push_back(ix);
        blur.push_back(iy);

        win.clctx->cqueue.exec("blur_image", blur, {dx, dy}, {16, 16});

        cl::args blur2;

        blur2.push_back(win.clctx->cl_image);
        blur2.push_back(tex);
        blur2.push_back(dx);
        blur2.push_back(dy);
        blur2.push_back(ix);
        blur2.push_back(iy);

        win.clctx->cqueue.exec("blur_image", blur2, {dx, dy}, {16, 16});
    }

    tex.unacquire(win.clctx->cqueue);
    win.clctx->cqueue.block();
}
#endif // NO_OPENCL

void post_render(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    render_window* win = (render_window*)cmd->UserCallbackData;

    assert(win->clctx);

    #ifndef NO_OPENCL
    blur_buffer(*win, win->clctx->cl_screen_tex);
    #endif // NO_OPENCL
}

void glfw_backend::display()
{
    assert(ctx.window);

    if(clctx)
    {
        ImGui::GetBackgroundDrawList()->AddCallback(post_render, this);
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

#ifdef USE_IMTUI
imtui_backend::imtui_backend(const render_settings& sett, const std::string& window_title)
{
    screen = new ImTui::TScreen;

    ImGui::CreateContext();

    printf("ImGui create context\n");

    ImGuiIO& io = ImGui::GetIO();

    /*
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
        ImGui::SetStyleLinearColor(true);*/

    io.Fonts->Clear();
    io.Fonts->AddFontDefault();
    /*ImGuiFreeType::BuildFontAtlas(&atlas, 0, 1)*/

    ImTui_ImplNcurses_Init(true);
    ImTui_ImplText_Init();
}

imtui_backend::~imtui_backend()
{
    ImTui_ImplText_Shutdown();
    ImTui_ImplNcurses_Shutdown();
    ImGui::DestroyContext();
}

void imtui_backend::poll(double maximum_sleep_s)
{
    ImTui_ImplNcurses_NewFrame(*screen);
    ImTui_ImplText_NewFrame();

    ImGui::GetIO().DeltaTime = clk.restart();

    ImGui::NewFrame();
}

void imtui_backend::display()
{
    ImGui::Render();

    ImTui_ImplText_RenderDrawData(ImGui::GetDrawData(), *screen);
    ImTui_ImplNcurses_DrawScreen(*screen);
}

bool imtui_backend::should_close()
{
    return closing;
}

void imtui_backend::close()
{
    closing = true;
}

#endif // USE_IMTUI

opencl_context* glfw_backend::get_opencl_context()
{
    return clctx;
}

#ifndef NO_OPENCL
opencl_context::opencl_context() : ctx(), cl_screen_tex(ctx), cqueue(ctx), cl_image(ctx)
{

}
#endif // NO_OPENCL

render_window::render_window(render_settings sett, const std::string& window_title, backend_type::type type)
{
    #ifdef __EMSCRIPTEN__
    double width, height;
    emscripten_get_element_css_size("canvas", &width, &height);
    emscripten_set_canvas_size(int(width), int(height));

    sett.width = width;
    sett.height = height;
    sett.viewports = false;
    sett.is_srgb = false;
    #endif // __EMSCRIPTEN__

    if(type == backend_type::GLFW)
        backend = new glfw_backend(sett, window_title);

    #ifdef USE_IMTUI
    if(type == backend_type::IMTUI)
        backend = new imtui_backend(sett, window_title);
    #endif // USE_IMTUI

    settings = sett;

    if(sett.opencl && backend->get_opencl_context())
    {
        clctx = backend->get_opencl_context();
    }

    backend->init_screen({sett.width, sett.height});

    #ifdef __EMSCRIPTEN__
    ImGui::GetIO().IniFilename = "web/imgui.ini";

    drag_drop_init();
    #endif // __EMSCRIPTEN__
}

render_window::~render_window()
{
    if(backend)
    {
        delete backend;
        backend = nullptr;
    }
}

render_settings render_window::get_render_settings()
{
    render_settings sett = settings;

    auto dim = get_window_size();

    sett.width = dim.x();
    sett.height = dim.y();

    return sett;
}

void render_window::set_srgb(bool enabled)
{
    if(enabled == settings.is_srgb)
        return;

    settings.is_srgb = enabled;

    ImGui::SetStyleLinearColor(settings.is_srgb);
}

void pre_render(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    render_window* win = (render_window*)cmd->UserCallbackData;

    //glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, win->rctx.background_fbo);
}

std::vector<frostable> render_window::get_frostables()
{
    ImGuiContext& g = *GImGui;

    std::vector<frostable> frosts;

    for(int i = 0; i != g.Windows.Size; i++)
    {
        ImGuiWindow* window = g.Windows[i];
        if(window->Active && (window->Flags & (ImGuiWindowFlags_ChildWindow)) == 0)
        {
            std::string name = std::string(window->Name);

            auto it = frost_map.find(name);

            if(it == frost_map.end() || it->second == false)
                continue;

            auto pos = window->Pos;
            ImVec2 dim = window->Size;

            if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                pos.x -= ImGui::GetMainViewport()->Pos.x;
                pos.y -= ImGui::GetMainViewport()->Pos.y;
            }

            frostable f;
            f.pos = {pos.x, pos.y};
            f.dim = {dim.x, dim.y};

            frosts.push_back(f);
        }
    }

    return frosts;
}

void render_window::render(const std::vector<vertex>& vertices, texture* tex)
{
    ImDrawList* idl = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

    if(tex)
    {
        assert(tex->get_size().x() > 0);
        assert(tex->get_size().y() > 0);

        idl->PushTextureID((void*)tex->handle);
    }
    else
    {
        idl->PushTextureID(ImGui::GetIO().Fonts->TexID);
    }

    vec2i window_pos = get_window_position();

    idl->PrimReserve(vertices.size(), vertices.size());
    ImDrawVert* vtx_write = idl->_VtxWritePtr;
    ImDrawIdx* idx_write = idl->_IdxWritePtr;
    unsigned int vtx_current_idx = idl->_VtxCurrentIdx;

    for(int i=0; i < (int)vertices.size(); i++)
    {
        vtx_write[i].pos.x = vertices[i].position.x() + window_pos.x();
        vtx_write[i].pos.y = vertices[i].position.y() + window_pos.y();
        if(tex == nullptr)
        {
            vtx_write[i].uv = {ImGui::GetDrawListSharedData()->TexUvWhitePixel.x, ImGui::GetDrawListSharedData()->TexUvWhitePixel.y};
        }
        else
        {
            vtx_write[i].uv.x = vertices[i].uv.x();
            vtx_write[i].uv.y = vertices[i].uv.y();
        }

        vec3f srgb_col = lin_to_srgb_approx(vertices[i].colour.xyz()) * 255.f;

        srgb_col = clamp(srgb_col, 0.f, 255.f);

        vtx_write[i].col = IM_COL32((int)srgb_col.x(), (int)srgb_col.y(), (int)srgb_col.z(), (int)(vertices[i].colour.w() * 255));

        idx_write[i] = vtx_current_idx + i;
    }

    idl->_VtxWritePtr += vertices.size();
    idl->_IdxWritePtr += vertices.size();
    idl->_VtxCurrentIdx += vertices.size();

    idl->PopTextureID();
}

void render_window::render_texture(unsigned int handle, vec2f p_min, vec2f p_max)
{
    ImDrawList* lst = ImGui::GetBackgroundDrawList();

    lst->AddImage((void*)handle, {p_min.x(), p_min.y()}, {p_max.x(), p_max.y()});
}

void gui::frost(const std::string& window_name)
{
    frost_map[window_name] = true;
}

void gui::current::frost()
{
    return gui::frost(std::string(ImGui::GetCurrentWindow()->Name));
}
