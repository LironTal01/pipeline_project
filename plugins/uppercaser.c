#define _GNU_SOURCE
#include "plugin_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * Uppercaser plugin transformation function
 * Converts input string to uppercase
 * @param input Input string to convert
 * @return Uppercase version of input string (caller must free)
 */
const char* plugin_transform(const char* input) {
    if (!input) {
        return NULL;
    }
    
    size_t len = strlen(input);
    char* result = (char*)malloc(len + 1);
    if (!result) {
        return NULL;
    }
    
    for (size_t i = 0; i < len; i++) {
        result[i] = toupper(input[i]);
    }
    result[len] = '\0';
    
    return result;
}

/**
 * Initialize the uppercaser plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "uppercaser", queue_size);
}
