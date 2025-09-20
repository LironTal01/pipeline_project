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
    
    // Don't process <END> - just return NULL to stop the pipeline
    if (strcmp(input, "<END>") == 0) {
        return NULL;
    }
    
    // Simulate typewriter effect with delays between characters
    size_t len = strlen(input);
    char* result = (char*)malloc(len + 1);
    if (!result) {
        return NULL;
    }
    
    // Type each character with a small delay
    for (size_t i = 0; i < len; i++) {
        result[i] = input[i];
        result[i + 1] = '\0';
        
        // Add delay between characters (100ms per character)
        usleep(100000); // 100ms = 100000 microseconds
    }
    
    return result;
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

// All other plugin functions are implemented in plugin_common.c
// and will be used automatically
