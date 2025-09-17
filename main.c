#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include "plugins/plugin_sdk.h"

// Function pointer types for plugin functions
typedef const char* (*plugin_init_func)(int);
typedef const char* (*plugin_fini_func)(void);
typedef const char* (*plugin_place_work_func)(const char*);
typedef void (*plugin_attach_func)(const char* (*)(const char*));
typedef const char* (*plugin_wait_finished_func)(void);
typedef const char* (*plugin_get_name_func)(void);

// Plugin structure
typedef struct {
    void* handle;
    plugin_init_func init;
    plugin_fini_func fini;
    plugin_place_work_func place_work;
    plugin_attach_func attach;
    plugin_wait_finished_func wait_finished;
    plugin_get_name_func get_name;
    char* name;
} plugin_t;

// Global plugin array
static plugin_t* plugins = NULL;
static int plugin_count = 0;

/**
 * Load a plugin from shared library
 * @param plugin_name Name of the plugin (without .so extension)
 * @return 0 on success, -1 on failure
 */
int load_plugin(const char* plugin_name) {
    char lib_path[256];
    snprintf(lib_path, sizeof(lib_path), "output/%s.so", plugin_name);
    
    // Open the shared library
    void* handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "[ERROR][main] dlopen('%s') failed: %s\n", lib_path, dlerror());
        return -1;
    }
    
    // Resize plugin array
    plugins = (plugin_t*)realloc(plugins, (plugin_count + 1) * sizeof(plugin_t));
    if (!plugins) {
        dlclose(handle);
        return -1;
    }
    
    plugin_t* plugin = &plugins[plugin_count];
    memset(plugin, 0, sizeof(plugin_t));
    
    // Get function pointers
    plugin->handle = handle;
    plugin->init = (plugin_init_func)dlsym(handle, "plugin_init");
    plugin->fini = (plugin_fini_func)dlsym(handle, "plugin_fini");
    plugin->place_work = (plugin_place_work_func)dlsym(handle, "plugin_place_work");
    plugin->attach = (plugin_attach_func)dlsym(handle, "plugin_attach");
    plugin->wait_finished = (plugin_wait_finished_func)dlsym(handle, "plugin_wait_finished");
    plugin->get_name = (plugin_get_name_func)dlsym(handle, "plugin_get_name");
    
    // Check if all required functions are found
    if (!plugin->init || !plugin->fini || !plugin->place_work || 
        !plugin->attach || !plugin->wait_finished || !plugin->get_name) {
        fprintf(stderr, "[ERROR][main] dlsym() failed for plugin '%s': missing symbol\n", plugin_name);
        dlclose(handle);
        return -1;
    }
    
    plugin->name = strdup(plugin_name);
    plugin_count++;
    
    return 0;
}

/**
 * Initialize all loaded plugins
 * @param queue_size Queue size for all plugins
 * @return 0 on success, -1 on failure
 */
int init_plugins(int queue_size) {
    for (int i = 0; i < plugin_count; i++) {
        const char* error = plugins[i].init(queue_size);
        if (error) {
            fprintf(stderr, "Error initializing plugin %s: %s\n", plugins[i].name, error);
            return -1;
        }
    }
    
    // Attach plugins in chain
    for (int i = 0; i < plugin_count - 1; i++) {
        plugins[i].attach(plugins[i + 1].place_work);
    }
    
    return 0;
}

/**
 * Finalize all loaded plugins
 */
void fini_plugins(void) {
    for (int i = 0; i < plugin_count; i++) {
        if (plugins[i].fini) {
            const char* error = plugins[i].fini();
            if (error) {
                fprintf(stderr, "Error finalizing plugin %s: %s\n", plugins[i].name, error);
            }
        }
        
        if (plugins[i].handle) {
            dlclose(plugins[i].handle);
        }
        
        if (plugins[i].name) {
            free(plugins[i].name);
        }
    }
    
    if (plugins) {
        free(plugins);
        plugins = NULL;
    }
    plugin_count = 0;
}

/**
 * Process input through the plugin chain
 * @param input Input string to process
 */
void process_input(const char* input) {
    if (plugin_count == 0) {
        printf("%s\n", input);
        return;
    }
    
    // Send input to first plugin
    const char* error = plugins[0].place_work(input);
    if (error) {
        fprintf(stderr, "Error placing work: %s\n", error);
    }
}

/**
 * Wait for all plugins to finish processing
 */
void wait_for_completion(void) {
    for (int i = 0; i < plugin_count; i++) {
        if (plugins[i].wait_finished) {
            const char* error = plugins[i].wait_finished();
            if (error) {
                fprintf(stderr, "Error waiting for plugin %s: %s\n", plugins[i].name, error);
            }
        }
    }
}

/**
 * Print usage information
 */
void print_usage(const char* program_name) {
    (void)program_name; // Suppress unused parameter warning
    printf("Usage: ./analyzer <queue_size> <plugin1> <plugin2> ... <pluginN>\n\n");
    printf("Arguments:\n");
    printf("queue_size Maximum number of items in each plugin's queue\n");
    printf("plugin1..N Names of plugins to load (without .so extension)\n\n");
    printf("Available plugins:\n");
    printf("logger - Logs all strings that pass through\n");
    printf("typewriter - Simulates typewriter effect with delays\n");
    printf("uppercaser - Converts strings to uppercase\n");
    printf("rotator - Move every character to the right. Last character moves to the beginning.\n");
    printf("flipper - Reverses the order of characters\n");
    printf("expander - Expands each character with spaces\n\n");
    printf("Example:\n");
    printf("./analyzer 20 uppercaser rotator logger\n");
    printf("echo 'hello' | ./analyzer 20 uppercaser rotator logger\n");
    printf("echo '<END>' | ./analyzer 20 uppercaser rotator logger\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Parse queue size
    int queue_size = atoi(argv[1]);
    if (queue_size <= 0) {
        fprintf(stderr, "Error: Queue size must be positive\n");
        return 1;
    }
    
    // Load plugins
    for (int i = 2; i < argc; i++) {
        if (load_plugin(argv[i]) != 0) {
            fini_plugins();
            return 1;
        }
    }
    
    // Initialize plugins
    if (init_plugins(queue_size) != 0) {
        fini_plugins();
        return 1;
    }
    
    // Process input from stdin
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        // Remove newline if present
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        
        // Check for end marker
        if (strcmp(buffer, "<END>") == 0) {
            // Send <END> to plugins so they know to finish
            process_input(buffer);
            break;
        }
        
        process_input(buffer);
    }
    
    // Wait for all processing to complete
    wait_for_completion();
    
    // Finalize plugins
    fini_plugins();
    
    printf("Pipeline shutdown complete.\n");
    return 0;
}
