#include "config.hpp"

#ifndef NO_OPENCL

#include <GL/glew.h>
#include "opencl.hpp"
#include <stdexcept>
#include <string.h>
#include <CL/cl_gl.h>
#include <GL/gl.h>
#include <fstream>
#include <iostream>
#include <assert.h>
#include <thread>
#include "clock.hpp"
#include <mutex>
#include <toolkit/fs_helpers.hpp>
#include <semaphore>

#ifdef _WIN32
#include <windows.h>
#else
#include <GL/glx.h>
#endif

#define CHECK(x) do{if(auto err = x; err != CL_SUCCESS) {printf("Got opencl error %i %s\n", err, #x); throw std::runtime_error("Got error " + std::to_string(err));}}while(0)

static
std::string read_file(const std::string& file)
{
    FILE *f = fopen(file.c_str(), "rb");
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string buffer;
    buffer.resize(fsize + 1);
    fread(&buffer[0], fsize, 1, f);
    fclose(f);

    return buffer;
}

static
bool file_exists(const std::string& file_name)
{
    std::ifstream file(file_name);
    return file.good();
}

static
std::string get_platform_name(cl_platform_id id)
{
    std::string val;

    size_t length = 0;

    CHECK(clGetPlatformInfo(id, CL_PLATFORM_NAME, 0, nullptr, &length));

    val.resize(length + 1);

    CHECK(clGetPlatformInfo(id, CL_PLATFORM_NAME, length, (void*)val.data(), NULL));
    return val;
}

static
cl_platform_id get_platform_ids()
{
    std::optional<cl_platform_id> ret;

    cl_uint num_platforms = 0;
    cl_int ciErrNum = clGetPlatformIDs(0, NULL, &num_platforms);

    if(ciErrNum != CL_SUCCESS)
        throw std::runtime_error("Bad clGetPlatformIDs call " + std::to_string(ciErrNum));

    if(num_platforms == 0)
        throw std::runtime_error("No available platforms");

    std::vector<cl_platform_id> clPlatformIDs;
    clPlatformIDs.resize(num_platforms);

    CHECK(clGetPlatformIDs(num_platforms, &clPlatformIDs[0], NULL));

    for(int i = 0; i < (int)num_platforms; i++)
    {
        std::string name = get_platform_name(clPlatformIDs[i]);

        if(name.contains("NVIDIA") || name.contains("AMD"))
            ret = clPlatformIDs[i];
    }

    if(!ret.has_value())
        ret = clPlatformIDs[num_platforms-1];

    if(!ret.has_value())
        throw std::runtime_error("Did not find platform");

    return ret.value();
}

namespace
{
std::vector<cl_event> to_raw_events(const std::vector<cl::event>& events)
{
    std::vector<cl_event> ret;

    for(const cl::event& e : events)
    {
        if(e.native_event.data == nullptr)
            continue;

        ret.push_back(e.native_event.data);
    }

    return ret;
}
}

void cl::event::block()
{
    if(native_event.data == nullptr)
        return;

    clWaitForEvents(1, &native_event.data);
}

bool cl::event::is_finished()
{
    if(native_event.data == nullptr)
        return true;

    cl_int status = 0;

    if(clGetEventInfo(native_event.data, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof(cl_int), (void*)&status, nullptr) != CL_SUCCESS)
        throw std::runtime_error("Bad event in clGetEventInfo in is_finished");

    return status == CL_COMPLETE;
}

void cl::event::set_completion_callback(void (CL_CALLBACK* pfn_notify)(cl_event event, cl_int event_command_status, void *user_data), void* userdata)
{
    clSetEventCallback(native_event.data, CL_COMPLETE, pfn_notify, userdata);
}

int count_arguments(cl_kernel k)
{
    cl_uint argc = 0;

    CHECK(clGetKernelInfo(k, CL_KERNEL_NUM_ARGS, sizeof(cl_uint), (void*)&argc, nullptr));

    return argc;
}

cl::kernel::kernel()
{

}

cl::kernel::kernel(cl::program& p, const std::string& kname)
{
    p.ensure_built();

    name = kname;

    cl_int err;

    cl_kernel ret = clCreateKernel(p.native_program.data, name.c_str(), &err);

    if(err != CL_SUCCESS)
    {
        std::cout << "Invalid Kernel Name " << kname << " err " << err << std::endl;
        throw std::runtime_error("Bad kernel " + kname);
    }
    else
    {
        native_kernel.data = ret;
    }

    argument_count = count_arguments(ret);
}

cl::kernel::kernel(cl_kernel k)
{
    native_kernel.data = k;

    size_t ret = 0;

    cl_int err = clGetKernelInfo(k, CL_KERNEL_FUNCTION_NAME, 0, nullptr, &ret);

    if(err != CL_SUCCESS)
    {
        std::cout << "Invalid kernel create from cl kernel, err " << err << std::endl;
        throw std::runtime_error("Bad kernel");
    }

    name.resize(ret + 1);

    clGetKernelInfo(k, CL_KERNEL_FUNCTION_NAME, name.size(), &name[0], nullptr);

    argument_count = count_arguments(k);
}

void cl::kernel::set_args(cl::args& pack)
{
    if((int)pack.arg_list.size() != argument_count)
        throw std::runtime_error("Called kernel " + name + " with wrong number of arguments");

    for(int i=0; i < (int)pack.arg_list.size(); i++)
    {
        pack.arg_list[i]->callback(native_kernel.data, i);
    }
}

cl_program cl::kernel::fetch_program()
{
    cl_program ret;

    clGetKernelInfo(native_kernel.data, CL_KERNEL_PROGRAM, sizeof(cl_program), &ret, nullptr);

    return ret;
}

cl::kernel cl::kernel::clone()
{
    cl_program prog = fetch_program();

    cl_int err = 0;

    cl_kernel kern = clCreateKernel(prog, name.c_str(), &err);

    if(err != CL_SUCCESS)
        throw std::runtime_error("Could not clone kernel " + name + " with error " + std::to_string(err));

    cl::kernel ret(kern);

    return ret;
}

