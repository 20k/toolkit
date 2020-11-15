#include "texture.hpp"
#include <GL/glew.h>
#include <GL/gl.h>

void texture::load_from_memory(const uint8_t* pixels_rgba, vec2i _dim)
{
    if(handle != 0)
    {
        glDeleteTextures(1, &handle);
    }

    dim = _dim;

    glGenTextures(1, &handle);
    glBindTexture(GL_TEXTURE_2D, handle);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.f);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, dim.x(), dim.y(), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_rgba);
    glGenerateMipmapEXT(GL_TEXTURE_2D);
}

void texture::load_from_memory(const texture_settings& settings, const uint8_t* pixels_rgba)
{
    if(handle != 0)
    {
        glDeleteTextures(1, &handle);
    }

    dim = {settings.width, settings.height};

    glGenTextures(1, &handle);
    glBindTexture(GL_TEXTURE_2D, handle);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    if(settings.shrink_linear)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    if(settings.magnify_linear)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.f);

    GLint internalformat = 0;

    if(settings.is_srgb)
    {
        internalformat = GL_SRGB8_ALPHA8;
    }
    else
    {
        internalformat = GL_RGBA8;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internalformat, dim.x(), dim.y(), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_rgba);
    glGenerateMipmapEXT(GL_TEXTURE_2D);
}

vec2i texture::get_size()
{
    return dim;
}

texture::~texture()
{
    if(handle != 0)
    {
        glDeleteTextures(1, &handle);
    }
}
