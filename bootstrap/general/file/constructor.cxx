#include <tracey/file/file.hxx>

tracey::file::TraceyFile::TraceyFile() noexcept
    : handle_(nullptr)
    , path_()
    , buffer_()
{}