cl::context::context()
{
    shared = std::make_shared<shared_kernel_info>();

    cl_platform_id pid = get_platform_ids();
    platform_name = get_platform_name(pid);

    cl_uint num_devices = 0;
    cl_device_id devices[100] = {};

    CHECK(clGetDeviceIDs(pid, CL_DEVICE_TYPE_GPU, 1, devices, &num_devices));

    selected_device = devices[0];

    if(cl::supports_extension(selected_device, "cl_khr_gl_sharing"))
    {
        #ifdef _WIN32
        cl_context_properties props[] =
        {
            CL_GL_CONTEXT_KHR, (cl_context_properties)wglGetCurrentContext(),
            CL_WGL_HDC_KHR, (cl_context_properties)wglGetCurrentDC(),
            CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
            0
        };
        #elif defined(__APPLE__)
        CGLContextObj cgl_context = CGLGetCurrentContext();
        CGLShareGroupObj cgl_share_group = CGLGetShareGroup(cgl_current_context);

        cl_context_properties properties[] = {
            CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
            (cl_context_properties) cgl_share_group,
            0
        };
        #else
        cl_context_properties props[] =
        {
            CL_GL_CONTEXT_KHR, (cl_context_properties)glXGetCurrentContext(),
            CL_GLX_DISPLAY_KHR, (cl_context_properties)glXGetCurrentDisplay(),
            CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
            0
        };
        #endif

        cl_int error = 0;

        cl_context ctx = clCreateContext(props, 1, &selected_device, nullptr, nullptr, &error);

        if(error != CL_SUCCESS)
            throw std::runtime_error("Failed to create context " + std::to_string(error));

        native_context.data = ctx;
    }
    else
    {
        cl_context_properties props[] =
        {
            CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
            0
        };

        cl_int error = 0;

        cl_context ctx = clCreateContext(props, 1, &selected_device, nullptr, nullptr, &error);

        if(error != CL_SUCCESS)
            throw std::runtime_error("Failed to create context " + std::to_string(error));

        native_context.data = ctx;
    }
}

void cl::context::register_program(cl::program& p)
{
    p.ensure_built();

    std::scoped_lock lock(shared->mut);

    std::map<std::string, cl::kernel, std::less<>>& which = shared->kernels.emplace_back();

    for(auto& [name, kern] : p.async->built_kernels)
    {
        which[name] = kern;
    }
}

void cl::context::deregister_program(int idx)
{
    std::scoped_lock lock(shared->mut);

    if(idx < 0 || idx >= (int)shared->kernels.size())
        throw std::runtime_error("idx < 0 || idx >= kernels->size() in deregister_program for cl::context");

    shared->kernels.erase(shared->kernels.begin() + idx);
}

void cl::context::register_kernel(const cl::kernel& kern, std::optional<std::string> name_override, bool can_overlap_existing)
{
    std::scoped_lock lock(shared->mut);

    std::string name = name_override.value_or(kern.name);

    if(!can_overlap_existing)
    {
        for(const auto& v : shared->kernels)
        {
            for(const auto& [check_name, kern] : v)
            {
                if(check_name == name)
                    throw std::runtime_error("Kernel with name " + check_name + " already exists");
            }
        }
    }

    std::map<std::string, cl::kernel, std::less<>>& which = shared->kernels.emplace_back();
    which[name] = kern;
}

void cl::context::register_kernel(std::shared_ptr<pending_kernel> pending, const std::string& name)
{
    std::scoped_lock lock(shared->mut);

    shared->pending_kernels.push_back({name, std::move(pending)});
}

cl::kernel cl::context::fetch_kernel(std::string_view name)
{
    std::scoped_lock lock(shared->mut);

    for(auto& v : shared->kernels)
    {
        for(auto& [key, val] : v)
        {
            if(key == name)
                return val;
        }
    }

    throw std::runtime_error("no such kernel in context");
}

void cl::context::remove_kernel(std::string_view name)
{
    std::scoped_lock lock(shared->mut);

    for(int i=0; i < (int)shared->kernels.size(); i++)
    {
        for(auto it = shared->kernels[i].begin(); it != shared->kernels[i].end();)
        {
            if(it->first == name)
                it = shared->kernels[i].erase(it);
            else
                it++;
        }
    }
}

cl::program::program(const context& ctx)
{
    selected_device = ctx.selected_device;
}

cl::program::program(const context& ctx, const std::string& data, bool is_file) : program(ctx, std::vector{data}, is_file)
{

}

cl::program::program(const context& ctx, const std::vector<std::string>& data, bool is_file)
{
    selected_device = ctx.selected_device;

    if(data.size() == 0)
        throw std::runtime_error("No Program Data (0 length data vector)");

    async = std::make_shared<async_context>();

    if(is_file)
    {
        for(const auto& i : data)
        {
            if(!file_exists(i))
                throw std::runtime_error("No such file " + i);
        }
    }

    std::vector<std::string> src;

    if(is_file)
    {
        for(const auto& i : data)
        {
            src.push_back(read_file(i));
        }
    }
    else
    {
        src = data;
    }

    std::vector<const char*> data_ptrs;

    for(const auto& i : src)
    {
        data_ptrs.push_back(i.c_str());
    }

    native_program.data = clCreateProgramWithSource(ctx.native_context.data, data.size(), &data_ptrs[0], nullptr, nullptr);
}

cl::program::program(const context& ctx, const std::string& binary_data, cl::program::binary_tag tag)
{
    selected_device = ctx.selected_device;

    assert(binary_data.size() > 0);

    async = std::make_shared<async_context>();

    size_t length = binary_data.size();
    const char* binary_data_ptr = binary_data.c_str();

    native_program.data = clCreateProgramWithBinary(ctx.native_context.data, 1, &selected_device, &length, (const unsigned char**)&binary_data_ptr, nullptr, nullptr);
}

