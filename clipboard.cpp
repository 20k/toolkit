#include "clipboard.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <GLFW/glfw3.h>
#endif // __EMSCRIPTEN__

#ifdef __EMSCRIPTEN__
EM_JS(void, copy_js, (const char* data),
{
    var set_name = UTF8ToString(data);

    Module.clipbuffer.focus();
    Module.clipbuffer.value = set_name;
    Module.clipbuffer.setSelectionRange(0, Module.clipbuffer.value.length);
    var succeeded = document.execCommand('copy');
});
#endif // __EMSCRIPTEN__

void clipboard::set(const std::string& data)
{
    #ifndef __EMSCRIPTEN__
    glfwSetClipboardString(NULL, data.c_str());
    #else
    copy_js(data.c_str());
    #endif
}
