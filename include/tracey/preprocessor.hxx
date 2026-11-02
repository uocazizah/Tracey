#ifndef TRACEY_PREPROCESSOR_HXX
#define TRACEY_PREPROCESSOR_HXX

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tracey
{

struct preprocess_result
{
    std::string content;
    bool had_error{false};
};

struct preprocessor_config
{
    std::vector<std::string> include_paths;
    std::filesystem::path base_dir;
};

preprocess_result preprocess(std::string_view source,
                             const preprocessor_config &config);

} // namespace tracey

#endif /* TRACEY_PREPROCESSOR_HXX */