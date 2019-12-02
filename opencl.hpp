#ifndef OPENCL_HPP_INCLUDED
#define OPENCL_HPP_INCLUDED

#include <vector>
#include <map>
#include <string>
#include <cl/cl.h>

namespace cl
{
    template<typename T, cl_int(*U)(T), cl_int(*V)(T), typename derived>
    struct base
    {
        T data;

        base()
        {
            data = nullptr;
        }

        base(const derived& other)
        {
            data = other.data;

            if(data)
            {
                U(data);
            }
        }

        derived& operator=(const derived& other)
        {
            if(this == &other)
                return *this;

            if(data)
            {
                V(data);
            }

            data = other.data;

            if(data)
            {
                U(data);
            }

            return *this;
        }

        base(derived&& other)
        {
            data = other.data;
            other.data = nullptr;
        }

        derived& operator=(derived&& other)
        {
            if(data)
            {
                V(data);
            }

            data = other.data;
            other.data = nullptr;

            return *this;
        }

        ~base()
        {
            if(data)
            {
                V(data);
            }
        }
    };

    struct event
    {
        base<cl_event, clRetainEvent, clReleaseEvent, event> native_event;
    };

    struct program;

    struct kernel
    {
        base<cl_kernel, clRetainKernel, clReleaseKernel, kernel> native_kernel;

        kernel();
        kernel(program& p, const std::string& name);
        kernel(cl_kernel k); ///non retaining

        std::string name;
    };

    struct program;

    struct context
    {
        std::vector<program> programs;
        std::map<std::string, kernel> kernels;
        cl_device_id selected_device;

        base<cl_context, clRetainContext, clReleaseContext, context> native_context;

        context();
        void register_program(program& p);
    };

    struct program
    {
        base<cl_program, clRetainProgram, clReleaseProgram, program> native_program;

        program(context& ctx, const std::string& data, bool is_file = true);
        void build(context& ctx, const std::string& options);
    };

    struct command_queue
    {
        base<cl_command_queue, clRetainCommandQueue, clReleaseCommandQueue, command_queue> native_command_queue;
        base<cl_context, clRetainContext, clReleaseContext, context> native_context;

        command_queue(context& ctx, cl_command_queue_properties props = 0);
    };

    struct mem_object
    {
        base<cl_mem, clRetainMemObject, clReleaseMemObject, mem_object> native_mem_object;
    };

    /*struct buffer
    {
        mem_object mem;


    };*/

    /*struct arg_info
    {
        void* ptr = nullptr;
        int64_t size = 0;
    };

    struct args
    {
        std::vector<arg_info> arg_list;

        template<typename T>
        inline
        void push_back(T& val)
        {
            arg_info inf;
            inf.ptr = &val;
            inf.size = sizeof(T);

            arg_list.push_back(inf);
        }
    };*/

    //cl_event exec_1d(cl_command_queue cqueue, cl_kernel kernel, const std::vector<cl_mem>& args, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<cl_event>& waitlist);
}
#endif // OPENCL_HPP_INCLUDED
