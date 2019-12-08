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

void make_fbo(unsigned int* fboptr, unsigned int* tex, vec2i dim)
{
    int wx = dim.x();
    int wy = dim.y();

    glGenFramebuffers(1, fboptr);
    glBindFramebuffer(GL_FRAMEBUFFER, *fboptr);

    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wx, wy, 0, GL_RGBA, GL_FLOAT, NULL);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
}

void init_screen_data(render_window& win, vec2i dim)
{
    make_fbo(&win.rctx.fbo, &win.rctx.screen_tex, dim);
    make_fbo(&win.rctx.background_fbo, &win.rctx.background_screen_tex, dim);

    win.cl_screen_tex.create_from_texture(win.rctx.screen_tex);
    win.cl_background_screen_tex.create_from_texture(win.rctx.background_screen_tex);
    win.cl_image.alloc(dim, cl_image_format{CL_RGBA, CL_FLOAT});
}

render_context::render_context(vec2i dim, const std::string& window_title, window_flags::window_flags flags)
{
    glfwSetErrorCallback(glfw_error_callback);

    if(!glfwInit())
        throw std::runtime_error("Could not init glfw");

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);

    if(flags & window_flags::DOUBLE_BUFFER)
        glfwWindowHint( GLFW_DOUBLEBUFFER, GL_FALSE );

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
    //glfwWindowHint(GLFW_SAMPLES, 8);

    if(flags & window_flags::SRGB)
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

    window = glfwCreateWindow(dim.x(), dim.y(), window_title.c_str(), nullptr, nullptr);

    if(window == nullptr)
        throw std::runtime_error("Nullptr window in glfw");

    glfwMakeContextCurrent(window);

    if(glewInit() != GLEW_OK)
        throw std::runtime_error("Bad Glew");


    ImGui::CreateContext(&atlas);

    printf("ImGui create context\n");

    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    if(flags & window_flags::VIEWPORTS)
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

    if(flags & window_flags::SRGB)
        ImGui::SetStyleLinearColor(true);

    io.Fonts->Clear();
    io.Fonts->AddFontDefault();
    ImGuiFreeType::BuildFontAtlas(&atlas, 0, 1);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

}

