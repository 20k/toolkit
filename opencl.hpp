#ifndef OPENCL_HPP_INCLUDED
#define OPENCL_HPP_INCLUDED

#include <vector>
#include <cl/cl.h>

namespace cl
{
    struct event
    {
        cl_event e = nullptr;

        event(const event& other);
        event& operator=(const event& other);
        event(event&& other);
        event& operator=(event&& other);
        ~event();
    };

    cl_event exec_1d(cl_command_queue cqueue, cl_kernel kernel, const std::vector<cl_mem>& args, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<cl_event>& waitlist);
}
#endif // OPENCL_HPP_INCLUDED
