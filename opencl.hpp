#ifndef OPENCL_HPP_INCLUDED
#define OPENCL_HPP_INCLUDED

#include <vector>
#include <map>
#include <string>
#include <cl/cl.h>
#include <memory>
#include <GL/glew.h>
#include <gl/gl.h>
#include <array>
#include <vec/vec.hpp>
#include <assert.h>
#include <functional>

namespace cl
{
    template<typename T, cl_int(*U)(T), cl_int(*V)(T)>
    struct base
    {
        T data;

        base()
        {
            data = nullptr;
        }

        base(const base<T, U, V>& other)
        {
            data = other.data;

            if(data)
            {
                U(data);
            }
        }

        base<T, U, V>& operator=(const base<T, U, V>& other)
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

        base<T, U, V>(base<T, U, V>&& other)
        {
            data = other.data;
            other.data = nullptr;
        }

        base<T, U, V>& operator=(base<T, U, V>&& other)
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

    struct arg_info
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
    };


    struct event
    {
        base<cl_event, clRetainEvent, clReleaseEvent> native_event;
    };

    struct program;

    struct kernel
    {
        base<cl_kernel, clRetainKernel, clReleaseKernel> native_kernel;

        kernel();
        kernel(program& p, const std::string& name);
        kernel(cl_kernel k); ///non retaining

        std::string name;
    };

    struct program;

    struct context
    {
        std::vector<program> programs;
        std::shared_ptr<std::map<std::string, kernel>> kernels;
        cl_device_id selected_device;

        base<cl_context, clRetainContext, clReleaseContext> native_context;

        context();
        explicit context(bool); ///defer context creation

        void register_program(program& p);
    };

    struct program
    {
        base<cl_program, clRetainProgram, clReleaseProgram> native_program;

        program(context& ctx, const std::string& data, bool is_file = true);
        program(context& ctx, const std::vector<std::string>& data, bool is_file = true);
        void build(context& ctx, const std::string& options);
    };

    struct command_queue;

    struct mem_object
    {
        base<cl_mem, clRetainMemObject, clReleaseMemObject> native_mem_object;
    };

    struct buffer : mem_object
    {
        base<cl_context, clRetainContext, clReleaseContext> native_context;
        int64_t alloc_size = 0;

        buffer(cl::context& ctx);

        void alloc(int64_t bytes);
        void write(command_queue& write_on, const char* ptr, int64_t bytes);

        template<typename T>
        void write(command_queue& write_on, const std::vector<T>& data)
        {
            if(data.size() == 0)
                return;

            write(write_on, (const char*)&data[0], data.size() * sizeof(T));
        }

        void read(command_queue& read_on, char* ptr, int64_t bytes);

        template<typename T>
        std::vector<T> read(command_queue& read_on)
        {
            std::vector<T> ret;

            if(alloc_size == 0)
                return ret;

            ret.resize(alloc_size / sizeof(T));

            read(read_on, (char*)&ret[0], alloc_size);

            return ret;
        }
    };

    struct image : mem_object
    {
        base<cl_context, clRetainContext, clReleaseContext> native_context;

        int dimensions = 1;
        std::array<int64_t, 3> sizes = {1, 1, 1};

        image(cl::context& ctx);

        void alloc_impl(int dims, const std::array<int64_t, 3>& sizes, const cl_image_format& format);

        template<int N>
        void alloc(const vec<N, int>& in_dims, const cl_image_format& format)
        {
            //assert(in_dims.size() == N);

            static_assert(N > 0 && N <= 3);

            std::array<int64_t, 3> storage = {1,1,1};

            for(int i=0; i < N; i++)
                storage[i] = in_dims.v[i];

            return alloc_impl(N, storage, format);
        }

        void alloc(std::initializer_list<int> init, const cl_image_format& format)
        {
            assert(init.size() <= 3 && init.size() > 0);

            std::array<int64_t, 3> storage = {1,1,1};

            int idx = 0;

            for(auto& i : init)
            {
                storage[idx] = i;
                idx++;
            }

            return alloc_impl(init.size(), storage, format);
        }

        void clear(cl::command_queue& cqueue);

        /*void write(command_queue& write_on, const char* ptr, int64_t bytes);

        template<typename T>
        void write(command_queue& write_on, const std::vector<T>& data)
        {
            if(data.size() == 0)
                return;

            write(write_on, (const char*)&data[0], data.size() * sizeof(T));
        }*/
    };

    struct command_queue
    {
        base<cl_command_queue, clRetainCommandQueue, clReleaseCommandQueue> native_command_queue;
        base<cl_context, clRetainContext, clReleaseContext> native_context;
        std::shared_ptr<std::map<std::string, kernel>> kernels;

        command_queue(context& ctx, cl_command_queue_properties props = 0);

        void exec(const std::string& kname, args& pack, const std::vector<int>& global_ws, const std::vector<int>& local_ws);
        void block();
    };

    struct gl_rendertexture : mem_object
    {
        bool acquired = false;
        base<cl_context, clRetainContext, clReleaseContext> native_context;
        int w = 0;
        int h = 0;

        GLuint texture_id = 0;

        gl_rendertexture(context& ctx);

        void create(int w, int h);
        void create_from_texture(GLuint texture_id);
        void create_from_framebuffer(GLuint framebuffer_id);

        void acquire(command_queue& cqueue);
        void unacquire(command_queue& cqueue);
    };

    template<int N, typename T>
    struct flip
    {
        int counter = 0;
        std::array<T, N> buffers;

        template<typename... V>
        flip(V&&... args) : buffers{std::forward<V>(args)...}
        {

        }

        template<typename U, typename... V>
        void apply(U in, V&&... args)
        {
            for(int i=0; i < N; i++)
            {
                std::invoke(in, buffers[i], std::forward<V>(args)...);
            }
        }

        T& get(int offset = 0)
        {
            int circ = (counter + offset) % N;

            return buffers[circ];
        }

        void next()
        {
            counter++;
            counter %= N;
        }
    };

    void copy(cl::command_queue& cqueue, cl::buffer& b1, cl::buffer& b2);

    template<typename T, typename U>
    void copy_image(cl::command_queue& cqueue, T& i1, U& i2, vec3i origin, vec3i region)
    {
        size_t src[3] = {(int)origin.x(), (int)origin.y(), (int)origin.z()};
        size_t iregion[3] = {(int)region.x(), (int)region.y(), (int)region.z()};

        clEnqueueCopyImage(cqueue.native_command_queue.data, i1.native_mem_object.data, i2.native_mem_object.data, src, src, iregion, 0, nullptr, nullptr);
    }

    template<typename T, typename U>
    void copy_image(cl::command_queue& cqueue, T& i1, U& i2, vec2i origin, vec2i region)
    {
        size_t src[3] = {(int)origin.x(), (int)origin.y(), 0};
        size_t iregion[3] = {(int)region.x(), (int)region.y(), 1};

        clEnqueueCopyImage(cqueue.native_command_queue.data, i1.native_mem_object.data, i2.native_mem_object.data, src, src, iregion, 0, nullptr, nullptr);
    }

    //cl_event exec_1d(cl_command_queue cqueue, cl_kernel kernel, const std::vector<cl_mem>& args, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<cl_event>& waitlist);
}

template<>
inline
void cl::args::push_back<cl::mem_object>(cl::mem_object& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

template<>
inline
void cl::args::push_back<cl::buffer>(cl::buffer& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

template<>
inline
void cl::args::push_back<cl::gl_rendertexture>(cl::gl_rendertexture& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

template<>
inline
void cl::args::push_back<cl::image>(cl::image& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

#endif // OPENCL_HPP_INCLUDED
