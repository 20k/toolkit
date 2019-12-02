#include "opencl.hpp"
#include <stdexcept>
#include <string.h>
#include <cl/cl_gl.h>
#include <gl/gl.h>
#include <fstream>
#include <iostream>
#include <assert.h>

#define CHECK(x) do{if(auto err = x; err != CL_SUCCESS) {throw std::runtime_error("Got error " + std::to_string(err));}}while(0)

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

                        //printf("Picked Platform: %s\n", chBuffer);
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

cl::kernel::kernel()
{

}

cl::kernel::kernel(cl::program& p, const std::string& kname)
{
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
}

cl::context::context()
{
    cl_platform_id pid = {};
    get_platform_ids(&pid);

    cl_uint num_devices = 0;
    cl_device_id devices[100] = {};

    CHECK(clGetDeviceIDs(pid, CL_DEVICE_TYPE_GPU, 1, devices, &num_devices));

    selected_device = devices[0];

    cl_context_properties props[] =
    {
        CL_GL_CONTEXT_KHR, (cl_context_properties)wglGetCurrentContext(),
        CL_WGL_HDC_KHR, (cl_context_properties)wglGetCurrentDC(),
        CL_CONTEXT_PLATFORM, (cl_context_properties)pid,
        0
    };

    cl_int error = 0;

    cl_context ctx = clCreateContext(props, 1, &selected_device, nullptr, nullptr, &error);

    if(error != CL_SUCCESS)
        throw std::runtime_error("Failed to create context " + std::to_string(error));

    native_context.data = ctx;
}

void cl::context::register_program(cl::program& p)
{
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

    for(cl_kernel& k : cl_kernels)
    {
        cl::kernel k1(k);

        k1.name.resize(strlen(k1.name.c_str()));

        std::cout << "Registered " << k1.name << std::endl;

        kernels[k1.name] = k1;
    }
}

cl::program::program(context& ctx, const std::string& data, bool is_file)
{
    if(is_file && !file_exists(data))
    {
        throw std::runtime_error("No such file " + data);
    }

    std::string src;

    if(is_file)
        src = read_file(data);
    else
        src = data;

    size_t len = src.size();
    const char* ptr = src.c_str();

    native_program.data = clCreateProgramWithSource(ctx.native_context.data, 1, &ptr, &len, nullptr);
}

void cl::program::build(context& ctx, const std::string& options)
{
    std::string build_options = "-cl-fast-relaxed-math -cl-no-signed-zeros -cl-single-precision-constant -cl-denorms-are-zero " + options;

    cl_int build_status = clBuildProgram(native_program.data, 1, &ctx.selected_device, build_options.c_str(), nullptr, nullptr);

    if(build_status != CL_SUCCESS)
    {
        std::cout << "Build Error" << std::endl;

        cl_build_status bstatus;
        clGetProgramBuildInfo(native_program.data, ctx.selected_device, CL_PROGRAM_BUILD_STATUS, sizeof(cl_build_status), &bstatus, nullptr);

        std::cout << "Build Status: " << bstatus << std::endl;

        assert(bstatus == CL_BUILD_ERROR);

        std::string log;
        size_t log_size;

        clGetProgramBuildInfo(native_program.data, ctx.selected_device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);

        log.resize(log_size + 1);

        clGetProgramBuildInfo(native_program.data, ctx.selected_device, CL_PROGRAM_BUILD_LOG, log.size(), &log[0], nullptr);

        std::cout << log << std::endl;

        throw std::runtime_error("Failed to build");
    }
}

cl::command_queue::command_queue(cl::context& ctx, cl_command_queue_properties props)
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

cl::buffer::buffer(cl::context& ctx)
{
    native_context = ctx.native_context;
}

void cl::buffer::alloc(int64_t bytes)
{
    alloc_size = bytes;

    cl_int err;
    cl_mem found = clCreateBuffer(native_context.data, CL_MEM_READ_WRITE, alloc_size, nullptr, &err);

    if(err != CL_SUCCESS)
    {
        std::cout << "Error allocating buffer" << std::endl;
        throw std::runtime_error("Could not allocate buffer");
    }

    native_mem_object.data = found;
}

void cl::buffer::write(cl::command_queue& write_on, const char* ptr, int64_t bytes)
{
    cl_int val = clEnqueueWriteBuffer(write_on.native_command_queue.data, native_mem_object.data, CL_TRUE, 0, alloc_size, ptr, 0, nullptr, nullptr);

    if(val != CL_SUCCESS)
    {
        throw std::runtime_error("Could not write");
    }
}
