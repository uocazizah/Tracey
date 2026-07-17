#include <tracey/file/file.hxx>
#include <utility>

tracey::file::TraceyFile::TraceyFile(TraceyFile&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
    , path_(std::move(other.path_))
    , buffer_(std::move(other.buffer_))
{}