std::string get_binary(const cl::base<cl_program, clRetainProgram, clReleaseProgram>& native_program)
{
    size_t sizes[1] = {0};
    clGetProgramInfo(native_program.data, CL_PROGRAM_BINARY_SIZES, 1 * sizeof(size_t), sizes, NULL);

    std::string binary;
    binary.resize(sizes[0]);

    char* binary_ptr = binary.data();

    clGetProgramInfo(native_program.data, CL_PROGRAM_BINARIES, 1 * sizeof(char*), &binary_ptr, NULL);

    return binary;
}

std::string cl::program::get_binary()
{
    ensure_built();

    return ::get_binary(native_program);
}

void debug_build_status(cl_program prog, cl_device_id selected_device)
{
    cl_build_status bstatus = CL_BUILD_ERROR;
    cl_int build_info_result = clGetProgramBuildInfo(prog, selected_device, CL_PROGRAM_BUILD_STATUS, sizeof(cl_build_status), &bstatus, nullptr);

    if(build_info_result != CL_SUCCESS)
    {
        std::cout << "Error in clGetProgramBuildInfo " << build_info_result << std::endl;
        return;
    }

    if(bstatus == CL_SUCCESS)
        return;

    std::cout << "Build Status: " << bstatus << std::endl;

    assert(bstatus == CL_BUILD_ERROR);

    std::string log;
    size_t log_size = 0;

    clGetProgramBuildInfo(prog, selected_device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);

    log.resize(log_size + 1);

    clGetProgramBuildInfo(prog, selected_device, CL_PROGRAM_BUILD_LOG, log.size(), &log[0], nullptr);

    std::cout << log << std::endl;

    throw std::runtime_error("Failed to build");
}

void debug_build_status(cl::program& prog)
{
    debug_build_status(prog.native_program.data, prog.selected_device);
}

struct async_setter
{
    std::shared_ptr<cl::program::async_context> async_ctx;

    async_setter(std::shared_ptr<cl::program::async_context> sett) : async_ctx(sett){}

    ~async_setter()
    {
        async_ctx->latch.count_down();
    }
};

static std::counting_semaphore semaphore(std::max(std::thread::hardware_concurrency(), 1u));

struct semaphore_manager
{
    semaphore_manager()
    {
        semaphore.acquire();
    }

    ~semaphore_manager()
    {
        semaphore.release();
    }
};

void cl::program::build(const context& ctx, const std::string& options)
{
    std::string build_options = "-cl-single-precision-constant " + options;

    auto prog = native_program;
    cl_device_id selected = selected_device;
    std::shared_ptr<async_context> async_ctx = async;
    bool cache_write = must_write_to_cache_when_built;
    std::string cache_name = name_in_cache;

    std::thread([prog, selected, build_options, async_ctx, options, cache_write, cache_name]()
    {
        async_setter sett(async_ctx);

        if(async_ctx->cancelled)
            return;

        cl_int build_err = 0;

        {
            ///serialise access to clbuildprogram
            semaphore_manager lock;

            if(async_ctx->cancelled)
                return;

            build_err = clBuildProgram(prog.data, 1, &selected, build_options.c_str(), nullptr, nullptr);
        }

        if(build_err != CL_SUCCESS && build_err != CL_BUILD_PROGRAM_FAILURE)
        {
            if(build_err == -66)
            {
                std::cout << "Failed to compile due to build options " << options << std::endl;
            }

            std::cout << "Error in clBuildProgram " << build_err << std::endl;
            throw std::runtime_error("Build Error " + std::to_string(build_err));
        }

        if(async_ctx->cancelled)
            return;

        debug_build_status(prog.data, selected);

        cl_uint num = 0;
        cl_int err = clCreateKernelsInProgram(prog.data, 0, nullptr, &num);

        if(err != CL_SUCCESS)
        {
            std::cout << "Error creating program " << err << std::endl;
            throw std::runtime_error("Bad Program");
        }

        if(async_ctx->cancelled)
            return;

        std::vector<cl_kernel> cl_kernels;
        cl_kernels.resize(num + 1);

        clCreateKernelsInProgram(prog.data, num, cl_kernels.data(), nullptr);

        cl_kernels.resize(num);

        if(num == 0)
        {
            printf("Warning, 0 kernels built\n");
        }

        std::map<std::string, cl::kernel>& which = async_ctx->built_kernels;

        for(cl_kernel k : cl_kernels)
        {
            cl::kernel k1(k);

            k1.name.resize(strlen(k1.name.c_str()));

            which[k1.name] = k1;
        }

        if(cache_write)
        {
            file::write("cache/" + cache_name, ::get_binary(prog), file::mode::BINARY);
        }
    }).detach();
}

void cl::program::ensure_built()
{
    async->latch.wait();
}

bool cl::program::is_built()
{
    return async->latch.try_wait();
}

void cl::program::cancel()
{
    async->cancelled = true;
}

namespace{
template<typename T>
void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}
}

