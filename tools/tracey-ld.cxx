#include <tracey/linker.hxx>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: tracey-ld out.elf obj1.o [obj2.o...]\n";
        return 1;
    }
    std::vector<std::string> objs;
    for (int i = 2; i < argc; i++)
        objs.push_back(argv[i]);
    if (!tracey::link_elf_aarch64(objs, argv[1]))
    {
        std::cerr << "link failed\n";
        return 1;
    }
    return 0;
}
