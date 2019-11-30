#ifndef TEXTURE_HPP_INCLUDED
#define TEXTURE_HPP_INCLUDED

#include <vec/vec.hpp>

struct texture
{
    unsigned int handle = 0;

    void load_from_memory(const uint8_t* pixels_rgba, vec2i dim);
    vec2i get_size();

    vec2i dim;
};

#endif // TEXTURE_HPP_INCLUDED
