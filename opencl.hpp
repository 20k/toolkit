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
#include <variant>
#include <string_view>
#include <atomic>
#include <optional>
#include <functional>
#include <span>
#include <latch>

#ifndef __clang__
#include <stdfloat>
#endif

namespace cl_adl
{
    template<typename T>
    inline
    std::array<T, 1> type_to_array(const T& in)
    {
        return std::array<T, 1>{in};
    }
}

namespace cl
{
    #ifndef __clang__
    using cl_float16_impl = std::float16_t;
    #else
    using cl_float16_impl = _Float16;
    #endif

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

        void borrow(T raw)
        {
            if(data == raw)
                return;

            if(data)
            {
                V(data);
            }

            data = raw;

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

        base(base<T, U, V>&& other)
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

        virtual ~base()
        {
            if(data)
            {
                V(data);
            }
        }
    };

    struct local_memory
    {
        size_t size = 0;

        local_memory(size_t in) : size(in)
        {
        }
    };

    #define DECLARE_VECTOR_OPENCL_TYPE(real_type, cl_type) \
        inline cl_type type_to_opencl(real_type v){return v;} \
        inline cl_type##2 type_to_opencl(cl_type##2 v){return v;} \
        inline cl_type##4 type_to_opencl(cl_type##4 v){return v;} \

    DECLARE_VECTOR_OPENCL_TYPE(int64_t, cl_long)
    DECLARE_VECTOR_OPENCL_TYPE(uint64_t, cl_ulong)
    DECLARE_VECTOR_OPENCL_TYPE(int32_t, cl_int)
    DECLARE_VECTOR_OPENCL_TYPE(uint32_t, cl_uint)
    DECLARE_VECTOR_OPENCL_TYPE(int16_t, cl_short)
    DECLARE_VECTOR_OPENCL_TYPE(uint16_t, cl_ushort)
    DECLARE_VECTOR_OPENCL_TYPE(int8_t, cl_char)
    DECLARE_VECTOR_OPENCL_TYPE(uint8_t, cl_uchar)
    DECLARE_VECTOR_OPENCL_TYPE(double, cl_double)
    DECLARE_VECTOR_OPENCL_TYPE(float, cl_float)
    DECLARE_VECTOR_OPENCL_TYPE(cl_float16_impl, cl_half)

    inline
    cl_mem type_to_opencl(std::nullptr_t){return nullptr;}

    inline
    cl_sampler type_to_opencl(cl_sampler v){return v;}

    inline
    cl_command_queue type_to_opencl(cl_command_queue v){return v;}

    inline
    size_t type_to_opencl(const local_memory& v){return v.size;}

    template<typename T, std::size_t N>
    inline
    auto to_opencl_from_array(std::array<T, N> raw)
    {
        #define MAPS_TO(real_type, cl_type) \
        if constexpr(std::is_same_v<T, real_type> && N == 4) \
            return cl_type##4{raw[0], raw[1], raw[2], raw[3]}; \
        if constexpr(std::is_same_v<T, real_type> && N == 3) \
            return cl_type##3{raw[0], raw[1], raw[2]}; \
        if constexpr(std::is_same_v<T, real_type> && N == 2) \
            return cl_type##2{raw[0], raw[1]}; \
        if constexpr(std::is_same_v<T, real_type> && N == 1) \
            return cl_type{raw[0]};

        if constexpr (N == 1)
            return type_to_opencl(raw[0]);

        MAPS_TO(int64_t, cl_long);
        MAPS_TO(uint64_t, cl_ulong);
        MAPS_TO(int32_t, cl_int);
        MAPS_TO(uint32_t, cl_uint);
        MAPS_TO(int16_t, cl_short);
        MAPS_TO(uint16_t, cl_ushort);
        MAPS_TO(int8_t, cl_char);
        MAPS_TO(uint8_t, cl_uchar);

        MAPS_TO(double, cl_double);
        MAPS_TO(float, cl_float);
        MAPS_TO(cl_float16_impl, cl_half);

        assert(false);
    }


