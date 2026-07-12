#ifndef ARGUMENT_HXX
#define ARGUMENT_HXX

namespace tracey::argument {

class argument_option
{
public:
    argument_option() noexcept;

    int parse_args(int argc, char **argv);

    void debug_print_args() const;

    const char *get_input_file() const noexcept;
    const char *get_output_file() const noexcept;
    bool get_warning_all() const noexcept;
    bool get_warning_extra() const noexcept;

private:
    const char *p_input_file;
    const char *p_output_file;
    bool warning_all;
    bool warning_extra;
};

} // namespace tracey::argument

#endif /* ARGUMENT_HXX */
