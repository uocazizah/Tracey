#ifndef ARGUMENT_HPP
#define ARGUMENT_HPP

class TraceyOptions
{
public:
    TraceyOptions() noexcept
        : p_input_file(nullptr),
          p_output_file(nullptr),
          warning_all(false),
          warning_extra(false)
    {
    }

    int parse_args(int argc, char **argv);

    void debug_print_args() const;

    const char *get_input_file() const noexcept  { return p_input_file; }
    const char *get_output_file() const noexcept { return p_output_file; }
    bool get_warning_all() const noexcept        { return warning_all; }
    bool get_warning_extra() const noexcept      { return warning_extra; }

private:
    const char *p_input_file;
    const char *p_output_file;
    bool warning_all;
    bool warning_extra;
};

#endif /* ARGUMENT_HXX */
