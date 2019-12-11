#include "texture.hpp"
#include <GL/glew.h>
#include <GL/gl.h>

void texture::load_from_memory(const uint8_t* pixels_rgba, vec2i _dim)
{
    dim = _dim;

    glGenTextures(1, &handle);
    glBindTexture(GL_TEXTURE_2D, handle);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dim.x(), dim.y(), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_rgba);
    glGenerateMipmapEXT(GL_TEXTURE_2D);
}

vec2i texture::get_size()
{
    return dim;
}
