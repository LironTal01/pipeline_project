#define _GNU_SOURCE
#include "plugin_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Logger plugin transformation function
 * Simply logs the input string
 * @param input Input string to log
 * @return Duplicated input string (caller must free)
 */
const char* plugin_transform(const char* input) {
    if (!input) {
        return NULL;
    }
    
    // Simply duplicate the input string
    return strdup(input);
}

/**
 * Initialize the logger plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "logger", queue_size);
}
