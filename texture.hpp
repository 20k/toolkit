#ifndef TEXTURE_HPP_INCLUDED
#define TEXTURE_HPP_INCLUDED

#include <vec/vec.hpp>

struct texture_settings
{
    int width = 0;
    int height = 0;
    bool is_srgb = true;

    bool magnify_linear = true;
    bool shrink_linear = true;
};

struct texture
{
    unsigned int handle = 0;

    [[deprecated]]
    void load_from_memory(const uint8_t* pixels_rgba, vec2i dim);

    void load_from_memory(const texture_settings& settings, const uint8_t* pixels_rgba);
    vec2i get_size();
    ~texture();

    vec2i dim;
};

#endif // TEXTURE_HPP_INCLUDED