    using shared_mem_object = base<cl_mem, clRetainMemObject, clReleaseMemObject>;

    struct command_queue;
    struct mem_object;

    namespace mem_object_access
    {
        enum type
        {
            READ_WRITE,
            READ,
            WRITE,
            NONE
        };
    }

    struct mem_object
    {
        shared_mem_object native_mem_object;

        cl_mem_flags get_flags();
        std::optional<mem_object> get_parent();
    };

    inline
    bool operator<(const mem_object& o1, const mem_object& o2)
    {
        return o1.native_mem_object.data < o2.native_mem_object.data;
    }

    struct access_storage
    {
        std::map<mem_object, std::vector<cl_mem_flags>> store;

        void add(const mem_object& in);
    };

    struct kernel;

    struct callback_helper_base
    {
        virtual void callback(cl_kernel kern, int idx)
        {
            assert(false);
        }

        virtual ~callback_helper_base(){}
    };

    template<typename T>
    struct callback_helper_generic : callback_helper_base
    {
        T t;
        decltype(to_opencl_from_array(cl_adl::type_to_array(std::declval<T>()))) native_type;

        callback_helper_generic(T&& in) : t(std::move(in)){native_type = to_opencl_from_array(cl_adl::type_to_array(t));}
        callback_helper_generic(const T& in) : t(in){native_type = to_opencl_from_array(cl_adl::type_to_array(t));}

        void callback(cl_kernel kern, int idx) override
        {
            auto [ptr, size] = get_ptr();

            clSetKernelArg(kern, idx, size, ptr);
        }

        std::pair<void*, size_t> get_ptr()
        {
            if constexpr(std::is_base_of_v<command_queue, T>)
            {
                return {&this->t.native_command_queue.data, sizeof(cl_command_queue)};
            }
            else if constexpr(std::is_base_of_v<mem_object, T>)
            {
                return {&this->t.native_mem_object.data, sizeof(cl_mem)};
            }
            else if constexpr(std::is_base_of_v<local_memory, T>)
            {
                return {nullptr, t.size};
            }
            else
            {
                static_assert(std::is_trivially_copyable_v<T>);

                return {&this->t, sizeof(T)};
            }
        }
    };

    struct args
    {
        std::vector<std::unique_ptr<callback_helper_base>> arg_list;

        template<typename T, typename... U>
        inline
        void push_back(const T& t, U&&... u)
        {
            push_back(t);
            push_back(std::forward<U>(u)...);
        }

        template<typename T>
        inline
        void push_back(const T& val)
        {
            std::unique_ptr<callback_helper_base> owned = std::make_unique<callback_helper_generic<T>>(val);

            arg_list.push_back(std::move(owned));
        }
    };

    struct event
    {
        base<cl_event, clRetainEvent, clReleaseEvent> native_event;

        void block();
        bool is_finished();
        void set_completion_callback(void (CL_CALLBACK* pfn_notify)(cl_event event, cl_int event_command_status, void *user_data), void* userdata);
    };


    inline
    cl_event type_to_opencl(const event& e){return e.native_event.data;}

    struct context;
    struct kernel;

    struct program
    {
        struct binary_tag{};

        cl_device_id selected_device;

        struct async_context
        {
            std::latch latch{1};
            std::atomic_bool cancelled{false};
            std::map<std::string, cl::kernel> built_kernels;
        };

        base<cl_program, clRetainProgram, clReleaseProgram> native_program;
        std::shared_ptr<async_context> async;
        bool must_write_to_cache_when_built = false;
        std::string name_in_cache;

        program(const context& ctx);
        program(const context& ctx, const std::string& data, bool is_file = true);
        program(const context& ctx, const std::vector<std::string>& data, bool is_file = true);
        program(const context& ctx, const std::string& binary_data, binary_tag tag);

        std::string get_binary();

