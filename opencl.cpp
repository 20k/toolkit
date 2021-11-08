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

#ifdef _WIN32
#include <windows.h>
#else
#include <GL/glx.h>
#endif

#define CHECK(x) do{if(auto err = x; err != CL_SUCCESS) {printf("Got opencl error %i\n", err); throw std::runtime_error("Got error " + std::to_string(err));}}while(0)

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
void get_platform_ids(cl_platform_id* clSelectedPlatformID)
{
    char chBuffer[1024] = {};
    cl_uint num_platforms;
    std::vector<cl_platform_id> clPlatformIDs;
    cl_int ciErrNum;
    *clSelectedPlatformID = NULL;
    cl_uint i = 0;

    ciErrNum = clGetPlatformIDs(0, NULL, &num_platforms);

    if(ciErrNum != CL_SUCCESS)
    {
        throw std::runtime_error("Bad clGetPlatformIDs call " + std::to_string(ciErrNum));
    }
    else
    {
        if(num_platforms == 0)
        {
            throw std::runtime_error("No available platforms");
        }
        else
        {
            clPlatformIDs.resize(num_platforms);

            ciErrNum = clGetPlatformIDs(num_platforms, &clPlatformIDs[0], NULL);

            for(i = 0; i < num_platforms; i++)
            {
                ciErrNum = clGetPlatformInfo(clPlatformIDs[i], CL_PLATFORM_NAME, 1024, &chBuffer, NULL);

                if(ciErrNum == CL_SUCCESS)
                {
                    if(strstr(chBuffer, "NVIDIA") != NULL || strstr(chBuffer, "AMD") != NULL)// || strstr(chBuffer, "Intel") != NULL)
                    {
                        *clSelectedPlatformID = clPlatformIDs[i];
                    }
                }
            }

            if(*clSelectedPlatformID == NULL)
            {
                *clSelectedPlatformID = clPlatformIDs[num_platforms-1];
            }
        }
    }
}

