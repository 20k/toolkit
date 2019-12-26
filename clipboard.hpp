#ifndef CLIPBOARD_HPP_INCLUDED
#define CLIPBOARD_HPP_INCLUDED

#include <string>

namespace clipboard
{
    void set(const std::string& str);
    std::string get();
}

#endif // CLIPBOARD_HPP_INCLUDED
