#include <tracey/file/file.hxx>

tracey::file::TraceyFile::~TraceyFile() {
    close();
}
