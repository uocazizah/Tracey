/*
 * Source file loading unit tests.
 *
 * The tests use real temporary files under /tmp because tracey_source_read
 * performs open/fstat based validation on the POSIX path. Each temporary
 * file is created with mkstemp so parallel test runs never collide, and is
 * unlinked before the case returns. Successful reads return owned path and
 * content buffers that are always released with tracey_source_free.
 */

#include "test_harness.h"

#include <tracey/source.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * make_temp_file - create a temporary regular file with the given contents.
 *
 * content is a NUL-terminated string written verbatim; its length excludes
 * the terminator. path must point to a writable buffer of at least
 * path_size bytes and receives the NUL-terminated mkstemp path on success.
 *
 * Returns true when the file was created, written and closed successfully.
 * On failure any partially created file is removed and false is returned.
 */
static bool make_temp_file(const char *content, char *path, size_t path_size)
{
    static const char pattern[] = "/tmp/tracey_source_XXXXXX";
    int fd;
    FILE *fp;
    size_t len = strlen(content);

    if (path_size < sizeof(pattern))
        return false;
    memcpy(path, pattern, sizeof(pattern));

    fd = mkstemp(path);
    if (fd < 0)
        return false;

    fp = fdopen(fd, "wb");
    if (fp == NULL)
    {
        /*
         * fdopen did not take ownership of fd on failure, so the
         * descriptor is closed explicitly before removing the file.
         */
        close(fd);
        unlink(path);
        return false;
    }

    if (len > 0 && fwrite(content, 1, len, fp) != len)
    {
        fclose(fp);
        unlink(path);
        return false;
    }

    if (fclose(fp) != 0)
    {
        unlink(path);
        return false;
    }

    return true;
}

/* Retained from the original placeholder test to keep its coverage. */
TEST_CASE(source_stub)
{
    CHECK(true);
}

TEST_CASE(source_reads_regular_file)
{
    static const char content[] = "int main() { return 0; }\n";
    char path[64];
    struct tracey_source source;

    REQUIRE(make_temp_file(content, path, sizeof(path)));
    CHECK_EQ(tracey_source_read(path, &source), TRACEY_SOURCE_OK);
    CHECK_EQ(source.size, strlen(content));
    CHECK_STR_EQ(source.path, path);
    CHECK_STR_EQ(source.content, content);
    tracey_source_free(&source);

    CHECK_EQ(unlink(path), 0);
}

TEST_CASE(source_reads_empty_file)
{
    char path[64];
    struct tracey_source source;

    REQUIRE(make_temp_file("", path, sizeof(path)));
    CHECK_EQ(tracey_source_read(path, &source), TRACEY_SOURCE_OK);
    CHECK_EQ(source.size, 0U);
    CHECK(source.content != NULL);
    CHECK_EQ(source.content[0], '\0');
    tracey_source_free(&source);

    CHECK_EQ(unlink(path), 0);
}

TEST_CASE(source_free_resets_snapshot)
{
    struct tracey_source source = {NULL, NULL, 0U};

    tracey_source_free(&source);
    CHECK(source.path == NULL);
    CHECK(source.content == NULL);
    CHECK_EQ(source.size, 0U);
}

TEST_CASE(source_missing_file_reports_open_failure)
{
    char path[64];
    struct tracey_source source;

    REQUIRE(make_temp_file("", path, sizeof(path)));
    REQUIRE(unlink(path) == 0);

    CHECK_EQ(tracey_source_read(path, &source), TRACEY_SOURCE_OPEN_FAILED);
    CHECK(source.path == NULL);
    CHECK(source.content == NULL);
    CHECK_EQ(source.size, 0U);
    tracey_source_free(&source);
}

TEST_CASE(source_rejects_invalid_paths)
{
    struct tracey_source source;

    CHECK_EQ(tracey_source_read(NULL, &source), TRACEY_SOURCE_INVALID_PATH);
    CHECK(source.path == NULL);
    CHECK(source.content == NULL);
    CHECK_EQ(source.size, 0U);

    CHECK_EQ(tracey_source_read("", &source), TRACEY_SOURCE_INVALID_PATH);
    CHECK(source.path == NULL);
    CHECK(source.content == NULL);
    CHECK_EQ(source.size, 0U);

    CHECK_EQ(tracey_source_read("/tmp", NULL), TRACEY_SOURCE_INVALID_PATH);
}

TEST_CASE(source_rejects_directory)
{
    struct tracey_source source;

    CHECK_EQ(tracey_source_read("/tmp", &source), TRACEY_SOURCE_NOT_REGULAR_FILE);
    CHECK(source.path == NULL);
    CHECK(source.content == NULL);
    CHECK_EQ(source.size, 0U);
    tracey_source_free(&source);
}

TEST_CASE(source_error_strings_are_stable)
{
    CHECK_STR_EQ(tracey_source_error_string(TRACEY_SOURCE_OK), "No error");
    CHECK(tracey_source_error_string(TRACEY_SOURCE_OPEN_FAILED) != NULL);
    CHECK(tracey_source_error_string((enum tracey_source_error)999) != NULL);
}

TEST_MAIN_BEGIN("source")
    TEST_RUN(source_stub);
    TEST_RUN(source_reads_regular_file);
    TEST_RUN(source_reads_empty_file);
    TEST_RUN(source_free_resets_snapshot);
    TEST_RUN(source_missing_file_reports_open_failure);
    TEST_RUN(source_rejects_invalid_paths);
    TEST_RUN(source_rejects_directory);
    TEST_RUN(source_error_strings_are_stable);
TEST_MAIN_END
