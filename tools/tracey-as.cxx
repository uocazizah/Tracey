#include <tracey/assembler.hxx>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: tracey-as input.s output.o\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    std::ostringstream ss;
    ss << in.rdbuf();
    auto r = tracey::assemble_aarch64(ss.str());
    if (!r.ok)
    {
        std::cerr << "assemble error: " << r.error << "\n";
        return 1;
    }
    if (!tracey::write_elf_object(argv[2], r))
    {
        std::cerr << "write obj failed\n";
        return 1;
    }
    return 0;
}
