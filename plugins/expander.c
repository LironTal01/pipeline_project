#define _GNU_SOURCE
#include "plugin_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/**
 * Expander plugin transformation function
 * Expands each character to multiple characters
 * @param input Input string to expand
 * @return Expanded version of input string (caller must free)
 */
const char* plugin_transform(const char* input) {
    if (!input) {
        return NULL;
    }
    
    // Don't process <END> - just return NULL to stop the pipeline
    if (strcmp(input, "<END>") == 0) {
        return NULL;
    }
    
    size_t len = strlen(input);
    // Insert a single white space between each character
    // Result length: original + (len-1) spaces + null terminator
    char* result = (char*)malloc(len * 2);
    if (!result) {
        return NULL;
    }
    
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        if (i > 0) {
            result[pos++] = ' '; // Add space before each character (except first)
        }
        result[pos++] = input[i];
    }
    result[pos] = '\0';
    
    return result;
}

/**
 * Initialize the expander plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "expander", queue_size);
}

// All other plugin functions are implemented in plugin_common.c
// and will be used automatically
