#include <tracey/assembler.hxx>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "Error: usage: tracey-as <input.s> <output.o>\n";
        std::cerr << "\033[1;34mnote:\033[0m provide an assembly input and output object path\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    std::ostringstream ss;
    ss << in.rdbuf();
    auto r = tracey::assemble_aarch64(ss.str());
    if (!r.ok)
    {
        std::cerr << "Error: assembly failed: " << r.error << "\n";
        std::cerr << "\033[1;34mnote:\033[0m inspect the reported instruction and operands\n";
        return 1;
    }
    if (!tracey::write_elf_object(argv[2], r))
    {
        std::cerr << "Error: failed to write ELF object file\n";
        std::cerr << "\033[1;34mnote:\033[0m check the output path and permissions\n";
        return 1;
    }
    return 0;
}