static
cl_event* get_event_pointer(const std::vector<cl::event>& events)
{
    if(events.size() > 0)
        return (cl_event*)&events[0];

    return nullptr;
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
        throw std::runtime_error("Bad kernel");
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
        clSetKernelArg(native_kernel.data, i, pack.arg_list[i]->fetch_size(), pack.arg_list[i]->fetch_ptr());
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
    kernels = std::make_shared<std::vector<std::map<std::string, kernel, std::less<>>>>();

    cl_platform_id pid = {};
    get_platform_ids(&pid);

    cl_uint num_devices = 0;
    cl_device_id devices[100] = {};

    CHECK(clGetDeviceIDs(pid, CL_DEVICE_TYPE_GPU, 1, devices, &num_devices));

    selected_device = devices[0];

    #ifdef _WIN32
    cl_context_properties props[] =
    {
        CL_GL_CONTEXT_KHR, (cl_context_properties)wglGetCurrentContext(),
        CL_WGL_HDC_KHR, (cl_context_properties)wglGetCurrentDC(),
        CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
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

void cl::context::register_program(cl::program& p)
{
    p.ensure_built();

    programs.push_back(p);

    cl_uint num = 0;
    cl_int err = clCreateKernelsInProgram(p.native_program.data, 0, nullptr, &num);

    if(err != CL_SUCCESS)
    {
        std::cout << "Error creating program " << err << std::endl;
        throw std::runtime_error("Bad Program");
    }

    std::vector<cl_kernel> cl_kernels;
    cl_kernels.resize(num + 1);

    clCreateKernelsInProgram(p.native_program.data, num, &cl_kernels[0], nullptr);

    cl_kernels.resize(num);

    std::map<std::string, cl::kernel, std::less<>>& which = kernels->emplace_back();

    for(cl_kernel& k : cl_kernels)
    {
        cl::kernel k1(k);

        k1.name.resize(strlen(k1.name.c_str()));

        std::cout << "Registered " << k1.name << std::endl;

        which[k1.name] = k1;
    }
}

void cl::context::deregister_program(int idx)
{
    if(idx < 0 || idx >= (int)programs.size())
        throw std::runtime_error("idx < 0 || idx >= programs.size() in deregister_program for cl::context");

    assert(programs.size() == kernels->size());

    kernels->erase(kernels->begin() + idx);
    programs.erase(programs.begin() + idx);
}

cl::kernel cl::context::fetch_kernel(std::string_view name)
{
    for(auto& program_map : *kernels)
    {
        if(auto it = program_map.find(name); it != program_map.end())
        {
            return it->second;
        }
    }

    throw std::runtime_error("no such kernel in context");
}

cl::program::program(context& ctx, const std::string& data, bool is_file) : program(ctx, std::vector{data}, is_file)
{

}

cl::program::program(context& ctx, const std::vector<std::string>& data, bool is_file)
{
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

void debug_build_status(cl::program& prog)
{
    cl_build_status bstatus;
    clGetProgramBuildInfo(prog.native_program.data, prog.async->selected_device, CL_PROGRAM_BUILD_STATUS, sizeof(cl_build_status), &bstatus, nullptr);

    std::cout << "Build Status: " << bstatus << std::endl;

    assert(bstatus == CL_BUILD_ERROR);

    std::string log;
    size_t log_size;

    clGetProgramBuildInfo(prog.native_program.data, prog.async->selected_device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);

    log.resize(log_size + 1);

    clGetProgramBuildInfo(prog.native_program.data, prog.async->selected_device, CL_PROGRAM_BUILD_LOG, log.size(), &log[0], nullptr);

    std::cout << log << std::endl;

    throw std::runtime_error("Failed to build");
}

void cl::program::build(context& ctx, const std::string& options)
{
    async->selected_device = ctx.selected_device;
    std::string build_options = "-cl-no-signed-zeros -cl-single-precision-constant " + options;

    cl_program prog = native_program.data;
    cl_device_id selected_device = ctx.selected_device;

    async->thrd = std::thread([prog, selected_device, build_options]()
    {
        clBuildProgram(prog, 1, &selected_device, build_options.c_str(), nullptr, nullptr);
    });
}

void cl::program::ensure_built()
{
    async->thrd.join();

    cl_build_status status;
    clGetProgramBuildInfo(native_program.data, async->selected_device, CL_PROGRAM_BUILD_STATUS, sizeof(cl_build_status), &status, nullptr);

    if(status != CL_SUCCESS)
    {
        debug_build_status(*this);
    }
}

cl::buffer::buffer(cl::context& ctx)
{
    native_context = ctx.native_context;
}

void cl::buffer::alloc(int64_t bytes)
{
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

void cl::buffer::write(cl::command_queue& write_on, const char* ptr, int64_t bytes)
{
    assert(bytes <= alloc_size);

    cl_int val = clEnqueueWriteBuffer(write_on.native_command_queue.data, native_mem_object.data, CL_TRUE, 0, bytes, ptr, 0, nullptr, nullptr);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not write");
    }
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
    assert(bytes <= alloc_size);

    cl_int val = clEnqueueReadBuffer(read_on.native_command_queue.data, native_mem_object.data, CL_TRUE, 0, bytes, ptr, 0, nullptr, nullptr);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not read");
    }
}

cl::event cl::buffer::read_async(cl::command_queue& read_on, char* ptr, int64_t bytes, const std::vector<cl::event>& wait_on)
{
    assert(bytes <= alloc_size);

    std::vector<cl_event> evts;

    for(auto& i : wait_on)
    {
        evts.push_back(i.native_event.data);
    }

    cl_event* eptr = nullptr;

    if(evts.size() > 0)
        eptr = evts.data();

    cl::event evt;

    cl_int val = clEnqueueReadBuffer(read_on.native_command_queue.data, native_mem_object.data, CL_FALSE, 0, bytes, ptr, evts.size(), eptr, &evt.native_event.data);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not read_async");
    }

    return evt;
}

void cl::buffer::set_to_zero(cl::command_queue& write_on)
{
    static int zero = 0;

    cl_int val = clEnqueueFillBuffer(write_on.native_command_queue.data, native_mem_object.data, (const void*)&zero, 1, 0, alloc_size, 0, nullptr, nullptr);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not set_to_zero");
    }
}

cl::buffer cl::buffer::as_device_read_only()
{
    cl::buffer buf = *this;

    cl_buffer_region region;
    region.origin = 0;
    region.size = alloc_size;

    cl_int err = 0;
    cl_mem as_readable = clCreateSubBuffer(native_mem_object.data, CL_MEM_READ_ONLY | CL_MEM_HOST_NO_ACCESS, CL_BUFFER_CREATE_TYPE_REGION, &region, &err);

    assert(err == 0);

    buf.native_mem_object.consume(as_readable);

    return buf;
}

cl::buffer cl::buffer::as_device_write_only()
{
    cl::buffer buf = *this;

    cl_buffer_region region;
    region.origin = 0;
    region.size = alloc_size;

    cl_int err = 0;
    cl_mem as_readable = clCreateSubBuffer(native_mem_object.data, CL_MEM_WRITE_ONLY | CL_MEM_HOST_NO_ACCESS, CL_BUFFER_CREATE_TYPE_REGION, &region, &err);

    assert(err == 0);

    buf.native_mem_object.consume(as_readable);

    return buf;
}

cl::image::image(cl::context& ctx)
{
    native_context = ctx.native_context;
}

void cl::image::alloc_impl(int dims, const std::array<int64_t, 3>& _sizes, const cl_image_format& format)
{
    cl_image_desc desc = {0};
    desc.image_width = 1;
    desc.image_height = 1;
    desc.image_depth = 1;

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

cl::command_queue::command_queue(cl::context& ctx, cl_command_queue_properties props) : kernels(ctx.kernels)
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

    kernels = ctx.kernels;

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
    cl::event ret;

    CHECK(clEnqueueMarkerWithWaitList(native_command_queue.data, deps.size(), get_event_pointer(deps), &ret.native_event.data));

    return ret;
}

