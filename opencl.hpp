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

    struct arg_base
    {
        virtual const void* fetch_ptr(){assert(false); return nullptr;};
        virtual size_t fetch_size(){assert(false); return 0;};
        virtual ~arg_base(){}
    };

    template<typename T, typename U>
    struct arg_typed : arg_base
    {
        ///keep the underlying value alive
        std::unique_ptr<T> val;
        ///pointer to the data to pass to the kernel
        U* data = nullptr;

        void take(std::unique_ptr<T>&& v, U* data_ptr)
        {
            val = std::move(v);
            data = data_ptr;
        }

        const void* fetch_ptr() override
        {
            return data;
        }

        size_t fetch_size() override
        {
            return sizeof(U);
        }
    };

    template<typename T, typename U>
    inline
    std::unique_ptr<arg_base> build_from_args(std::unique_ptr<T>&& ptr, U* data)
    {
        arg_typed<T, U>* typed = new arg_typed<T, U>();

        typed->take(std::move(ptr), data);

        return std::unique_ptr<arg_base>(typed);
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

    struct args
    {
        std::vector<std::unique_ptr<arg_base>> arg_list;
        access_storage memory_objects;

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
            std::unique_ptr<T> v = std::make_unique<T>(val);

            if constexpr(std::is_base_of_v<command_queue, T>)
            {
                cl_command_queue* ptr = &v->native_commmand_queue.data;
                push_arg(cl::build_from_args(std::move(v), ptr));
            }
            else if constexpr(std::is_base_of_v<mem_object, T>)
            {
                if(v->native_mem_object.data != nullptr)
                {
                    memory_objects.add(*v);
                }

                cl_mem* ptr = &v->native_mem_object.data;
                push_arg(cl::build_from_args(std::move(v), ptr));
            }
            else
            {
                static_assert(std::is_trivially_copyable_v<T>);

                T* ptr = v.get();
                push_arg(cl::build_from_args(std::move(v), ptr));
            }
        }

    private:

        void push_arg(std::unique_ptr<arg_base>&& base)
        {
            arg_list.push_back(std::move(base));
        }
    };

    bool requires_memory_barrier(args& a1, args& a2);
    bool requires_memory_barrier(const std::vector<shared_mem_object>& a1, const std::vector<shared_mem_object>& a2);

    struct event
    {
        base<cl_event, clRetainEvent, clReleaseEvent> native_event;

        void block();
        bool is_finished();
        void set_completion_callback(void (CL_CALLBACK* pfn_notify)(cl_event event, cl_int event_command_status, void *user_data), void* userdata);
    };

    struct context;
    struct kernel;

    struct program
    {
        struct binary_tag{};

        cl_device_id selected_device;

        struct async_context
        {
            std::atomic_flag finished_waiter;
            std::atomic_bool built{false};
            std::atomic_bool cancelled{false};
            std::map<std::string, cl::kernel> built_kernels;
        };

        base<cl_program, clRetainProgram, clReleaseProgram> native_program;
        std::shared_ptr<async_context> async;

        program(context& ctx);
        program(context& ctx, const std::string& data, bool is_file = true);
        program(context& ctx, const std::vector<std::string>& data, bool is_file = true);
        program(context& ctx, const std::string& binary_data, binary_tag tag);

        std::string get_binary();

        void build(context& ctx, const std::string& options);
        void ensure_built();
        bool is_built();
        void cancel(); ///purely optional
    };

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

    struct context
    {
        std::vector<program> programs;
        std::shared_ptr<std::vector<std::map<std::string, kernel, std::less<>>>> kernels;
        cl_device_id selected_device;

        base<cl_context, clRetainContext, clReleaseContext> native_context;

        context();
        explicit context(bool); ///defer context creation

        void register_program(program& p);
        void deregister_program(int idx);

        void register_kernel(const std::string& name, cl::kernel kern);

        kernel fetch_kernel(std::string_view name);
    };

    struct command_queue;
    struct managed_command_queue;

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
        void write(command_queue& write_on, const char* ptr, int64_t bytes, int64_t offset);
        void write(command_queue& write_on, const char* ptr, int64_t bytes);

        template<typename T>
        void write(command_queue& write_on, const std::vector<T>& data)
        {
            return write(write_on, std::span{data});
        }

        template<typename T>
        void write(command_queue& write_on, std::span<T> data)
        {
            if(data.size() == 0)
                return;

            write(write_on, (const char*)data.data(), data.size() * sizeof(T));
        }

        event write_async(command_queue& write_on, const char* ptr, int64_t bytes);

        void read(command_queue& read_on, char* ptr, int64_t bytes);
        void read(command_queue& read_on, char* ptr, int64_t bytes, int64_t offset);

        event read_async(command_queue& read_on, char* ptr, int64_t bytes, const std::vector<cl::event>& wait_on);

        template<typename T>
        read_info<T> read_async(command_queue& read_on, int64_t elements)
        {
            read_info<T> ret;

            if(elements == 0)
                return ret;

            ret.data = new T[elements];
            ret.evt = read_async(read_on, (char*)ret.data, elements * sizeof(T), {});

            return ret;
        }

        cl::event set_to_zero(command_queue& write_on);
        cl::event fill(command_queue& write_on, const void* pattern, size_t pattern_size, size_t size, const std::vector<cl::event>& deps = std::vector<cl::event>());

        cl::event set_to_zero(managed_command_queue& write_on);
        cl::event fill(managed_command_queue& write_on, const void* pattern, size_t pattern_size, size_t size, const std::vector<cl::event>& deps = std::vector<cl::event>());

        template<typename T>
        cl::event fill(command_queue& write_on, const T& value)
        {
            assert((alloc_size % sizeof(T)) == 0);

            return fill(write_on, (void*)&value, sizeof(T), alloc_size);
        }

        template<typename T>
        cl::event fill(managed_command_queue& write_on, const T& value)
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

    struct command_queue
    {
        base<cl_command_queue, clRetainCommandQueue, clReleaseCommandQueue> native_command_queue;
        base<cl_context, clRetainContext, clReleaseContext> native_context;
        std::shared_ptr<std::vector<std::map<std::string, kernel, std::less<>>>> kernels;

        command_queue(context& ctx, cl_command_queue_properties props = 0);

        event enqueue_marker(const std::vector<event>& deps);

        ///apparently past me was not very bright, and used an int max work size here
        event exec(cl::kernel& kern, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<event>& deps = {});
        event exec(const std::string& kname, args& pack, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<event>& deps);
        event exec(const std::string& kname, args& pack, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws);
        void block();
        void flush();

    protected:
        command_queue();
    };

    struct device_command_queue : command_queue
    {
        device_command_queue(context& ctx, cl_command_queue_properties props = 0);
    };

    ///this functions as a real unordered command queue
    struct multi_command_queue
    {
        std::vector<command_queue> queues;
        int which = 0;

        multi_command_queue(context& ctx, cl_command_queue_properties props, int queue_count);

        ///syncs cqueue with our queue
        ///kind of models fork join
        void begin_splice(cl::command_queue& cqueue);
        void end_splice(cl::command_queue& cqueue);

        command_queue& next();
    };

    ///uses buffer info to execute things out of order
    struct managed_command_queue
    {
        multi_command_queue mqueue;
        std::vector<std::tuple<cl::event, access_storage, std::string>> event_history;

        managed_command_queue(context& ctx, cl_command_queue_properties props, int queue_count);

        std::vector<cl::event> get_dependencies(cl::mem_object& obj);

        void begin_splice(cl::command_queue& cqueue);
        void end_splice(cl::command_queue& cqueue);

        void getting_value_depends_on(cl::mem_object& obj, const cl::event& evt);
        event exec(const std::string& kname, args& pack, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<event>& deps = {});

        void flush();
        void block();

        void cleanup_events();

        template<typename T>
        cl::event add(const T& func, cl::mem_object& obj, const std::vector<cl::event>& events)
        {
            cleanup_events();

            std::vector<cl::event> evts = get_dependencies(obj);

            evts.insert(evts.end(), events.begin(), events.end());

            cl::command_queue& exec_on = mqueue.next();

            cl::event next = func(exec_on, evts);

            cl::access_storage store;
            store.add(obj);

            event_history.push_back({next, store, "generic"});

            return next;
        }
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

        event acquire(command_queue& cqueue);
        event unacquire(command_queue& cqueue);

        event acquire(command_queue& cqueue, const std::vector<cl::event>& events);
        event unacquire(command_queue& cqueue, const std::vector<cl::event>& events);

        event acquire(managed_command_queue& cqueue, const std::vector<cl::event>& events = std::vector<cl::event>());
        event unacquire(managed_command_queue& cqueue, const std::vector<cl::event>& events = std::vector<cl::event>());
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
    event copy(cl::managed_command_queue& cqueue, cl::buffer& source, cl::buffer& dest, const std::vector<cl::event>& events = {});

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

    std::string get_extensions(context& ctx);
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