cl::program cl::build_program_with_cache(const context& ctx, const std::vector<std::string>& data, bool is_file, const std::string& options, const std::vector<std::string>& extra_deps, const std::string& cache_name)
{
    assert(data.size() > 0);

    std::vector<std::string> file_data;

    if(is_file)
    {
        for(auto& i : data)
        {
            file_data.push_back(file::read(i, file::mode::BINARY));
        }
    }
    else
    {
        file_data = data;
    }

    std::vector<std::string> deps_file_data;

    for(const auto& name : extra_deps)
    {
        deps_file_data.push_back(file::read(name, file::mode::BINARY));
    }

    std::optional<cl::program> prog_opt;

    std::size_t hsh = 0;

    hash_combine(hsh, options);

    for(auto& i : file_data)
        hash_combine(hsh, i);

    for(const auto& file : deps_file_data)
        hash_combine(hsh, file);

    hash_combine(hsh, ctx.platform_name);

    auto d_name_as_vec = get_device_info(ctx.selected_device, CL_DEVICE_NAME);

    hash_combine(hsh, std::string(d_name_as_vec.begin(), d_name_as_vec.end()));

    file::mkdir("cache");

    std::string filename;

    if(is_file)
    {
        for(auto& i : data)
            filename += i + "_";
    }
    else
    {
        filename = "";
    }

    std::string name_in_cache = filename + std::to_string(hsh);

    if(cache_name != "")
    {
        name_in_cache = cache_name + "_" + std::to_string(hsh);
    }

    if(file::exists("cache/" + name_in_cache))
    {
        std::string bin = file::read("cache/" + name_in_cache, file::mode::BINARY);

        prog_opt.emplace(ctx, bin, cl::program::binary_tag{});
    }
    else
    {
        prog_opt.emplace(ctx, file_data, false);
        prog_opt.value().must_write_to_cache_when_built = true;
        prog_opt.value().name_in_cache = name_in_cache;
    }

    cl::program& t_program = prog_opt.value();

    try
    {
        t_program.build(ctx, options);
    }
    catch(...)
    {
        std::cout << "Error building " << filename << std::endl;
        throw;
    }

    return t_program;
}

cl_mem_flags cl::mem_object::get_flags()
{
    return cl::get_flags(*this);
}

std::optional<cl::mem_object> cl::mem_object::get_parent()
{
    return cl::get_parent(*this);
}

std::optional<cl::mem_object> cl::get_parent(const cl::mem_object& in)
{
    cl_mem ret;
    clGetMemObjectInfo(in.native_mem_object.data, CL_MEM_ASSOCIATED_MEMOBJECT, sizeof(cl_mem), &ret, nullptr);

    if(ret == nullptr)
        return std::nullopt;

    ///reference count status of clGetMemObjectInfo is unclear, so explicit increment
    cl::mem_object result;
    result.native_mem_object.borrow(ret);

    return result;
}

cl_mem_flags cl::get_flags(const cl::mem_object& in)
{
    cl_mem_flags ret = 0;
    clGetMemObjectInfo(in.native_mem_object.data, CL_MEM_FLAGS, sizeof(cl_mem_flags), &ret, nullptr);

    return ret;
}

/*bool cl::requires_memory_barrier(cl_mem in1, cl_mem in2)
{
    if(in1 == nullptr || in2 == nullptr)
        return false;

    std::optional<cl_mem> parent1 = cl::get_parent(in1);
    std::optional<cl_mem> parent2 = cl::get_parent(in2);

    cl_mem to_check1 = parent1.value_or(in1);
    cl_mem to_check2 = parent2.value_or(in2);

    if(to_check1 == to_check2)
        return requires_memory_barrier_raw(cl::get_flags(in1), cl::get_flags(in2));

    return false;
}*/

std::pair<cl::mem_object, cl_mem_flags> get_barrier_vars(const cl::mem_object& in);

void cl::access_storage::add(const cl::mem_object& in)
{
    auto vars = get_barrier_vars(in);

    store[vars.first].push_back(vars.second);
}

std::pair<cl::mem_object, cl_mem_flags> get_barrier_vars(const cl::mem_object& in)
{
    assert(in.native_mem_object.data);

    std::optional<cl::mem_object> parent = cl::get_parent(in);

    while(parent.has_value())
    {
        auto next_parent_opt = cl::get_parent(parent.value());

        if(next_parent_opt.has_value())
            parent = next_parent_opt;
        else
            break;
    }

    cl_mem_flags flags = cl::get_flags(in);

    return {parent.value_or(in), flags};
}

cl::buffer::buffer(cl::context& ctx)
{
    native_context = ctx.native_context;
}

void cl::buffer::alloc(int64_t bytes)
{
    assert(bytes >= 0);

    alloc_size = bytes;

    native_mem_object.release();

    cl_int err;
    cl_mem found = clCreateBuffer(native_context.data, CL_MEM_READ_WRITE, alloc_size, nullptr, &err);

    if(err != CL_SUCCESS)
    {
        std::cout << "Error allocating buffer" << std::endl;
        throw std::runtime_error("Could not allocate buffer");
    }

    native_mem_object.consume(found);
}

void cl::buffer::write(cl::command_queue& write_on, const char* ptr, int64_t bytes, int64_t offset)
{
    assert((bytes + offset) <= alloc_size);

    cl_int val = clEnqueueWriteBuffer(write_on.native_command_queue.data, native_mem_object.data, CL_TRUE, offset, bytes, ptr, 0, nullptr, nullptr);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not write");
    }
}

void cl::buffer::write(cl::command_queue& write_on, const char* ptr, int64_t bytes)
{
    write(write_on, ptr, bytes, 0);
}

void event_memory_free(cl_event event, cl_int event_command_status, void* user_data)
{
    if(event_command_status != CL_COMPLETE)
        return;

    delete [] (char*)user_data;
}

cl::event cl::buffer::write_async(cl::command_queue& write_on, const char* ptr, int64_t bytes)
{
    assert(bytes <= alloc_size);

    cl::event evt;

    char* nptr = new char[bytes];

    memcpy(nptr, ptr, bytes);

    cl_int val = clEnqueueWriteBuffer(write_on.native_command_queue.data, native_mem_object.data, CL_FALSE, 0, bytes, nptr, 0, nullptr, &evt.native_event.data);

    clSetEventCallback(evt.native_event.data, CL_COMPLETE, &event_memory_free, nptr);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not write");
    }

    return evt;
}

void cl::buffer::read(cl::command_queue& read_on, char* ptr, int64_t bytes)
{
    return read(read_on, ptr, bytes, 0);
}

void cl::buffer::read(cl::command_queue& read_on, char* ptr, int64_t bytes, int64_t offset)
{
    assert((bytes + offset) <= alloc_size);

    cl_int val = clEnqueueReadBuffer(read_on.native_command_queue.data, native_mem_object.data, CL_TRUE, offset, bytes, ptr, 0, nullptr, nullptr);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not read, with error " + std::to_string(val));
    }
}

