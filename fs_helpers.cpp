#include "fs_helpers.hpp"

#include <fstream>
#include "clock.hpp"

#include <string>
#include <vector>
#include <sstream>
#include <fstream>

#ifdef __WIN32__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif // __WIN32__

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif // __EMSCRIPTEN__

#ifdef __EMSCRIPTEN__
EM_JS(void, syncer, (),
{
    FS.syncfs(false, function (err) {

    });
});
#endif // __EMSCRIPTEN__

void sync_writes()
{
    #ifdef __EMSCRIPTEN__
    syncer();
    #endif // __EMSCRIPTEN__
}

std::string file::read(const std::string& file)
{
    #ifndef __EMSCRIPTEN__
    FILE* f = fopen(file.c_str(), "rb");
    #else
    FILE* f = fopen(("web/" + file).c_str(), "rb");
    #endif

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string buffer;
    buffer.resize(fsize + 1);
    fread(&buffer[0], fsize, 1, f);
    fclose(f);

    return buffer;
}

void file::write(const std::string& file, const std::string& data)
{
    {
        #ifndef __EMSCRIPTEN__
        std::ofstream out(file, std::ios::binary);
        #else
        std::ofstream out("web/" + file, std::ios::binary);
        #endif
        out << data;
    }

    sync_writes();
}

void file::write_atomic(const std::string& in_file, const std::string& data)
{
    if(data.size() == 0)
        return;

    #ifndef __EMSCRIPTEN__
    std::string file = in_file;
    #else
    std::string file = "web/" + in_file;
    #endif

    std::string atomic_extension = ".atom";
    std::string atomic_file = file + atomic_extension;
    std::string backup_file = file + ".back";

    #ifdef __WIN32__
    HANDLE handle = CreateFile(atomic_file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    WriteFile(handle, &data[0], data.size(), nullptr, nullptr);
    FlushFileBuffers(handle);
    CloseHandle(handle);
    #else
    int fd = open(atomic_file.c_str(), O_CREAT | O_DIRECT | O_SYNC | O_TRUNC | O_WRONLY, 0777);

    int written = 0;

    while(written < data.size())
    {
        int rval = ::write(fd, &data[written], (int)data.size() - written);

        if(rval == -1)
        {
            close(fd);
            throw std::runtime_error("Errno in atomic write " + std::to_string(errno));
        }

        written += rval;
    }

    close(fd);

    #endif // __WIN32__

    sync_writes();

    if(!file::exists(file))
    {
        ::rename(atomic_file.c_str(), file.c_str());
        sync_writes();
        return;
    }

    steady_timer timer;

    bool write_success = false;
    bool any_errors = false;

    do
    {
        #ifdef __WIN32__
        bool err = ReplaceFileA(file.c_str(), atomic_file.c_str(), backup_file.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) == 0;
        #else
        bool err = ::rename(atomic_file.c_str(), file.c_str()) != 0;
        #endif // __WIN32__

        //bool err = ReplaceFileA(file.c_str(), atomic_file.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) == 0;

        if(!err)
        {
            write_success = true;
            break;
        }

        if(err)
        {
            #ifdef __WIN32__
            printf("atomic write error %lu ", GetLastError());
            #else
            printf("atomic write error %i %s\n", errno, file.c_str());
            #endif // __WIN32__

            any_errors = true;
        }

        sync_writes();
    }
    while(timer.get_elapsed_time_s() < 1);

    if(!write_success)
    {
        throw std::runtime_error("Explod in atomic write");
    }

    if(any_errors)
    {
        printf("atomic_write had errors but recovered");
    }
}

bool file::exists(const std::string& name)
{
    #ifndef __EMSCRIPTEN__
    std::ifstream f(name.c_str());
    #else
    std::ifstream f(("web/" + name).c_str());
    #endif
    return f.good();
}

void file::rename(const std::string& from, const std::string& to)
{
    #ifndef __EMSCRIPTEN__
    ::rename(from.c_str(), to.c_str());
    #else
    ::rename(("web/" + from).c_str(), ("web/" + to).c_str());
    #endif

    sync_writes(); //?
}

#ifdef __EMSCRIPTEN__
EM_JS(void, handle_mounting, (),
{
    FS.mkdir('/web');
    FS.mount(IDBFS, {}, "/web");

    Module.syncdone = 0;

    FS.syncfs(true, function (err) {
        Module.syncdone = 1;
    });
});

struct em_helper
{
    em_helper()
    {
        handle_mounting();

        printf("Mounted\n");

        while(emscripten_run_script_int("Module.syncdone") == 0)
        {
            emscripten_sleep(1000);
        }

        printf("Finished mounting\n");
    }
};

#endif

void file::init()
{
    #ifdef __EMSCRIPTEN__
    static em_helper help;
    #endif // __EMSCRIPTEN__
}
