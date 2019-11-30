#include <iostream>
#include "render_window.hpp"
#include "vertex.hpp"
#include "sfml_compatibility.hpp"
#include <networking/networking.hpp>

int main()
{
    render_window window({1000, 1000}, "Hello", window_flags::VIEWPORTS);

    while(!window.should_close())
    {
        window.poll();

        vertex vbase;
        vbase.colour = {1,1,1,1};

        vertex v1 = vbase;
        vertex v2 = vbase;
        vertex v3 = vbase;

        v1.position = {10, 10};
        v2.position = {50, 10};
        v3.position = {10, 50};

        window.render({v1, v2, v3}, nullptr);

        window.display();
    }

    assert(false);

    return 0;
}
