#include "clock.hpp"

double steady_timer::get_elapsed_time_s()
{
    auto now = std::chrono::steady_clock::now();

    return std::chrono::duration<double>(now - start).count();
}

double steady_timer::restart()
{
    auto now = std::chrono::steady_clock::now();

    double result = std::chrono::duration<double>(now - start).count();

    start = now;

    return result;
}
