#ifndef FS_HELPERS_HPP_INCLUDED
#define FS_HELPERS_HPP_INCLUDED

#include <string>

///a directory should be without prefixes, eg a/hello.txt
namespace file
{
    std::string read(const std::string& file);
    void write(const std::string& file, const std::string& data);
    void write_atomic(const std::string& file, const std::string& data);
    bool exists(const std::string& name);
    void rename(const std::string& from, const std::string& to);
}

#endif // FS_HELPERS_HPP_INCLUDED