render_window::render_window(vec2i dim, const std::string& window_title, window_flags::window_flags flags) : rctx(dim, window_title, flags), ctx(), cl_screen_tex(ctx), cl_background_screen_tex(ctx), cqueue(ctx), cl_image(ctx)
{
    init_screen_data(*this, dim);
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

void pre_render(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    render_window* win = (render_window*)cmd->UserCallbackData;

    //glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, win->rctx.background_fbo);
}

void blur_buffer(render_window& win, cl::gl_rendertexture& tex)
{
    glFinish();

    std::vector<frostable> frosty = win.get_frostables();

    tex.acquire(win.cqueue);

    for(int i=0; i < 20; i++)
    for(frostable& f : frosty)
    {
        int ix = f.pos.x();
        int iy = win.get_window_size().y() - f.pos.y() - f.dim.y();

        int red = 0;

        cl::args blur;
        blur.push_back(tex);
        blur.push_back(tex);
        blur.push_back(f.dim.x());
        blur.push_back(f.dim.y());
        blur.push_back(ix);
        blur.push_back(iy);
        blur.push_back(red);

        win.cqueue.exec("blur_image", blur, {f.dim.x()/2, f.dim.y()}, {16, 16});

        red = (red + 1) % 2;

        cl::args blur2;
        blur2.push_back(tex);
        blur2.push_back(tex);
        blur2.push_back(f.dim.x());
        blur2.push_back(f.dim.y());
        blur2.push_back(ix);
        blur2.push_back(iy);
        blur2.push_back(red);

        win.cqueue.exec("blur_image", blur2, {f.dim.x()/2, f.dim.y()}, {16, 16});
    }

    tex.unacquire(win.cqueue);
    win.cqueue.block();
}

void blend_buffers(render_window& win, cl::gl_rendertexture& out, cl::gl_rendertexture& in)
{
    glFinish();

    out.acquire(win.cqueue);
    in.acquire(win.cqueue);

    cl::args blendo;
    blendo.push_back(out);
    blendo.push_back(in);
    blendo.push_back(out);

    auto dim = win.get_window_size();

    win.cqueue.exec("blend", blendo, {dim.x(), dim.y()}, {16, 16});

    in.unacquire(win.cqueue);
    out.unacquire(win.cqueue);

    win.cqueue.block();
}

void post_render(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    render_window* win = (render_window*)cmd->UserCallbackData;

    //glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, win->rctx.fbo);

    #if 0
    glFinish();

    #if 1
    std::vector<frostable> frosty = win->get_frostables();

    std::cout << "Frosty Size " << frosty.size() << std::endl;

    win->cl_screen_tex.acquire(win->cqueue);

    for(frostable& f : frosty)
    {
        /*cl::args blur;
        blur.push_back(screen_img_full);
        blur.push_back(screen_img_full2);
        blur.push_back(f.dim.x());
        blur.push_back(f.dim.y());
        blur.push_back(f.pos.x());
        blur.push_back(f.pos.y());

        cqueue.exec("gauss_x_image", blur, {screen_reduction_dim.x(), screen_reduction_dim.y()}, {16, 16});

        cl::args blur2;
        blur2.push_back(screen_img_full2);
        blur2.push_back(screen_img_full);
        blur2.push_back(f.dim.x());
        blur2.push_back(f.dim.y());
        blur2.push_back(f.pos.x());
        blur2.push_back(f.pos.y());

        cqueue.exec("gauss_y_image", blur2, {screen_reduction_dim.x(), screen_reduction_dim.y()}, {16, 16});*/

        int red = 0;

        cl::args blur;
        blur.push_back(win->cl_screen_tex);
        blur.push_back(win->cl_screen_tex);
        blur.push_back(f.dim.x());
        blur.push_back(f.dim.y());
        blur.push_back(f.pos.x());
        blur.push_back(f.pos.y());
        blur.push_back(red);

        win->cqueue.exec("blur_image", blur, {f.dim.x()/2, f.dim.y()}, {16, 16});

        red = 1;

        cl::args blur2;
        blur2.push_back(win->cl_screen_tex);
        blur2.push_back(win->cl_screen_tex);
        blur2.push_back(f.dim.x());
        blur2.push_back(f.dim.y());
        blur2.push_back(f.pos.x());
        blur2.push_back(f.pos.y());
        blur2.push_back(red);

        win->cqueue.exec("blur_image", blur2, {f.dim.x()/2, f.dim.y()}, {16, 16});
    }

    win->cl_screen_tex.unacquire(win->cqueue);
    win->cqueue.block();
    #endif // 0
    #endif // 0

    blur_buffer(*win, win->cl_screen_tex);
}

void render_window::poll()
{
    assert(rctx.window);

    glfwPollEvents();

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

    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    draw->AddCallback(pre_render, this);
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

#if 0
void handle_frosting(render_window& win)
{
    std::vector<frostable> frosty = win.get_frostables();

    win.cl_screen_tex.acquire(win.cqueue);

    auto dim = win.get_window_size();

    std::cout << "Frostables " << frosty.size() << std::endl;

    for(frostable& f : frosty)
    {
        cl::args blur;
        blur.push_back(win.cl_screen_tex);
        blur.push_back(win.cl_image);
        blur.push_back(dim.x());
        blur.push_back(dim.y());

        win.cqueue.exec("gauss_x_image", blur, {dim.x(), dim.y()}, {16, 16});

        cl::args blur2;
        blur2.push_back(win.cl_image);
        blur2.push_back(win.cl_screen_tex);
        blur2.push_back(dim.x());
        blur2.push_back(dim.y());

        win.cqueue.exec("gauss_y_image", blur2, {dim.x(), dim.y()}, {16, 16});
        //blur.push_back()
    }

    win.cl_screen_tex.unacquire(win.cqueue);
    win.cqueue.block();
}
#endif // 0

void render_window::display()
{
    assert(rctx.window);

    ImGui::GetBackgroundDrawList()->AddCallback(post_render, this);

    ImGui::Render();

    vec2i dim = get_window_size();

    glfwMakeContextCurrent(rctx.window);

    glViewport(0, 0, dim.x(), dim.y());

    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, rctx.background_fbo);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);


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

    //handle_frosting(*this);

    /*glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, 0);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER, rctx.background_fbo);

    glBlitFramebuffer(0, 0, dim.x(), dim.y(), 0, 0, dim.x(), dim.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);*/

    //blur_buffer(*this, cl_background_screen_tex);
    //blend_buffers(*this, cl_screen_tex, cl_background_screen_tex);

    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, 0);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER, rctx.fbo);

    glBlitFramebuffer(0, 0, dim.x(), dim.y(), 0, 0, dim.x(), dim.y(), GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glFinish();
    glfwSwapBuffers(rctx.window);
}

bool render_window::should_close()
{
    return closing;
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
