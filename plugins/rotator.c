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
    
    size_t len = strlen(input);
    char* result = (char*)malloc(len + 1);
    if (!result) {
        return NULL;
    }
    
    for (size_t i = 0; i < len; i++) {
        char c = input[i];
        if (isalpha(c)) {
            if (islower(c)) {
                result[i] = 'a' + ((c - 'a' + 13) % 26);
            } else {
                result[i] = 'A' + ((c - 'A' + 13) % 26);
            }
        } else {
            result[i] = c;
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