cl::event cl::command_queue::exec(cl::kernel& kern, const std::vector<int>& global_ws, const std::vector<int>& local_ws, const std::vector<event>& deps)
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
            int rem = g_ws[i] % l_ws[i];

            g_ws[i] -= rem;
            g_ws[i] += l_ws[i];
        }

        if(g_ws[i] == 0)
        {
            g_ws[i] += l_ws[i];
        }
    }

    static_assert(sizeof(cl::event) == sizeof(cl_event));

    cl_int err = CL_SUCCESS;

    #ifndef GPU_PROFILE
    if(deps.size() > 0)
        err = clEnqueueNDRangeKernel(native_command_queue.data, kern.native_kernel.data, dim, nullptr, g_ws, l_ws, deps.size(), (cl_event*)&deps[0], &ret.native_event.data);
    else
        err = clEnqueueNDRangeKernel(native_command_queue.data, kern.native_kernel.data, dim, nullptr, g_ws, l_ws, 0, nullptr, &ret.native_event.data);
    #else

    err = clEnqueueNDRangeKernel(native_command_queue.data, kern.native_kernel.data, dim, nullptr, g_ws, l_ws, 0, nullptr, &ret.native_event.data);

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

cl::event cl::command_queue::exec(const std::string& kname, cl::args& pack, const std::vector<int>& global_ws, const std::vector<int>& local_ws, const std::vector<event>& deps)
{
    assert(global_ws.size() == local_ws.size());

    for(auto& kerns : *kernels)
    {
        auto kernel_it = kerns.find(kname);

        if(kernel_it == kerns.end())
            continue;

        cl::kernel& kern = kernel_it->second;

        kern.set_args(pack);

        return exec(kern, global_ws, local_ws, deps);
    }

    throw std::runtime_error("Kernel not found in any program");
}

cl::event cl::command_queue::exec(const std::string& kname, cl::args& pack, const std::vector<int>& global_ws, const std::vector<int>& local_ws)
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
}

void cl::gl_rendertexture::create(int _w, int _h)
{
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

    native_mem_object.release();

    cl_int err;
    cl_mem cmem = clCreateFromGLTexture(native_context.data, CL_MEM_READ_WRITE, GL_TEXTURE_2D, 0, _texture_id, &err);

    if(err != CL_SUCCESS)
    {
        std::cout << "Failure in create from rendertexture " << err << std::endl;
        throw std::runtime_error("Failure in create_from rendertexture");
    }

    texture_id = _texture_id;
    native_mem_object.consume(cmem);

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
}

///unfortunately, this does not support -1 which would have been superhumanly useful
void cl::gl_rendertexture::create_from_texture_with_mipmaps(GLuint _texture_id, int mip_level)
{
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

cl::event cl::gl_rendertexture::acquire(cl::command_queue& cqueue, const std::vector<cl::event>& events)
{
    cl::event ret;

    if(acquired)
        return ret;

    acquired = true;

    clEnqueueAcquireGLObjects(cqueue.native_command_queue.data, 1, &native_mem_object.data, events.size(), get_event_pointer(events), &ret.native_event.data);

    return ret;
}

cl::event cl::gl_rendertexture::acquire(cl::command_queue& cqueue)
{
    return acquire(cqueue, {});
}

cl::event cl::gl_rendertexture::unacquire(cl::command_queue& cqueue, const std::vector<cl::event>& events)
{
    cl::event ret;

    if(!acquired)
        return ret;

    acquired = false;

    clEnqueueReleaseGLObjects(cqueue.native_command_queue.data, 1, &native_mem_object.data, events.size(), get_event_pointer(events), &ret.native_event.data);

    return ret;
}

cl::event cl::gl_rendertexture::unacquire(cl::command_queue& cqueue)
{
    return unacquire(cqueue, {});
}

void cl::copy(cl::command_queue& cqueue, cl::buffer& b1, cl::buffer& b2)
{
    size_t amount = std::min(b1.alloc_size, b2.alloc_size);

    cl_int err = clEnqueueCopyBuffer(cqueue.native_command_queue.data, b1.native_mem_object.data, b2.native_mem_object.data, 0, 0, amount, 0, nullptr, nullptr);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Could not copy buffers");
    }
}

bool cl::supports_extension(cl::context& ctx, const std::string& name)
{
    size_t arr_size = 0;
    cl_int err = clGetDeviceInfo(ctx.selected_device, CL_DEVICE_EXTENSIONS, 0, nullptr, &arr_size);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Error in clGetDeviceInfo");
    }

    if(arr_size == 0)
        return false;

    std::string extensions;
    extensions.resize(arr_size + 1);

    err = clGetDeviceInfo(ctx.selected_device, CL_DEVICE_EXTENSIONS, arr_size, &extensions[0], nullptr);

    if(err != CL_SUCCESS)
    {
        throw std::runtime_error("Error in clGetDeviceInfo");
    }

    return extensions.find(name) != std::string::npos;
}

#endif // NO_OPENCL
