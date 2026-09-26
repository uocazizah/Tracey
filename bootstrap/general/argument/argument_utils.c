#include <tracey/argument.h>
#include "argument_internal.h"
#include <stdlib.h>
#include <string.h>

/* Ensure array has capacity for at least one more element */
tracey_args_result_t args_ensure_capacity(void*** array, size_t* count, size_t* capacity)
{
    if (*count >= *capacity) {
        size_t new_cap = *capacity * 2;
        if (new_cap < *capacity) return TRACEY_ARGS_ERROR_NOMEM; /* overflow */
        void** tmp = realloc(*array, new_cap * sizeof(void*));
        if (!tmp) return TRACEY_ARGS_ERROR_NOMEM;
        *array = tmp;
        *capacity = new_cap;
    }
    return TRACEY_ARGS_OK;
}

/* Add a string pointer to a dynamic array */
tracey_args_result_t args_add_string(char*** array, size_t* count, size_t* capacity, const char* str)
{
    tracey_args_result_t res = args_ensure_capacity((void***)array, count, capacity);
    if (res != TRACEY_ARGS_OK) return res;
    (*array)[(*count)++] = (char*)str;
    return TRACEY_ARGS_OK;
}

/* Free a dynamic array (pointers only, not the strings they point to) */
void args_free_array(char** array)
{
    free(array);
}