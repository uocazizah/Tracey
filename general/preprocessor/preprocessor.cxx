#include <tracey/preprocessor.hxx>

#include <fstream>
#include <iostream>
#include <print>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace tracey
{

static std::string preprocess_recursive(const std::string &content,
                                        const std::string &base_dir,
                                        const std::vector<std::string> &incs,
                                        std::set<std::string> &visited,
                                        bool &had_error)
{
    std::istringstream in(content);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line))
    {
        std::string t = line;
        // trim leading whitespace
        size_t a = t.find_first_not_of(" \t");
        if (a != std::string::npos)
            t = t.substr(a);
        if (t.rfind("#include", 0) == 0)
        {
            size_t lt = t.find('<');
            size_t gt = t.find('>');
            size_t q1 = t.find('"');
            size_t q2 = (q1 != std::string::npos) ? t.find('"', q1 + 1) : std::string::npos;
            std::string incname;
            bool is_system = false;
            if (lt != std::string::npos && gt != std::string::npos)
            {
                incname = t.substr(lt + 1, gt - lt - 1);
                is_system = true;
            }
            else if (q1 != std::string::npos && q2 != std::string::npos)
            {
                incname = t.substr(q1 + 1, q2 - q1 - 1);
            }
            if (!incname.empty())
            {
                std::vector<std::string> search;
                if (!is_system)
                    search.push_back(base_dir);
                for (auto &p : incs)
                    search.push_back(p);
                if (is_system)
                {
                    search.push_back("lib");
                    search.push_back("include");
                }
                std::string found;
                for (auto &d : search)
                {
                    std::filesystem::path cand = std::filesystem::path(d) / incname;
                    if (std::filesystem::exists(cand))
                    {
                        found = cand.string();
                        break;
                    }
                }
                if (found.empty())
                {
                    std::print(stderr, "Error: cannot find included file '{}'\n", incname);
                    std::print(stderr, "\033[1;34mnote:\033[0m verify the include search paths and file name\n");
                    had_error = true;
                    continue;
                }
                if (visited.count(found))
                    continue; // include guard
                visited.insert(found);
                std::ifstream f(found);
                std::ostringstream ss;
                ss << f.rdbuf();
                std::string incdir = std::filesystem::path(found).parent_path().string();
                out << preprocess_recursive(ss.str(), incdir, incs, visited, had_error) << "\n";
                continue;
            }
        }
        out << line << "\n";
    }
    return out.str();
}

preprocess_result preprocess(std::string_view source,
                             const preprocessor_config &config)
{
    preprocess_result result;
    std::set<std::string> visited;
    bool had_error = false;
    result.content = preprocess_recursive(std::string(source),
                                          config.base_dir.string(),
                                          config.include_paths,
                                          visited,
                                          had_error);
    result.had_error = had_error;
    return result;
}

} // namespace tracey