#define _GNU_SOURCE
#include "plugin_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Global plugin context
static plugin_context_t* g_context = NULL;

/**
 * Generic consumer thread function
 * This function runs in a separate thread and processes items from the queue
 * @param arg Pointer to plugin_context_t
 * @return NULL
 */
void* plugin_consumer_thread(void* arg) {
    plugin_context_t* context = (plugin_context_t*)arg;
    if (!context) {
        return NULL;
    }
    
    char* item;
    while ((item = consumer_producer_get(context->queue)) != NULL) {
        // Check if this is the end marker
        if (strcmp(item, "<END>") == 0) {
            // This is the end marker - pass it to next plugin or print it, then break
            if (context->next_place_work) {
                context->next_place_work(item);
            } else {
                printf("[%s] %s\n", context->name, item);
            }
            free(item);
            // Signal that this plugin is finished
            context->finished = 1;
            consumer_producer_signal_finished(context->queue);
            break;
        }
        
        // Process the item using the plugin's transformation function
        if (context->process_function) {
            const char* result = context->process_function(item);
            
            // Pass result to next plugin or print it
            if (context->next_place_work && result) {
                context->next_place_work(result);
            } else if (result) {
                // Last plugin in chain - print result
                printf("[%s] %s\n", context->name, result);
                free((void*)result);
            }
            
            // Free the input item
            free(item);
        } else {
            // No processing function - just pass through
            if (context->next_place_work) {
                context->next_place_work(item);
            } else {
                printf("[%s] %s\n", context->name, item);
                free(item);
            }
        }
    }
    
    // Signal that this plugin is finished
    context->finished = 1;
    consumer_producer_signal_finished(context->queue);
    
    return NULL;
}

/**
 * Print error message in the format [ERROR][Plugin Name] - message
 * @param context Plugin context
 * @param message Error message
 */
void log_error(plugin_context_t* context, const char* message) {
    if (context && message) {
        fprintf(stderr, "[ERROR][%s] - %s\n", context->name, message);
    }
}

/**
 * Print info message in the format [INFO][Plugin Name] - message
 * @param context Plugin context
 * @param message Info message
 */
void log_info(plugin_context_t* context, const char* message) {
    if (context && message) {
        printf("[INFO][%s] - %s\n", context->name, message);
    }
}

/**
 * Get the plugin's name
 * @return The plugin's name (should not be modified or freed)
 */
__attribute__((visibility("default")))
const char* plugin_get_name(void) {
    if (g_context) {
        return g_context->name;
    }
    return "unknown";
}

/**
 * Initialize the common plugin infrastructure with the specified queue size
 * @param process_function Plugin-specific processing function
 * @param name Plugin name
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
const char* common_plugin_init(const char* (*process_function)(const char*), const char* name, int queue_size) {
    if (!process_function) {
        return "Process function is NULL";
    }
    
    if (!name) {
        return "Plugin name is NULL";
    }
    
    if (queue_size <= 0) {
        return "Queue size must be positive";
    }
    
    // Allocate context
    g_context = (plugin_context_t*)calloc(1, sizeof(plugin_context_t));
    if (!g_context) {
        return "Failed to allocate plugin context";
    }
    
    // Initialize context
    g_context->name = name;
    g_context->process_function = process_function;
    g_context->initialized = 0;
    g_context->finished = 0;
    
    // Allocate and initialize queue
    g_context->queue = (consumer_producer_t*)calloc(1, sizeof(consumer_producer_t));
    if (!g_context->queue) {
        free(g_context);
        g_context = NULL;
        return "Failed to allocate queue";
    }
    
    const char* error = consumer_producer_init(g_context->queue, queue_size);
    if (error) {
        free(g_context->queue);
        free(g_context);
        g_context = NULL;
        return error;
    }
    
    // Create consumer thread
    if (pthread_create(&g_context->consumer_thread, NULL, plugin_consumer_thread, g_context) != 0) {
        consumer_producer_destroy(g_context->queue);
        free(g_context->queue);
        free(g_context);
        g_context = NULL;
        return "Failed to create consumer thread";
    }
    
    g_context->initialized = 1;
    return NULL; // Success
}

/**
 * Initialize the plugin with the specified queue size - calls common_plugin_init
 * This function should be implemented by each plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
__attribute__((weak))
const char* plugin_init(int queue_size) {
    // This should be overridden by each plugin
    (void)queue_size; // Suppress unused parameter warning
    return "Plugin init not implemented";
}

/**
 * Finalize the plugin - drain queue and terminate thread gracefully (i.e. pthread_join)
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_fini(void) {
    if (!g_context || !g_context->initialized) {
        return "Plugin not initialized";
    }
    
    // Signal that no more work will be added
    consumer_producer_signal_finished(g_context->queue);
    
    // Wait for consumer thread to finish
    if (pthread_join(g_context->consumer_thread, NULL) != 0) {
        return "Failed to join consumer thread";
    }
    
    // Clean up resources
    consumer_producer_destroy(g_context->queue);
    free(g_context->queue);
    free(g_context);
    g_context = NULL;
    
    return NULL; // Success
}

/**
 * Place work (a string) into the plugin's queue
 * @param str The string to process (plugin takes ownership if it allocates new memory)
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_place_work(const char* str) {
    if (!g_context || !g_context->initialized) {
        return "Plugin not initialized";
    }
    
    if (!str) {
        return "String is NULL";
    }
    
    // Duplicate the string since we take ownership
    char* str_copy = strdup(str);
    if (!str_copy) {
        return "Failed to duplicate string";
    }
    
    const char* error = consumer_producer_put(g_context->queue, str_copy);
    if (error) {
        free(str_copy);
        return error;
    }
    
    return NULL; // Success
}

/**
 * Attach this plugin to the next plugin in the chain
 * @param next_place_work Function pointer to the next plugin's place_work function
 */
__attribute__((visibility("default")))
void plugin_attach(const char* (*next_place_work)(const char*)) {
    if (g_context) {
        g_context->next_place_work = next_place_work;
    }
}

/**
 * Wait until the plugin has finished processing all work and is ready to shutdown
 * This is a blocking function used for graceful shutdown coordination
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_wait_finished(void) {
    if (!g_context || !g_context->initialized) {
        return "Plugin not initialized";
    }
    
    // Wait for the finished signal
    if (consumer_producer_wait_finished(g_context->queue) != 0) {
        return "Failed to wait for finished signal";
    }
    
    return NULL; // Success
}
