#include "clipboard.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <GLFW/glfw3.h>
#endif // __EMSCRIPTEN__

#ifdef __EMSCRIPTEN__
EM_JS(void, copy_js, (),
{
    Module.clipbuffer.focus();
    Module.clipbuffer.value = "hellothere";
    Module.clipbuffer.setSelectionRange(0, Module.clipbuffer.value.length);
    var succeeded = document.execCommand('copy');
});
#endif // __EMSCRIPTEN__

void clipboard::set(const std::string& data)
{
    #ifndef __EMSCRIPTEN__
    glfwSetClipboardString(NULL, data.c_str());
    #else
    copy_js();
    #endif
}
