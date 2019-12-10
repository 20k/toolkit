#include "serialisables.hpp"

#include <networking/serialisable.hpp>
#include "render_window.hpp"

DEFINE_SERIALISE_FUNCTION(render_settings)
{
    SERIALISE_SETUP();

    DO_FSERIALISE(width);
    DO_FSERIALISE(height);
    DO_FSERIALISE(flags);
}
