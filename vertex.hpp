#ifndef VERTEX_HPP_INCLUDED
#define VERTEX_HPP_INCLUDED

#include <vec/vec.hpp>

struct vertex
{
    vec2f position;
    vec4f colour;
    vec2f uv; ///opengl, normalised
};

#endif // VERTEX_HPP_INCLUDED
