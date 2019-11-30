#ifndef SFML_COMPATIBILITY_HPP_INCLUDED
#define SFML_COMPATIBILITY_HPP_INCLUDED

#include "vertex.hpp"
#include <vector>
#include <vec/vec.hpp>
#include <imgui/imgui_internal.h>

template<typename T>
inline
std::vector<vertex> sfml_to_vertices(const T& shape)
{
    int vcount = shape.getPointCount();

    assert(vcount >= 3);

    vec2f centre_pos = {0,0};

    for(int i=0; i < vcount; i++)
    {
        auto pos = shape.getTransform() * shape.getPoint(i);

        centre_pos += (vec2f){pos.x, pos.y} / (float)vcount;
    }

    auto scol = shape.getFillColor();
    auto lin_col = srgb_to_lin_approx((vec3f){scol.r, scol.g, scol.b}/255.f);

    std::vector<vertex> vertices;
    vertices.reserve(vcount * 3);

    for(int i=0; i < vcount; i++)
    {
        int next = (i + 1) % vcount;

        auto real_pos_1 = shape.getTransform() * shape.getPoint(i);
        auto real_pos_2 = shape.getTransform() * shape.getPoint(next);

        vertex vert;
        vert.position = {real_pos_1.x, real_pos_1.y};
        vert.colour = {lin_col.x(), lin_col.y(), lin_col.z(), scol.a / 255.f};
        vert.uv = {ImGui::GetDrawListSharedData()->TexUvWhitePixel.x, ImGui::GetDrawListSharedData()->TexUvWhitePixel.y};

        vertex vert2 = vert;
        vert2.position = {real_pos_2.x, real_pos_2.y};

        vertex centre = vert;
        centre.position = centre_pos;

        vertices.push_back(vert);
        vertices.push_back(vert2);
        vertices.push_back(centre);
    }

    return vertices;
}

#endif // SFML_COMPATIBILITY_HPP_INCLUDED
