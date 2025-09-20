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
            // This is the end marker - pass it to next plugin or just finish (don't print <END>)
            if (context->next_place_work) {
                context->next_place_work(item);
            }
            // Don't print <END> - just finish
            free(item);
            // Signal that this plugin is finished
            context->finished = 1;
            consumer_producer_signal_finished(context->queue);
            break;
        }
        
        // Process the item using the plugin's transformation function
        if (context->process_function) {
            const char* result = context->process_function(item);
            
            // Logger plugin always prints, others only print if they're last
            if (strcmp(context->name, "logger") == 0 && result) {
                printf("[%s] %s\n", context->name, result);
            }
            
            // Pass result to next plugin or print it (if not logger)
            if (context->next_place_work && result) {
                context->next_place_work(result);
            } else if (result && strcmp(context->name, "logger") != 0) {
                // Last plugin in chain - print result with prefix (non-logger plugins)
                printf("[%s] %s\n", context->name, result);
                free((void*)result);
            }
            
            // Free the result if it was a logger (since it printed and doesn't pass on)
            if (strcmp(context->name, "logger") == 0 && result) {
                free((void*)result);
            }
            
            // Free the input item
            free(item);
        } else {
            // No processing function - just pass through
            if (context->next_place_work) {
                context->next_place_work(item);
            } else {
                // Last plugin in chain - print with prefix
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
    const char* name = (context && context->name) ? context->name : "plugin";
    fprintf(stderr, "[ERROR][%s] %s\n", name, message);
}

/**
 * Print info message in the format [INFO][Plugin Name] - message
 * @param context Plugin context
 * @param message Info message
 */
void log_info(plugin_context_t* context, const char* message) {
    (void)context; (void)message;  // quiet by default to not pollute STDOUT
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
    if (!process_function || !name || queue_size <= 0) {
        return "common_plugin_init: bad arguments";
    }
    
    // Allow re-initialization by cleaning up existing context
    if (g_context && g_context->initialized) {
        // Clean up existing context
        if (g_context->queue) {
            consumer_producer_destroy(g_context->queue);
            free(g_context->queue);
        }
        free((void*)g_context->name);
        free(g_context);
        g_context = NULL;
    }
    
    // Allocate context
    g_context = (plugin_context_t*)malloc(sizeof(plugin_context_t));
    if (!g_context) {
        return "common_plugin_init: allocation failed";
    }
    
    // Initialize context with memset for better safety
    memset(g_context, 0, sizeof(*g_context));
    g_context->name = strdup(name);
    if (!g_context->name) {
        free(g_context);
        g_context = NULL;
        return "common_plugin_init: name allocation failed";
    }
    g_context->process_function = process_function;
    
    // Allocate and initialize queue
    g_context->queue = (consumer_producer_t*)malloc(sizeof(consumer_producer_t));
    if (!g_context->queue) {
        free((void*)g_context->name);
        free(g_context);
        g_context = NULL;
        return "common_plugin_init: queue allocation failed";
    }
    
    const char* error = consumer_producer_init(g_context->queue, queue_size);
    if (error) {
        free(g_context->queue);
        free((void*)g_context->name);
        free(g_context);
        g_context = NULL;
        return error;
    }
    
    // Create consumer thread
    if (pthread_create(&g_context->consumer_thread, NULL, plugin_consumer_thread, g_context) != 0) {
        consumer_producer_destroy(g_context->queue);
        free(g_context->queue);
        free((void*)g_context->name);
        free(g_context);
        g_context = NULL;
        return "common_plugin_init: thread create failed";
    }
    
    g_context->initialized = 1;
    g_context->finished = 0;
    g_context->joined = 0;
    g_context->next_place_work = NULL;
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
        return NULL; // not initialized
    }
    
    // Signal that no more work will be added
    if (!g_context->finished && g_context->queue) {
        consumer_producer_signal_finished(g_context->queue);
    }
    
    // Wait for consumer thread to finish (prevent double join)
    if (!g_context->joined) {
        pthread_join(g_context->consumer_thread, NULL);
        g_context->joined = 1;
    }
    
    // Clean up resources
    consumer_producer_destroy(g_context->queue);
    free(g_context->queue);
    free((void*)g_context->name);
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
    if (!g_context) {
        return "plugin_place_work: not initialized";
    }
    if (!g_context->initialized) {
        return "plugin_place_work: not initialized";
    }
    if (!g_context->queue) {
        return "plugin_place_work: not initialized";
    }
    if (!str) {
        return "plugin_place_work: bad argument";
    }
    
    // Duplicate the string since we take ownership
    char* str_copy = strdup(str);
    if (!str_copy) {
        return "plugin_place_work: string duplication failed";
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
    if (!g_context || !g_context->initialized || !g_context->queue) {
        return NULL; // not initialized
    }
    
    // Wait for the finished signal
    int rc = consumer_producer_wait_finished(g_context->queue);
    if (rc != 0) {
        return "plugin_wait_finished: queue wait failed";
    }
    
    // Join thread if not already joined
    if (!g_context->joined) {
        pthread_join(g_context->consumer_thread, NULL);
        g_context->joined = 1;
    }
    
    return NULL; // Success
}
