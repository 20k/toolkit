#ifndef FS_HELPERS_HPP_INCLUDED
#define FS_HELPERS_HPP_INCLUDED

#include <string>

///a directory should be without prefixes, eg a/hello.txt
namespace file
{
    namespace mode
    {
        enum type
        {
            BINARY,
            TEXT
        };
    }

    ///for emscripten performance reasons
    struct manual_fs_sync
    {
        manual_fs_sync();
        ~manual_fs_sync();
    };

    std::string read(const std::string& file, mode::type m);
    void write(const std::string& file, const std::string& data, mode::type m);
    void write_atomic(const std::string& file, const std::string& data, mode::type m);
    bool exists(const std::string& name);
    void rename(const std::string& from, const std::string& to);
    bool remove(const std::string& name);

    void mkdir(const std::string& name);

    namespace memfs
    {
        std::string read(const std::string& file, mode::type m);
        bool exists(const std::string& name);
        void mkdir(const std::string& name);
    }

    #ifdef __EMSCRIPTEN__
    // EMSCRIPTEN ONLY OBVIOUSLY
    void download(const std::string& name, const std::string& data);
    #endif // __EMSCRIPTEN__

    void init();
}

#endif // FS_HELPERS_HPP_INCLUDED
