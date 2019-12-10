#include "base_serialisables.hpp"

#include <networking/serialisable.hpp>
#include "render_window.hpp"

DEFINE_SERIALISE_FUNCTION(render_settings)
{
    SERIALISE_SETUP();

    DO_FSERIALISE(width);
    DO_FSERIALISE(height);
    DO_FSERIALISE(is_srgb);
    DO_FSERIALISE(no_double_buffer);
    DO_FSERIALISE(viewports);
    DO_FSERIALISE(opencl);
}