cl::event cl::buffer::read_async(cl::command_queue& read_on, char* ptr, int64_t bytes, const std::vector<cl::event>& wait_on)
{
    assert(bytes <= alloc_size);

    std::vector<cl_event> evts = to_raw_events(wait_on);
;
    cl::event evt;

    cl_int val = clEnqueueReadBuffer(read_on.native_command_queue.data, native_mem_object.data, CL_FALSE, 0, bytes, ptr, evts.size(), evts.data(), &evt.native_event.data);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not read_async " + std::to_string(val));
    }

    return evt;
}

cl::event cl::buffer::set_to_zero(cl::command_queue& write_on)
{
    static int zero = 0;

    return fill(write_on, (const char*)&zero, 1, alloc_size);
}

cl::event cl::buffer::fill(cl::command_queue& write_on, const void* pattern, size_t pattern_size, size_t size, const std::vector<cl::event>& deps)
{
    cl::event evt;

    std::vector<cl_event> events = to_raw_events(deps);

    cl_int val = clEnqueueFillBuffer(write_on.native_command_queue.data, native_mem_object.data, pattern, pattern_size, 0, size, events.size(), events.data(), &evt.native_event.data);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not fill buffer");
    }

    return evt;
}

///I think it might be better to simply mark buffers
namespace
{
    cl::buffer as_props(cl::buffer& in, cl_mem_flags flags, cl_buffer_region region)
    {
        cl::buffer ret = in;

        assert(region.origin >= 0);
        assert(region.origin + region.size <= (size_t)in.alloc_size);

        cl_int err = 0;
        cl_mem as_subobject = clCreateSubBuffer(in.native_mem_object.data, flags, CL_BUFFER_CREATE_TYPE_REGION, &region, &err);

        if(err != 0)
            throw std::runtime_error("Bad object in clcreatesubbuffer " + std::to_string(err));

        assert(err == 0);

        ret.native_mem_object.consume(as_subobject);
        ret.alloc_size = region.size;

        return ret;
    }

    cl::buffer as_props_whole(cl::buffer& in, cl_mem_flags flags)
    {
        cl_buffer_region region;
        region.origin = 0;
        region.size = in.alloc_size;

        return as_props(in, flags, region);
    }
}

cl::buffer cl::buffer::as_read_only()
{
    return as_props_whole(*this, CL_MEM_READ_ONLY);
}

cl::buffer cl::buffer::as_write_only()
{
    return as_props_whole(*this, CL_MEM_WRITE_ONLY);
}

cl::buffer cl::buffer::as_device_read_only()
{
    return as_props_whole(*this, CL_MEM_READ_ONLY);
}

cl::buffer cl::buffer::as_device_write_only()
{
    return as_props_whole(*this, CL_MEM_WRITE_ONLY);
}

cl::buffer cl::buffer::as_device_inaccessible()
{
    cl::buffer buf = *this;
    buf.alloc_size = 0;
    buf.native_mem_object.release();

    return buf;
}

cl::buffer cl::buffer::slice(int64_t offset, int64_t length, cl_mem_flags flags)
{
    cl_buffer_region region;
    region.origin = offset;
    region.size = length;

    return as_props(*this, flags, region);
}

cl::image::image(cl::context& ctx)
{
    native_context = ctx.native_context;
}

void cl::image::alloc_impl(int dims, const std::array<int64_t, 3>& _sizes, const cl_image_format& format, cl::image_flags::type t)
{
    cl_image_desc desc = {0};
    desc.image_width = 1;
    desc.image_height = 1;
    desc.image_depth = 1;

    bool is_arr = t == cl::image_flags::ARRAY;

    if(!is_arr)
    {
        if(dims == 1)
        {
            desc.image_type = CL_MEM_OBJECT_IMAGE1D;
            desc.image_width = _sizes[0];
        }

        if(dims == 2)
        {
            desc.image_type = CL_MEM_OBJECT_IMAGE2D;
            desc.image_width = _sizes[0];
            desc.image_height = _sizes[1];
        }

        if(dims == 3)
        {
            desc.image_type = CL_MEM_OBJECT_IMAGE3D;
            desc.image_width = _sizes[0];
            desc.image_height = _sizes[1];
            desc.image_depth = _sizes[2];
        }
    }
    else
    {
        assert(dims != 1);
        assert(dims != 4); ///cannot have array of 3d objects

        if(dims == 2)
        {
            desc.image_type = CL_MEM_OBJECT_IMAGE1D_ARRAY;
            desc.image_width = _sizes[0];
            desc.image_array_size = _sizes[1];
        }

        if(dims == 3)
        {
            desc.image_type = CL_MEM_OBJECT_IMAGE2D_ARRAY;
            desc.image_width = _sizes[0];
            desc.image_height = _sizes[1];
            desc.image_array_size = _sizes[2];
        }
    }

    native_mem_object.release();

    cl_int err;
    cl_mem ret = clCreateImage(native_context.data, CL_MEM_READ_WRITE, &format, &desc, nullptr, &err);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Could not clCreateImage " + std::to_string(err));
    }

    dimensions = dims;
    sizes = _sizes;
    native_mem_object.consume(ret);
}

void cl::image_base::clear(cl::command_queue& cqueue)
{
    char everything_zero[sizeof(double) * 4] = {0};

    size_t origin[3] = {0,0,0};
    size_t regions[3] = {0,0,0};

    for(int i=0; i < 3; i++)
    {
        regions[i] = sizes[i];
    }

    cl_int ret = clEnqueueFillImage(cqueue.native_command_queue.data, native_mem_object.data, (const void*)everything_zero, origin, regions, 0, nullptr, nullptr);

    if(ret != CL_SUCCESS)
    {
        printf("Ret from clenqueuefillimage %i\n", ret);
    }
}

