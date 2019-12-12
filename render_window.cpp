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

namespace
{
    thread_local std::map<std::string, bool> frost_map;
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

    if(!is_srgb)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wx, wy, 0, GL_RGBA, GL_FLOAT, NULL);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8, wx, wy, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
}

void init_screen_data(render_window& win, vec2i dim)
{
    make_fbo(&win.rctx.fbo, &win.rctx.screen_tex, dim, false);
    make_fbo(&win.rctx.fbo_srgb, &win.rctx.screen_tex_srgb, dim, true);

    if(win.clctx)
    {
        win.clctx->cl_screen_tex.create_from_texture(win.rctx.screen_tex);
        win.clctx->cl_image.alloc(dim, cl_image_format{CL_RGBA, CL_FLOAT});
    }
}

render_context::render_context(const render_settings& sett, const std::string& window_title)
{
    glfwSetErrorCallback(glfw_error_callback);

    if(!glfwInit())
        throw std::runtime_error("Could not init glfw");

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);

    if(sett.no_double_buffer)
        glfwWindowHint( GLFW_DOUBLEBUFFER, GL_FALSE );

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
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

opencl_context::opencl_context() : ctx(), cl_screen_tex(ctx), cqueue(ctx), cl_image(ctx)
{

}

render_window::render_window(const render_settings& sett, const std::string& window_title) : rctx(sett, window_title)
{
    settings = sett;

    if(sett.opencl)
    {
        clctx = new opencl_context;
    }

    init_screen_data(*this, {sett.width, sett.height});
}

render_window::~render_window()
{
    if(clctx)
    {
        delete clctx;
        clctx = nullptr;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(rctx.window);
    glfwTerminate();
}

render_settings render_window::get_render_settings()
{
    render_settings sett = settings;

    auto dim = get_window_size();

    sett.width = dim.x();
    sett.height = dim.y();

    return sett;
}

vec2i render_window::get_window_size()
{
    assert(rctx.window);

    int display_w = 0;
    int display_h = 0;

    glfwGetFramebufferSize(rctx.window, &display_w, &display_h);

    return {display_w, display_h};
}

vec2i render_window::get_window_position()
{
    assert(rctx.window);

    int wxpos = 0;
    int wypos = 0;

    glfwGetWindowPos(rctx.window, &wxpos, &wypos);

    return {wxpos, wypos};
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

///just realised a much faster version of this
///unconditionally blur whole screen, then only clip bits we want
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

void post_render(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    render_window* win = (render_window*)cmd->UserCallbackData;

    assert(win->clctx);

    blur_buffer(*win, win->clctx->cl_screen_tex);
}

void render_window::poll(double maximum_sleep_s)
{
    assert(rctx.window);

    glfwWaitEventsTimeout(maximum_sleep_s);

    if(glfwWindowShouldClose(rctx.window))
        closing = true;

    auto next_size = get_window_size();

    if(next_size != last_size)
    {
        last_size = next_size;

        init_screen_data(*this, next_size);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    //ImDrawList* draw = ImGui::GetBackgroundDrawList();
    //draw->AddCallback(pre_render, this);
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

/*
template<int N, typename T>
inline
constexpr vec<N, T> lin_to_srgb_approx(const vec<N, T>& in)
{
    vec<N, T> S1 = sqrtf(in);
    vec<N, T> S2 = sqrtf(S1);
    vec<N, T> S3 = sqrtf(S2);

    return 0.662002687f * S1 + 0.684122060f * S2 - 0.323583601f * S3 - 0.0225411470f * in;
}
*/

/*"uniform mat4 ProjMtx;\n"
        "in vec2 Position;\n"
        "in vec2 UV;\n"
        "in vec4 Color;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";*/

/*const GLchar* fragment_shader_glsl_130 =
        "uniform sampler2D Texture;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "out vec4 Out_Color_0;\n"
        "out vec4 Out_Color_1;\n"
        "void main()\n"
        "{\n"
        "   vec4 tex_col4 = texture(Texture, Frag_UV.st);\n"
        "   Out_Color_0 = Frag_Color * tex_col4;\n"
        "   Out_Color_1 = 1 - tex_col4;\n"
        "}\n";*/

static const char* vert_shader =
        "uniform mat4 ProjMtx;\n"
        "in vec2 Position;\n"
        "in vec2 UV;\n"
        "in vec4 Color;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";

static const char* fragment_shader_explicit =
        "uniform sampler2D Texture;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "out vec4 Out_Color_0;\n"
        "void main()\n"
        "{\n"
        "   vec4 tex_col4 = texture(Texture, Frag_UV.st);\n"
        "   vec3 s1 = sqrt(tex_col4.xyz);\n"
        "   vec3 s2 = sqrt(s1);\n"
        "   vec3 s3 = sqrt(s2);\n"
        "   Out_Color_0 = (vec4)(0.662002687f * S1 + 0.684122060f * S2 - 0.323583601f * S3 - 0.0225411470f * tex_col4.xyz, tex_col4.w);\n"
        "}\n";

static const char* fragment_shader_implicit =
        "uniform sampler2D Texture;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "out vec4 Out_Color_0;\n"
        "void main()\n"
        "{\n"
        "   vec4 tex_col4 = texture(Texture, Frag_UV.st);\n"
        "   Out_Color_0 = tex_col4;\n"
        "}\n";

void render_window::display()
{
    assert(rctx.window);

    if(clctx)
    {
        ImGui::GetBackgroundDrawList()->AddCallback(post_render, this);
    }

    ImGui::Render();

    vec2i dim = get_window_size();

    glfwMakeContextCurrent(rctx.window);

    glViewport(0, 0, dim.x(), dim.y());

    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, rctx.fbo);
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

        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, rctx.fbo_srgb);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER, rctx.fbo);

        glBlitFramebuffer(0, 0, dim.x(), dim.y(), 0, 0, dim.x(), dim.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER, rctx.fbo_srgb);

        glBlitFramebuffer(0, 0, dim.x(), dim.y(), 0, 0, dim.x(), dim.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glDisable(GL_FRAMEBUFFER_SRGB);
    }
    else
    {
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, 0);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER, rctx.fbo);

        glBlitFramebuffer(0, 0, dim.x(), dim.y(), 0, 0, dim.x(), dim.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    glFinish();
    glfwSwapBuffers(rctx.window);
}

bool render_window::should_close()
{
    return closing;
}

void render_window::close()
{
    closing = true;
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
