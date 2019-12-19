#ifndef CLOCK_HPP_INCLUDED
#define CLOCK_HPP_INCLUDED

#include <chrono>

struct steady_timer
{
    double get_elapsed_time_s();
    double restart();

    std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();
};

#endif // CLOCK_HPP_INCLUDED
