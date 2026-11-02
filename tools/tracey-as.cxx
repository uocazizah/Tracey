#include <tracey/assembler.hxx>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{

/*
 * is_safe_output_path - Validate a user-supplied output path.
 *
 * p is the candidate path string.
 *
 * Rejects empty paths, paths containing embedded NUL bytes, and paths that
 * traverse upward ("..") so that the assembler cannot be tricked into
 * overwriting arbitrary files through a crafted argument.
 *
 * Return: true if the path is acceptable, false otherwise.
 */
bool is_safe_output_path(const std::string &p)
{
    if (p.empty())
        return false;
    if (p.find('\0') != std::string::npos)
        return false;
    std::filesystem::path path(p);
    for (const auto &part : path)
    {
        if (part == "..")
            return false;
    }
    return true;
}

} // namespace

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
    if (!is_safe_output_path(argv[2]))
    {
        std::cerr << "Error: unsafe output path\n";
        std::cerr << "\033[1;34mnote:\033[0m output path must not be empty or contain '..' components\n";
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