void cl::image_base::read_impl(cl::command_queue& cqueue, const vec<4, size_t>& origin, const vec<4, size_t>& region, char* out)
{
    cl_int err = clEnqueueReadImage(cqueue.native_command_queue.data, native_mem_object.data, CL_TRUE, &origin.v[0], &region.v[0], 0, 0, out, 0, nullptr, nullptr);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Could not read image");
    }
}

void cl::image::write_impl(command_queue& write_on, const char* ptr, const vec<3, size_t>& origin, const vec<3, size_t>& region)
{
    cl_int err = clEnqueueWriteImage(write_on.native_command_queue.data, native_mem_object.data, true, &origin.v[0], &region.v[0], 0, 0, ptr, 0, nullptr, nullptr);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Could not write to image " + std::to_string(err));
    }
}


cl::image_with_mipmaps::image_with_mipmaps(cl::context& ctx)
{
    native_context = ctx.native_context;
}

void cl::image_with_mipmaps::alloc_impl(int dims, const std::array<int64_t, 3>& _sizes, int _mip_levels, const cl_image_format& format)
{
    mip_levels = _mip_levels;

    cl_image_desc desc = {0};
    desc.image_width = 1;
    desc.image_height = 1;
    desc.image_depth = 1;
    desc.num_mip_levels = mip_levels;

    if(dims == 1)
    {
        desc.image_type = CL_MEM_OBJECT_IMAGE1D;
        desc.image_width = _sizes[0];
    }

    if(dims == 2)
    {
        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = _sizes[0];
        desc.image_height = _sizes[1];
    }

    if(dims == 3)
    {
        desc.image_type = CL_MEM_OBJECT_IMAGE3D;
        desc.image_width = _sizes[0];
        desc.image_height = _sizes[1];
        desc.image_depth = _sizes[2];
    }

    native_mem_object.release();

    cl_int err;
    cl_mem ret = clCreateImage(native_context.data, CL_MEM_READ_WRITE, &format, &desc, nullptr, &err);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Could not clCreateImage");
    }

    dimensions = dims;
    sizes = _sizes;
    native_mem_object.consume(ret);
}

void cl::image_with_mipmaps::write_impl(command_queue& write_on, const char* ptr, const vec<3, size_t>& origin, const vec<3, size_t>& region, int mip_level)
{
    vec<4, size_t> lorigin = {origin.x(), origin.y(), origin.z(), 1};

    lorigin.v[dimensions] = mip_level;

    cl_int err = clEnqueueWriteImage(write_on.native_command_queue.data, native_mem_object.data, true, &lorigin.v[0], &region.v[0], 0, 0, ptr, 0, nullptr, nullptr);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Could not write to image " + std::to_string(err));
    }
}

cl::command_queue::command_queue(cl::context& ctx, cl_command_queue_properties props) : shared(ctx.shared)
{
    cl_int err;

    #ifndef GPU_PROFILE
    cl_command_queue cqueue = clCreateCommandQueue(ctx.native_context.data, ctx.selected_device, props, &err);
    #else
    cl_command_queue cqueue = clCreateCommandQueue(ctx.native_context.data, ctx.selected_device, CL_QUEUE_PROFILING_ENABLE | props, &err);
    #endif

    if(err != CL_SUCCESS)
    {
        std::cout << "Error creating command queue " << err << std::endl;
        throw std::runtime_error("Could not make command queue");
    }

    native_command_queue.data = cqueue;

    native_context = ctx.native_context;
}

cl::command_queue::command_queue()
{

}

cl::device_command_queue::device_command_queue(cl::context& ctx, cl_command_queue_properties props)
{
    cl_int err;

    shared = ctx.shared;

    cl_queue_properties qprop[] = { CL_QUEUE_SIZE, 4096, CL_QUEUE_PROPERTIES,
     (cl_command_queue_properties)(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE |
                                   CL_QUEUE_ON_DEVICE |
                                   CL_QUEUE_ON_DEVICE_DEFAULT), 0 };

    cl_command_queue cqueue = clCreateCommandQueueWithProperties(ctx.native_context.data, ctx.selected_device, qprop, &err);

    if(err != CL_SUCCESS)
    {
        std::cout << "Error creating command queue " << err << std::endl;
        throw std::runtime_error("Could not make command queue");
    }

    native_command_queue.data = cqueue;
    native_context = ctx.native_context;
}

cl::event cl::command_queue::enqueue_marker(const std::vector<cl::event>& deps)
{
    std::vector<cl_event> events = to_raw_events(deps);

    cl::event ret;

    CHECK(clEnqueueMarkerWithWaitList(native_command_queue.data, events.size(), events.data(), &ret.native_event.data));

    return ret;
}

cl::event cl::command_queue::exec(cl::kernel& kern, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<event>& deps)
{
    cl::event ret;

    int dim = global_ws.size();

    size_t g_ws[3] = {0};
    size_t l_ws[3] = {0};

    for(int i=0; i < dim; i++)
    {
        l_ws[i] = local_ws[i];
        g_ws[i] = global_ws[i];

        if(l_ws[i] == 0)
            continue;

        if((g_ws[i] % l_ws[i]) != 0)
        {
            size_t rem = g_ws[i] % l_ws[i];

            g_ws[i] -= rem;
            g_ws[i] += l_ws[i];
        }

        if(g_ws[i] == 0)
        {
            g_ws[i] += l_ws[i];
        }
    }

    std::vector<cl_event> events = to_raw_events(deps);

    cl_int err = CL_SUCCESS;

    #ifndef GPU_PROFILE
    err = clEnqueueNDRangeKernel(native_command_queue.data, kern.native_kernel.data, dim, nullptr, g_ws, l_ws, events.size(), events.data(), &ret.native_event.data);
    #else

    err = clEnqueueNDRangeKernel(native_command_queue.data, kern.native_kernel.data, dim, nullptr, g_ws, l_ws, events.size(), events.data(), &ret.native_event.data);

    cl_ulong start;
    cl_ulong finish;

    block();

    clGetEventProfilingInfo(ret.native_event.data, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, nullptr);
    clGetEventProfilingInfo(ret.native_event.data, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &finish, nullptr);

    cl_ulong diff = finish - start;

    double ddiff = diff / 1000. / 1000.;

    std::cout << "kernel " << kern.name << " ms " << ddiff << std::endl;

    #endif // GPU_PROFILE

    if(err != CL_SUCCESS)
    {
        std::cout << "clEnqueueNDRangeKernel Error " << err << " for kernel " << kern.name << std::endl;
    }

    return ret;
}

