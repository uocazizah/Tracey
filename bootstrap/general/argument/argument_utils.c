#include <tracey/argument.h>
#include "argument_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
tracey_args_result_t args_add_string(void*** array, size_t* count, size_t* capacity, const char* str)
{
    tracey_args_result_t res = args_ensure_capacity(array, count, capacity);
    if (res != TRACEY_ARGS_OK) return res;
    /* Store const pointer in void** array - safe as we never modify the strings */
    (*array)[(*count)++] = (void*)(uintptr_t)(str);
    return TRACEY_ARGS_OK;
}

/* Free a dynamic array (pointers only, not the strings they point to) */
void args_free_array(void** array)
{
    free(array);
}
