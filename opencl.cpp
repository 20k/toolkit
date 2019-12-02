#include "opencl.hpp"

cl::event::event(const event& other)
{
    e = other.e;

    if(e)
    {
        clRetainEvent(e);
    }
}

cl::event& cl::event::operator=(const event& other)
{
    if(this == &other)
        return *this;

    if(e)
    {
        clReleaseEvent(e);
    }

    e = other.e;

    if(e)
    {
        clRetainEvent(e);
    }

    return *this;
}

cl::event::event(event&& other)
{
    e = other.e;
    other.e = nullptr;
}

cl::event& cl::event::operator=(event&& other)
{
    if(e)
    {
        clReleaseEvent(e);
    }

    e = other.e;

    other.e = nullptr;

    return *this;
}

cl::event::~event()
{
    if(e)
    {
        clReleaseEvent(e);
    }
}