bool cl::shared_kernel_info::promote_pending(const std::string& name)
{
    std::shared_ptr<pending_kernel> pend;

    {
        std::scoped_lock lock(mut);
        bool found = false;

        for(auto& [check_name, ptr] : pending_kernels)
        {
            if(check_name == name)
            {
                pend = ptr;
                found = true;
            }
        }

        if(!found)
            return false;
    }

    pend->latch.wait();

    {
        std::scoped_lock lock(mut);

        bool should_add = false;

        ///someone might have done the same thing in the meantime, remove us from pending_kernels but don't be too surprised if its not found

        for(auto it = pending_kernels.begin(); it != pending_kernels.end();)
        {
            if(it->second == pend)
            {
                ///only add us to the kernels list if we were the one that removed us from pending kernels
                should_add = true;
                break;
            }
            else
                it++;
        }

        ///race condition in that someone did this first, but still a valid sign for the caller to re-check
        if(!should_add)
            return true;

        std::map<std::string, kernel, std::less<>>& next = kernels.emplace_back();
        next[name] = pend->kernel.value();
    }

    return true;
}

cl::event cl::command_queue::exec(const std::string& kname, cl::args& pack, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws, const std::vector<event>& deps)
{
    assert(global_ws.size() == local_ws.size());

    {
        std::vector<std::map<std::string, kernel, std::less<>>> current;

        {
            std::scoped_lock lock(shared->mut);

            current = shared->kernels;
        }

        for(auto& kerns : current)
        {
            auto kernel_it = kerns.find(kname);

            if(kernel_it == kerns.end())
                continue;

            cl::kernel& kern = kernel_it->second;

            kern.set_args(pack);

            return exec(kern, global_ws, local_ws, deps);
        }
    }

    if(shared->promote_pending(kname))
    {
        return exec(kname, pack, global_ws, local_ws, deps);
    }

    throw std::runtime_error("Kernel " + kname + " not found in any program");
}

cl::event cl::command_queue::exec(const std::string& kname, cl::args& pack, const std::vector<size_t>& global_ws, const std::vector<size_t>& local_ws)
{
    std::vector<cl::event> evts;

    return exec(kname, pack, global_ws, local_ws, evts);
}

void cl::command_queue::block()
{
    clFinish(native_command_queue.data);
}

void cl::command_queue::flush()
{
    clFlush(native_command_queue.data);
}

cl::gl_rendertexture::gl_rendertexture(context& ctx)
{
    native_context = ctx.native_context;

    sharing_is_available = cl::supports_extension(ctx, "cl_khr_gl_sharing");
}

void cl::gl_rendertexture::create(int _w, int _h)
{
    assert(sharing_is_available);

    sizes[0] = _w;
    sizes[1] = _h;

    GLuint fbo;
    glGenFramebuffersEXT(1, &fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sizes[0], sizes[1], 0, GL_RGBA, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    native_mem_object.release();

    cl_int err;
    cl_mem cmem = clCreateFromGLTexture(native_context.data, CL_MEM_READ_WRITE, GL_TEXTURE_2D, 0, texture_id, &err);

    if(err != CL_SUCCESS)
    {
        std::cout << "Failure in create rendertexture " << err << std::endl;
        throw std::runtime_error("Failure in create rendertexture");
    }

    native_mem_object.consume(cmem);
}

void cl::gl_rendertexture::create_from_texture(GLuint _texture_id)
{
    ///Do I need this?
    glBindTexture(GL_TEXTURE_2D, _texture_id);

    int w, h, d;
    int miplevel = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, miplevel, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, miplevel, GL_TEXTURE_HEIGHT, &h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, miplevel, GL_TEXTURE_DEPTH, &d);

    sizes[0] = w;
    sizes[1] = h;

    if(d < 1)
        d = 1;

    sizes[2] = d;

    native_mem_object.release();

    if(sharing_is_available)
    {
        cl_int err;
        cl_mem cmem = clCreateFromGLTexture(native_context.data, CL_MEM_READ_WRITE, GL_TEXTURE_2D, 0, _texture_id, &err);

        if(err != CL_SUCCESS)
        {
            std::cout << "Failure in create from rendertexture " << err << std::endl;
            throw std::runtime_error("Failure in create_from rendertexture");
        }

        native_mem_object.consume(cmem);
    }
    else
    {
        cl_image_format format;
        format.image_channel_order = CL_RGBA;
        format.image_channel_data_type = CL_FLOAT;

        cl_image_desc desc = {};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = w;
        desc.image_height = h;
        desc.image_depth = d;
        desc.image_array_size = 1;
        desc.image_row_pitch = 0;
        desc.image_slice_pitch = 0;
        desc.num_mip_levels = 0;
        desc.num_samples = 0;
        desc.buffer = nullptr;

        cl_int err;
        cl_mem cmem = clCreateImage(native_context.data, CL_MEM_READ_WRITE, &format, &desc, nullptr, &err);

        if(err != CL_SUCCESS)
        {
            std::cout << "Failure in create from rendertexture " << err << std::endl;
            throw std::runtime_error("Failure in create_from rendertexture");
        }

        native_mem_object.consume(cmem);
    }

    texture_id = _texture_id;
}

