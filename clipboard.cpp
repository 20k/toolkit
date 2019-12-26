#include "clipboard.hpp"

#include <stdexcept>

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

EM_JS(void, update_clipboard_data, (),
{
    Module.osclipdata = window.clipboardData.getData('Text');
});

EM_JS(int, get_osclipdata_length, (),
{
    return Module.osclipdata.length;
});

EM_JS(void, get_osclipdata, (char* out),
{
    stringToUTF8(Module.osclipdata, out, Module.osclipdata.length+1);
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

std::string clipboard::get()
{
    #ifndef __EMSCRIPTEN__
    const char* ptr = glfwGetClipboardString(NULL);

    if(ptr == nullptr)
        throw std::runtime_error("Clipboard Error");

    return ptr;
    #else
    update_clipboard_data();
    int clip_len = get_osclipdata_length();
    std::string clip_buf;
    clip_buf.resize(clip_len + 1);
    get_osclipdata(&clip_buf[0]);

    int cstrlen = strlen(clip_buf.c_str());

    clip_buf = std::string(clip_buf.begin(), clip_buf.begin() + cstrlen);

    return clip_buf;
    #endif
}
