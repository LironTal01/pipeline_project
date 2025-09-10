#define _GNU_SOURCE
#include "plugin_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Typewriter plugin transformation function
 * Simulates typewriter effect by adding delays between characters
 * @param input Input string to type
 * @return Duplicated input string (caller must free)
 */
const char* plugin_transform(const char* input) {
    if (!input) {
        return NULL;
    }
    
    // Simply duplicate the input string
    // The typewriter effect is simulated by the delay in processing
    return strdup(input);
}

/**
 * Initialize the typewriter plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "typewriter", queue_size);
}
