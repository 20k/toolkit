#ifndef STACKTRACE_HPP_INCLUDED
#define STACKTRACE_HPP_INCLUDED

#include <string>

struct stack_frame
{
    std::string name;
    std::string file;
    size_t line = 0;
};

std::string get_stacktrace();
stack_frame frame_from_ptr(void* ptr);

#endif // STACKTRACE_HPP_INCLUDED