///unfortunately, this does not support -1 which would have been superhumanly useful
void cl::gl_rendertexture::create_from_texture_with_mipmaps(GLuint _texture_id, int mip_level)
{
    assert(sharing_is_available);

    ///Do I need this?
    glBindTexture(GL_TEXTURE_2D, _texture_id);

    native_mem_object.release();

    cl_int err;
    cl_mem cmem = clCreateFromGLTexture(native_context.data, CL_MEM_READ_WRITE, GL_TEXTURE_2D, mip_level, _texture_id, &err);

    if(err != CL_SUCCESS)
    {
        std::cout << "Failure in create from rendertexture " << err << std::endl;
        throw std::runtime_error("Failure in create_from rendertexture");
    }

    texture_id = _texture_id;
    native_mem_object.consume(cmem);

    int w, h, d;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, mip_level, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, mip_level, GL_TEXTURE_HEIGHT, &h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, mip_level, GL_TEXTURE_DEPTH, &d);

    sizes[0] = w;
    sizes[1] = h;

    if(d < 1)
        d = 1;

    sizes[2] = d;
}

void cl::gl_rendertexture::create_from_framebuffer(GLuint _framebuffer_id)
{
    /*glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER, _framebuffer_id);

    GLint params = 0;

    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_FRONT_LEFT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &params);

    std::cout << "type? " << params << std::endl;

    assert(params == GL_TEXTURE);

    GLint out = 0;

    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_FRONT_LEFT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &out);

    texture_id = out;
    */

    ///can't be done
    /*if(_framebuffer_id == 0)
    {
        cl_int err;
        cl_mem mem = clCreateFromGLRenderbuffer(native_context.data, CL_MEM_READ_WRITE, 0, &err);

        std::cout << "err? " << err << std::endl;
    }*/
}

cl::event cl::gl_rendertexture::acquire(cl::command_queue& cqueue, const std::vector<cl::event>& deps)
{
    cl::event ret;

    if(acquired)
        return ret;

    std::vector<cl_event> events = to_raw_events(deps);

    acquired = true;

    if(sharing_is_available)
        clEnqueueAcquireGLObjects(cqueue.native_command_queue.data, 1, &native_mem_object.data, events.size(), events.data(), &ret.native_event.data);

    return ret;
}

cl::event cl::gl_rendertexture::acquire(cl::command_queue& cqueue)
{
    return acquire(cqueue, {});
}

cl::event cl::gl_rendertexture::unacquire(cl::command_queue& cqueue, const std::vector<cl::event>& deps)
{
    cl::event ret;

    if(!acquired)
        return ret;

    std::vector<cl_event> events = to_raw_events(deps);

    acquired = false;

    if(sharing_is_available)
    {
        clEnqueueReleaseGLObjects(cqueue.native_command_queue.data, 1, &native_mem_object.data, events.size(), events.data(), &ret.native_event.data);
    }
    else
    {
        std::vector<cl_event> raw_events = to_raw_events(deps);

        if(raw_events.size() > 0)
            clWaitForEvents(raw_events.size(), raw_events.data());

        std::vector<cl_float4> data = this->read<2, cl_float4>(cqueue, {0,0}, {sizes[0], sizes[1]});

        glBindTexture(GL_TEXTURE_2D, texture_id);

        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sizes[0], sizes[1], GL_RGBA, GL_FLOAT, (void*)data.data());
    }

    return ret;
}

cl::event cl::gl_rendertexture::unacquire(cl::command_queue& cqueue)
{
    return unacquire(cqueue, {});
}

cl::event cl::copy(cl::command_queue& cqueue, cl::buffer& source, cl::buffer& dest, const std::vector<cl::event>& events)
{
    cl::event evt;

    assert(source.alloc_size == dest.alloc_size);

    size_t amount = std::min(source.alloc_size, dest.alloc_size);

    std::vector<cl_event> raw_events = to_raw_events(events);

    cl_int err = clEnqueueCopyBuffer(cqueue.native_command_queue.data, source.native_mem_object.data, dest.native_mem_object.data, 0, 0, amount, raw_events.size(), raw_events.data(), &evt.native_event.data);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Could not copy buffers");
    }

    return evt;
}

std::string cl::get_extensions(context& ctx)
{
    size_t arr_size = 0;
    cl_int err = clGetDeviceInfo(ctx.selected_device, CL_DEVICE_EXTENSIONS, 0, nullptr, &arr_size);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Error in clGetDeviceInfo");
    }

    if(arr_size == 0)
        return "";

    std::string extensions;
    extensions.resize(arr_size + 1);

    err = clGetDeviceInfo(ctx.selected_device, CL_DEVICE_EXTENSIONS, arr_size, extensions.data(), nullptr);

    return extensions;
}

bool cl::supports_extension(cl_device_id device_id, const std::string& name)
{
    size_t arr_size = 0;
    cl_int err = clGetDeviceInfo(device_id, CL_DEVICE_EXTENSIONS, 0, nullptr, &arr_size);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Error in clGetDeviceInfo");
    }

    if(arr_size == 0)
        return false;

    std::string extensions;
    extensions.resize(arr_size + 1);

    err = clGetDeviceInfo(device_id, CL_DEVICE_EXTENSIONS, arr_size, &extensions[0], nullptr);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Error in clGetDeviceInfo");
    }

    return extensions.find(name) != std::string::npos;
}

bool cl::supports_extension(cl::context& ctx, const std::string& name)
{
   return supports_extension(ctx.selected_device, name);
}

std::vector<char> cl::get_device_info(cl_device_id id, cl_device_info param)
{
    std::vector<char> ret;

    size_t size = 0;

    cl_int err = clGetDeviceInfo(id, param, 0, nullptr, &size);

    if(err != CL_SUCCESS)
        throw std::runtime_error("Error " + std::to_string(err) + " in get_device_info");

    if(size == 0)
        return ret;

    ret.resize(size);

    err = clGetDeviceInfo(id, param, ret.size(), ret.data(), nullptr);

    if(err != CL_SUCCESS)
        throw std::runtime_error("Error " + std::to_string(err) + " in get_device_info 2");

    return ret;
}

#endif // NO_OPENCL
