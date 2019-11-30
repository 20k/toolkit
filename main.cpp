#include <iostream>
#include "render_window.hpp"

int main()
{
    render_window window({1000, 1000}, "Hello", window_flags::VIEWPORTS);

    while(!window.should_close())
    {
        window.poll();

        window.display();
    }

    return 0;
}
