#include "render_window.hpp"
#include "texture.hpp"
#include "vertex.hpp"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/misc/freetype/imgui_freetype.h>
#include <GL/glew.h>
#include <map>
#include <iostream>
#include <toolkit/fs_helpers.hpp>
#include "render_window_glfw.hpp"
#include "clipboard.hpp"
#include <functional>

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
}


#ifdef __EMSCRIPTEN__
std::string fixup_string(std::string in)
{
    if(in.size() == 0)
        return in;

    int clen = strlen(in.c_str());

    in.resize(clen);

    return in;
}

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

std::function<void(void*, const char*)> old_set_clipboard;
std::function<const char*(void*)> old_get_clipboard;

void set_clipboard_free(void* user_data, const char* text)
{
    #ifndef __EMSCRIPTEN__
    old_set_clipboard(user_data, text);
    #else
    clipboard::set(text);
    #endif
}

const char* get_clipboard_free(void* user_data)
{
    #ifndef __EMSCRIPTEN__
    return old_get_clipboard(user_data);
    #else
    static thread_local std::string clip_buffer;

    clip_buffer = clipboard::get();

    return clip_buffer.c_str();
    #endif
}

void init_clipboard()
{
    old_set_clipboard = ImGui::GetIO().SetClipboardTextFn;
    old_get_clipboard = ImGui::GetIO().GetClipboardTextFn;

    ImGui::GetIO().GetClipboardTextFn = &get_clipboard_free;
    ImGui::GetIO().SetClipboardTextFn = &set_clipboard_free;
}

void emscripten_drag_drop::init()
{
    #ifdef __EMSCRIPTEN__
    drag_drop_init();
    #endif // __EMSCRIPTEN__
}

std::vector<dropped_file> emscripten_drag_drop::get_dropped_files()
{
    std::vector<dropped_file> files;

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

        files.push_back(next);
    }

    clear_dropped();

    #endif // __EMSCRIPTEN__

    return files;
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
    ImTui_ImplNcurses_NewFrame();
    ImTui_ImplText_NewFrame();

    ImGui::GetIO().DeltaTime = clk.restart();

    ImGui::NewFrame();
}

void imtui_backend::display()
{
    ImGui::Render();

    ImTui_ImplText_RenderDrawData(ImGui::GetDrawData(), screen);
    ImTui_ImplNcurses_DrawScreen(true);
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

#ifndef NO_OPENCL
opencl_context::opencl_context() : ctx(), cl_screen_tex(ctx), cqueue(ctx), cl_image(ctx)
{

}
#endif // NO_OPENCL

render_window::render_window(render_settings sett, generic_backend* _backend)
{
    backend = _backend;

    #ifdef __EMSCRIPTEN__
    double width, height;
    emscripten_get_element_css_size("canvas", &width, &height);
    emscripten_set_canvas_size(int(width), int(height));

    sett.width = width;
    sett.height = height;
    sett.viewports = false;
    sett.is_srgb = false;
    #endif // __EMSCRIPTEN__

    settings = sett;

    if(sett.opencl && backend->get_opencl_context())
    {
        clctx = backend->get_opencl_context();
    }

    backend->init_screen({sett.width, sett.height});

    #ifdef __EMSCRIPTEN__
    ImGui::GetIO().IniFilename = "web/imgui.ini";

    //drag_drop_init();

    init_clipboard();
    #endif // __EMSCRIPTEN__
}

render_window::render_window(render_settings sett, const std::string& window_title, backend_type::type type)
{
    #ifndef NO_DEFAULT_BACKEND
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

    //drag_drop_init();

    init_clipboard();
    #endif // __EMSCRIPTEN__
    #else
    assert(false);
    #endif
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
    sett.vsync = backend->is_vsync();

    return sett;
}

void render_window::set_srgb(bool enabled)
{
    if(enabled == settings.is_srgb)
        return;

    settings.is_srgb = enabled;

    #ifndef USE_IMTUI
    ImGui::SetStyleLinearColor(settings.is_srgb);
    #endif // USE_IMTUI
}

void pre_render(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    render_window* win = (render_window*)cmd->UserCallbackData;

    //glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, win->rctx.background_fbo);
}

#ifndef USE_IMTUI
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
#endif // USE_IMTUI

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

        if((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_IsSRGB) == 0)
        {
            vec3f srgb_col = lin_to_srgb_approx(vertices[i].colour.xyz()) * 255.f;

            srgb_col = clamp(srgb_col, 0.f, 255.f);

            vtx_write[i].col = IM_COL32((int)srgb_col.x(), (int)srgb_col.y(), (int)srgb_col.z(), (int)(vertices[i].colour.w() * 255));
        }
        else
        {
            vec3f srgb_col = vertices[i].colour.xyz() * 255.f;

            vtx_write[i].col = IM_COL32((int)srgb_col.x(), (int)srgb_col.y(), (int)srgb_col.z(), (int)(vertices[i].colour.w() * 255));
        }

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
