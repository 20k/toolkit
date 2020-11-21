#ifndef OPENCL_HPP_INCLUDED
#define OPENCL_HPP_INCLUDED

#include <vector>
#include <map>
#include <string>
#include <CL/cl.h>
#include <memory>
#include <GL/glew.h>
#include <GL/gl.h>
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

        void consume(T raw)
        {
            if(data)
            {
                V(data);
            }

            data = raw;
        }

        void release()
        {
            if(data)
            {
                V(data);
            }

            data = nullptr;
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
        const void* ptr = nullptr;
        int64_t size = 0;
    };

    struct args
    {
        std::vector<arg_info> arg_list;

        template<typename T>
        inline
        void push_back(const T& val)
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

        void block();
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

        void write_async(command_queue& write_on, const char* ptr, int64_t bytes);

        void read(command_queue& read_on, char* ptr, int64_t bytes);

        void set_to_zero(command_queue& write_on);

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

    struct image_base : mem_object
    {
        void clear(cl::command_queue& cqueue);
        std::array<int64_t, 3> sizes = {1, 1, 1};

        void read_impl(cl::command_queue& cqueue, const vec<4, size_t>& origin, const vec<4, size_t>& region, char* out);

        template<int N, typename T>
        std::vector<T> read(cl::command_queue& cqueue, const vec<N, size_t>& origin, const vec<N, size_t>& region)
        {
            vec<4, size_t> lorigin = {0,0,0,0};
            vec<4, size_t> lregion = {1,1,1,1};

            for(int i=0; i < N; i++)
            {
                lorigin.v[i] = origin.v[i];
                lregion.v[i] = region.v[i];
            }

            std::vector<T> ret;

            int elements = 1;

            for(int i=0; i < N; i++)
            {
                elements *= region.v[i];
            }

            ret.resize(elements);

            if(ret.size() == 0)
                return ret;

            read_impl(cqueue, lorigin, lregion, (char*)&ret[0]);

            return ret;
        }

        template<int N>
        vec<N, size_t> size()
        {
            vec<N, size_t> ret;

            static_assert(N <= 3);

            for(int i=0; i < N; i++)
            {
                ret.v[i] = sizes[i];
            }

            return ret;
        }
    };

    struct image : image_base
    {
        base<cl_context, clRetainContext, clReleaseContext> native_context;

        int dimensions = 1;

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

        void write_impl(command_queue& write_on, const char* ptr, const vec<3, size_t>& origin, const vec<3, size_t>& region);

        template<int N>
        void write(command_queue& write_on, const char* ptr, const vec<N, size_t>& origin, const vec<N, size_t>& region)
        {
            vec<3, size_t> forigin;
            vec<3, size_t> fregion = {1,1,1};

            for(int i=0; i < N && i < 3; i++)
            {
                forigin.v[i] = origin.v[i];
                fregion.v[i] = region.v[i];
            }

            write_impl(write_on, ptr, forigin, fregion);
        }

        /*template<typename T>
        void write(command_queue& write_on, const std::vector<T>& data)
        {
            if(data.size() == 0)
                return;

            write(write_on, (const char*)&data[0], data.size() * sizeof(T));
        }*/
    };


    struct image_with_mipmaps : image_base
    {
        base<cl_context, clRetainContext, clReleaseContext> native_context;

        int dimensions = 1;

        image_with_mipmaps(cl::context& ctx);

        void alloc_impl(int dims, const std::array<int64_t, 3>& sizes, int mip_levels, const cl_image_format& format);

        template<int N>
        void alloc(const vec<N, int>& in_dims, int mip_levels, const cl_image_format& format)
        {
            static_assert(N > 0 && N <= 3);

            std::array<int64_t, 3> storage = {1,1,1};

            for(int i=0; i < N; i++)
                storage[i] = in_dims.v[i];

            return alloc_impl(N, storage, mip_levels, format);
        }

        void write_impl(command_queue& write_on, const char* ptr, const vec<3, size_t>& origin, const vec<3, size_t>& region, int mip_level);

        template<int N>
        void write(command_queue& write_on, const char* ptr, const vec<N, size_t>& origin, const vec<N, size_t>& region, int mip_level)
        {
            vec<3, size_t> forigin;
            vec<3, size_t> fregion = {1,1,1};

            for(int i=0; i < N && i < 3; i++)
            {
                forigin.v[i] = origin.v[i];
                fregion.v[i] = region.v[i];
            }

            write_impl(write_on, ptr, forigin, fregion, mip_level);
        }
    };

    struct command_queue
    {
        base<cl_command_queue, clRetainCommandQueue, clReleaseCommandQueue> native_command_queue;
        base<cl_context, clRetainContext, clReleaseContext> native_context;
        std::shared_ptr<std::map<std::string, kernel>> kernels;

        command_queue(context& ctx, cl_command_queue_properties props = 0);

        event exec(const std::string& kname, args& pack, const std::vector<int>& global_ws, const std::vector<int>& local_ws, const std::vector<event>& deps);
        event exec(const std::string& kname, args& pack, const std::vector<int>& global_ws, const std::vector<int>& local_ws);
        void block();
        void flush();

    protected:
        command_queue();
    };

    struct device_command_queue : command_queue
    {
        device_command_queue(context& ctx, cl_command_queue_properties props = 0);
    };

    struct gl_rendertexture : image_base
    {
        bool acquired = false;
        base<cl_context, clRetainContext, clReleaseContext> native_context;

        GLuint texture_id = 0;

        gl_rendertexture(context& ctx);

        void create(int w, int h);
        void create_from_texture(GLuint texture_id);
        void create_from_texture_with_mipmaps(GLuint texture_id, int mip_level);
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

    bool supports_extension(context& ctx, const std::string& name);

    //cl_event exec_1d(cl_command_queue cqueue, cl_kernel kernel, const std::vector<cl_mem>& args, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<cl_event>& waitlist);
}

template<>
inline
void cl::args::push_back<cl::mem_object>(const cl::mem_object& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

template<>
inline
void cl::args::push_back<cl::buffer>(const cl::buffer& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

template<>
inline
void cl::args::push_back<cl::gl_rendertexture>(const cl::gl_rendertexture& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

template<>
inline
void cl::args::push_back<cl::image>(const cl::image& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

template<>
inline
void cl::args::push_back<cl::image_with_mipmaps>(const cl::image_with_mipmaps& val)
{
    cl::arg_info inf;
    inf.ptr = &val.native_mem_object.data;
    inf.size = sizeof(cl_mem);

    arg_list.push_back(inf);
}

#endif // OPENCL_HPP_INCLUDED
