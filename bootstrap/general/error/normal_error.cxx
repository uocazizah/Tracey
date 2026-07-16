#include <iostream>
#include <cstdlib>

#define TRACEYC_NAME   "\033[1mtraceyc\033[0m"

#define COLOR_FATAL    "\033[1;31m"
#define COLOR_ERROR    "\033[31m"
#define COLOR_WARNING  "\033[35m"
#define COLOR_RESET    "\033[0m"

// Cetak pesan error: semua argumen dicetak berurutan ke stderr
template <typename... Args>
void error(const Args&... args)
{
    std::cerr << TRACEYC_NAME << ": "
              << COLOR_ERROR << "error" << COLOR_RESET << ": ";
    ((std::cerr << args << ' '), ...);   // fold expression (C++17)
    std::cerr << '\n';
}

template <typename... Args>
void warning(const Args&... args)
{
    std::cerr << TRACEYC_NAME << ": "
              << COLOR_WARNING << "warning" << COLOR_RESET << ": ";
    ((std::cerr << args << ' '), ...);
    std::cerr << '\n';
}

template <typename... Args>
[[noreturn]] void fatal(const Args&... args)
{
    std::cerr << TRACEYC_NAME << ": "
              << COLOR_FATAL << "fatal" << COLOR_RESET << ": ";
    ((std::cerr << args << ' '), ...);
    std::cerr << '\n';
    std::exit(EXIT_FAILURE);
}