        void build(const context& ctx, const std::string& options);
        void ensure_built();
        bool is_built();
        void cancel(); ///purely optional
    };

    program build_program_with_cache(const context& ctx, const std::vector<std::string>& data, bool is_file = true, const std::string& options = "", const std::vector<std::string>& extra_deps = {}, const std::string& cache_name = "");

    struct kernel
    {
        base<cl_kernel, clRetainKernel, clReleaseKernel> native_kernel;

        kernel();
        kernel(program& p, const std::string& name);
        kernel(cl_kernel k); ///non retaining

        std::string name;
        int argument_count = 0;

        void set_args(cl::args& pack);

        template<typename... T>
        void set_args(T&&... args)
        {
            cl::args in_args;
            in_args.push_back(std::forward<T>(args)...);

            set_args(in_args);
        }

        cl_program fetch_program();

        kernel clone();
    };

    struct pending_kernel
    {
        std::optional<cl::kernel> kernel;
        std::latch latch{1};
    };

    struct shared_kernel_info
    {
        std::vector<std::map<std::string, kernel, std::less<>>> kernels;
        std::vector<std::pair<std::string, std::shared_ptr<pending_kernel>>> pending_kernels;
        std::mutex mut;

        bool promote_pending(const std::string& name);
    };

    struct context
    {
        std::shared_ptr<shared_kernel_info> shared;
        cl_device_id selected_device;
        std::string platform_name;

        base<cl_context, clRetainContext, clReleaseContext> native_context;

        context();
        explicit context(bool); ///defer context creation

        void register_program(program& p);
        void deregister_program(int idx);

        void register_kernel(const cl::kernel& kern, std::optional<std::string> name_override = std::nullopt, bool can_overlap_existing = false);
        void register_kernel(std::shared_ptr<pending_kernel> pending, const std::string& produced_name);

        kernel fetch_kernel(std::string_view name);
        void remove_kernel(std::string_view name);
    };

    struct command_queue;

    std::optional<cl::mem_object> get_parent(const cl::mem_object& in);
    cl_mem_flags get_flags(const cl::mem_object& in);

    bool requires_memory_barrier(cl_mem in1, cl_mem in2);

    template<typename T>
    struct read_info
    {
        T* data = nullptr;
        event evt;

        void consume()
        {
            if(data == nullptr)
                return;

            evt.block();
            delete [] data;
            data = nullptr;
        }

        ~read_info()
        {
            consume();
        }
    };

    struct buffer : mem_object
    {
        base<cl_context, clRetainContext, clReleaseContext> native_context;
        int64_t alloc_size = 0;

        buffer(cl::context& ctx);

        void alloc(int64_t bytes);
        cl::event write(command_queue& write_on, const char* ptr, int64_t bytes, int64_t offset);
        cl::event write(command_queue& write_on, const char* ptr, int64_t bytes);

        template<typename T>
        cl::event write(command_queue& write_on, const std::vector<T>& data)
        {
            return write(write_on, std::span{data});
        }

        template<typename T>
        cl::event write(command_queue& write_on, std::span<T> data)
        {
            if(data.size() == 0)
                return cl::event();

            return write(write_on, (const char*)data.data(), data.size() * sizeof(T));
        }

        event write_async(command_queue& write_on, const char* ptr, int64_t bytes);

        template<typename T>
        event write_async(command_queue& write_on, std::span<T> data)
        {
            if(data.size() == 0)
                return event();

            return write_async(write_on, (const char*)data.data(), data.size() * sizeof(T));
        }

        template<typename T>
        event write_async(command_queue& write_on, const std::vector<T>& data)
        {
            return write_async(write_on, std::span{data});
        }

        void read(command_queue& read_on, char* ptr, int64_t bytes);
        void read(command_queue& read_on, char* ptr, int64_t bytes, int64_t offset);

        event read_async(command_queue& read_on, char* ptr, int64_t bytes, const std::vector<cl::event>& wait_on);

