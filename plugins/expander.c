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
    
    size_t len = strlen(input);
    // Each character becomes 3 characters (original + 2 expanded)
    char* result = (char*)malloc(len * 3 + 1);
    if (!result) {
        return NULL;
    }
    
    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        char c = input[i];
        result[pos++] = c;
        
        if (isalpha(c)) {
            // Add two more characters for letters
            result[pos++] = c;
            result[pos++] = c;
        } else if (isdigit(c)) {
            // Add one more character for digits
            result[pos++] = c;
        }
        // For other characters, just keep the original
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
