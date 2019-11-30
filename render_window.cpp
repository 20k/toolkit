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

void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

render_window::render_window(vec2i dim, const std::string& window_title, window_flags::window_flags flags)
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

vec2i render_window::get_window_size()
{
    assert(window);

    int display_w = 0;
    int display_h = 0;

    glfwGetFramebufferSize(window, &display_w, &display_h);

    return {display_w, display_h};
}

vec2i render_window::get_window_position()
{
    assert(window);

    int wxpos = 0;
    int wypos = 0;

    glfwGetWindowPos(window, &wxpos, &wypos);

    return {wxpos, wypos};
}

void render_window::poll()
{
    assert(window);

    glfwPollEvents();

    if(glfwWindowShouldClose(window))
        closing = true;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void render_window::display()
{
    assert(window);

    ImGui::Render();

    vec2i dim = get_window_size();

    glfwMakeContextCurrent(window);

    glViewport(0, 0, dim.x(), dim.y());

    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);
    //glDrawBuffer(GL_BACK);
    //glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, 0);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }

    glfwSwapBuffers(window);
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

    idl->PrimReserve(vertices.size(), vertices.size());
    ImDrawVert* vtx_write = idl->_VtxWritePtr;
    ImDrawIdx* idx_write = idl->_IdxWritePtr;
    unsigned int vtx_current_idx = idl->_VtxCurrentIdx;

    for(int i=0; i < (int)vertices.size(); i++)
    {
        vtx_write[i].pos.x = vertices[i].position.x();
        vtx_write[i].pos.y = vertices[i].position.y();

        vtx_write[i].uv.x = vertices[i].uv.x();
        vtx_write[i].uv.y = vertices[i].uv.y();

        vec3f srgb_col = lin_to_srgb(vertices[i].colour.xyz()) * 255.f;

        vtx_write[i].col = IM_COL32((int)srgb_col.x(), (int)srgb_col.y(), (int)srgb_col.z(), (int)(vertices[i].colour.w() * 255));

        idx_write[i] = vtx_current_idx + i;
    }

    idl->_VtxWritePtr += vertices.size();
    idl->_IdxWritePtr += vertices.size();
    idl->_VtxCurrentIdx += vertices.size();

    idl->PopTextureID();
}
