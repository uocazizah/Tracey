#include <tracey/linker.hxx>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "Error: usage: tracey-ld <out.elf> <obj1.o> [obj2.o...]\n";
        std::cerr << "\033[1;34mnote:\033[0m provide an output executable and at least one object file\n";
        return 1;
    }
    std::vector<std::string> objs;
    for (int i = 2; i < argc; i++)
        objs.push_back(argv[i]);
    if (!tracey::link_elf_aarch64(objs, argv[1]))
    {
        std::cerr << "Error: linking failed\n";
        std::cerr << "\033[1;34mnote:\033[0m verify object files, entry point, and unresolved symbols\n";
        return 1;
    }
    return 0;
}
