#define _GNU_SOURCE
#include "plugin_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


/**
 * Rotator plugin transformation function
 * Rotates each character by 13 positions (ROT13)
 * @param input Input string to rotate
 * @return Rotated version of input string (caller must free)
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
    char* result = (char*)malloc(len + 1);
    if (!result) {
        return NULL;
    }
    
    // Rotate right: move every character one position to the right
    // The last character wraps around to the front
    if (len > 0) {
        result[0] = input[len - 1]; // Last character goes to first position
        for (size_t i = 1; i < len; i++) {
            result[i] = input[i - 1]; // Each character moves one position right
        }
    }
    result[len] = '\0';
    
    return result;
}

/**
 * Initialize the rotator plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_init(int queue_size) {
    return common_plugin_init(plugin_transform, "rotator", queue_size);
}

// All other plugin functions are implemented in plugin_common.c
// and will be used automatically