        template<typename T>
        read_info<T> read_async(command_queue& read_on, int64_t elements, const std::vector<cl::event>& deps = std::vector<cl::event>())
        {
            read_info<T> ret;

            if(elements == 0)
                return ret;

            assert(elements * sizeof(T) <= alloc_size);

            ret.data = new T[elements];
            ret.evt = read_async(read_on, (char*)ret.data, elements * sizeof(T), deps);

            return ret;
        }

        cl::event set_to_zero(command_queue& write_on);
        cl::event fill(command_queue& write_on, const void* pattern, size_t pattern_size, size_t size, const std::vector<cl::event>& deps = std::vector<cl::event>());

        template<typename T>
        cl::event fill(command_queue& write_on, const T& value)
        {
            assert((alloc_size % sizeof(T)) == 0);

            return fill(write_on, (void*)&value, sizeof(T), alloc_size);
        }

        template<typename T>
        std::vector<T> read(command_queue& read_on)
        {
            std::vector<T> ret;

            if(alloc_size == 0)
                return ret;

            assert((alloc_size % sizeof(T)) == 0);

            ret.resize(alloc_size / sizeof(T));

            read(read_on, (char*)&ret[0], alloc_size);

            return ret;
        }

        cl::buffer as_read_only();
        cl::buffer as_write_only();
        cl::buffer as_device_read_only();
        cl::buffer as_device_write_only();
        cl::buffer as_device_inaccessible();

        cl::buffer slice(int64_t offset, int64_t length, cl_mem_flags flags = 0);
    };

    inline
    cl_mem type_to_opencl(const mem_object& in){return in.native_mem_object.data;};

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

    namespace image_flags
    {
        enum type
        {
            NONE,
            ARRAY,
        };
    }

    struct image : image_base
    {
        base<cl_context, clRetainContext, clReleaseContext> native_context;

        int dimensions = 1;

        image(cl::context& ctx);

        void alloc_impl(int dims, const std::array<int64_t, 3>& sizes, const cl_image_format& format, image_flags::type t = image_flags::NONE);

        template<int N>
        void alloc(const vec<N, int>& in_dims, const cl_image_format& format, image_flags::type t = image_flags::NONE)
        {
            //assert(in_dims.size() == N);

            static_assert(N > 0 && N <= 3);

            std::array<int64_t, 3> storage = {1,1,1};

            for(int i=0; i < N; i++)
                storage[i] = in_dims.v[i];

            return alloc_impl(N, storage, format, t);
        }

        void alloc(std::initializer_list<int> init, const cl_image_format& format, image_flags::type t = image_flags::NONE)
        {
            assert(init.size() <= 3 && init.size() > 0);

            std::array<int64_t, 3> storage = {1,1,1};

            int idx = 0;

            for(auto& i : init)
            {
                storage[idx] = i;
                idx++;
            }

            return alloc_impl(init.size(), storage, format, t);
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
        int mip_levels = 0;

        image_with_mipmaps(cl::context& ctx);

        void alloc_impl(int dims, const std::array<int64_t, 3>& sizes, int mip_levels, const cl_image_format& format);

        template<int N>
        void alloc(const vec<N, int>& in_dims, int _mip_levels, const cl_image_format& format)
        {
            static_assert(N > 0 && N <= 3);

            std::array<int64_t, 3> storage = {1,1,1};

            for(int i=0; i < N; i++)
                storage[i] = in_dims.v[i];

            return alloc_impl(N, storage, _mip_levels, format);
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

    namespace detail
    {
        template<typename T>
        inline
        std::vector<size_t> array_to_vec(const T& in)
        {
            std::vector<size_t> out;

            for(const auto& i : in)
                out.push_back(i);

            return out;
        }
    }

    struct command_queue
    {
        base<cl_command_queue, clRetainCommandQueue, clReleaseCommandQueue> native_command_queue;
        base<cl_context, clRetainContext, clReleaseContext> native_context;

        std::shared_ptr<shared_kernel_info> shared;

        command_queue(context& ctx, cl_command_queue_properties props = 0);

        event enqueue_marker(const std::vector<event>& deps);

        ///apparently past me was not very bright, and used an int max work size here
        event exec(cl::kernel& kern, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<event>& deps = {});
        event exec(const std::string& kname, args& pack, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<event>& deps);
        event exec(const std::string& kname, args& pack, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws);

        template<typename T>
        event exec(const std::string& kname, args& pack, const T& global_ws, const T& local_ws, const std::vector<event>& deps = {})
        {
            auto global_as_array = cl_adl::type_to_array(global_ws);
            auto local_as_array = cl_adl::type_to_array(local_ws);

            std::vector<size_t> global_as_vec = detail::array_to_vec(global_as_array);
            std::vector<size_t> local_as_vec = detail::array_to_vec(local_as_array);

            return exec(kname, pack, global_as_vec, global_as_vec, deps);
        }

        void block();
        void flush();

    protected:
        command_queue();
    };

    inline
    cl_command_queue type_to_opencl(command_queue& in){return in.native_command_queue.data;};

    struct device_command_queue : command_queue
    {
        device_command_queue(context& ctx, cl_command_queue_properties props = 0);
    };

    struct gl_rendertexture : image_base
    {
        bool sharing_is_available = false;
        bool acquired = false;
        base<cl_context, clRetainContext, clReleaseContext> native_context;

        GLuint texture_id = 0;

        gl_rendertexture(context& ctx);

        void create(int w, int h);
        void create_from_texture(GLuint texture_id);
        void create_from_texture_with_mipmaps(GLuint texture_id, int mip_level);
        void create_from_framebuffer(GLuint framebuffer_id);

        event acquire(command_queue& cqueue);
        event unacquire(command_queue& cqueue);

        event acquire(command_queue& cqueue, const std::vector<cl::event>& events);
        event unacquire(command_queue& cqueue, const std::vector<cl::event>& events);
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

    event copy(cl::command_queue& cqueue, cl::buffer& source, cl::buffer& dest, const std::vector<cl::event>& events = {});

    template<typename T, typename U>
    void copy_image(cl::command_queue& cqueue, T& src, U& dst, vec3i origin, vec3i region)
    {
        size_t origin_arr[3] = {(int)origin.x(), (int)origin.y(), (int)origin.z()};
        size_t iregion[3] = {(int)region.x(), (int)region.y(), (int)region.z()};

        clEnqueueCopyImage(cqueue.native_command_queue.data, src.native_mem_object.data, dst.native_mem_object.data, origin_arr, origin_arr, iregion, 0, nullptr, nullptr);
    }

    template<typename T, typename U>
    cl::event copy_image(cl::command_queue& cqueue, T& src, U& dst, vec2i origin, vec2i region)
    {
        size_t origin_arr[3] = {(int)origin.x(), (int)origin.y(), 0};
        size_t iregion[3] = {(int)region.x(), (int)region.y(), 1};

        cl::event ret;

        clEnqueueCopyImage(cqueue.native_command_queue.data, src.native_mem_object.data, dst.native_mem_object.data, origin_arr, origin_arr, iregion, 0, nullptr, &ret.native_event.data);

        return ret;
    }

    std::string get_extensions(context& ctx);

    bool supports_extension(cl_device_id id, const std::string& name);
    bool supports_extension(context& ctx, const std::string& name);

    std::vector<char> get_device_info(cl_device_id id, cl_device_info param);

    template<typename T>
    inline
    T get_device_info(cl_device_id id, cl_device_info param)
    {
        T ret = T();

        std::vector<char> value = get_device_info(id, param);

        assert(value.size() == sizeof(T));

        memcpy(&ret, value.data(), value.size() * sizeof(char));

        return ret;
    }

    //cl_event exec_1d(cl_command_queue cqueue, cl_kernel kernel, const std::vector<cl_mem>& args, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<cl_event>& waitlist);
}

#endif // OPENCL_HPP_INCLUDED
