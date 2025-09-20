#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
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
    void* context; // Plugin context
    int   is_temp_copy;  // Whether we loaded from a temporary copy
    char* loaded_path;   // Path of the loaded library (so we can unlink the temp file)
} plugin_t;

// Global plugin array
static plugin_t* plugins = NULL;
static int plugin_count = 0;

//Copy a file from src to dst. Returns 0 on success, -1 on error.
static int copy_file(const char* src, const char* dst) {
    int in = open(src, O_RDONLY);
    if (in < 0) {
        return -1;
    }
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0700);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[64 * 1024];
    ssize_t r;
    while ((r = read(in, buf, sizeof buf)) > 0) {
        ssize_t o = 0;
        while (o < r) {
            ssize_t w = write(out, buf + o, (size_t)(r - o));
            if (w < 0) {
                close(in);
                close(out);
                return -1;
            }
            o += w;
        }
    }
    close(in);
    if (close(out) != 0) {
        return -1;
    }
    return (r < 0) ? -1 : 0;
}

/**
 * Load a plugin from shared library
 * @param plugin_name Name of the plugin (without .so extension)
 * @return 0 on success, -1 on failure
 */
int load_plugin(const char* plugin_name) {
    char lib_path[256];
    snprintf(lib_path, sizeof(lib_path), "output/%s.so", plugin_name);

    // Count how many times we've already loaded this plugin
    int duplicate_index = 0;
    for (int i = 0; i < plugin_count; i++) { 
        // Check if the plugin name is the same as the plugin name we are trying to load
        if (plugins[i].name && strcmp(plugins[i].name, plugin_name) == 0) { 
            duplicate_index++;
        }
    }

    char load_path[256]; // Path of the loaded library (so we can unlink the temp file)
    int is_temp = 0;
    if (duplicate_index == 0) {
        // First instance: load directly from the original .so
        snprintf(load_path, sizeof(load_path), "%s", lib_path); // Set the load path to the original path
    } else {
        // Subsequent instances: create a unique temporary copy
        snprintf(load_path, sizeof(load_path), 
                 "/tmp/analyzer_%ld_%d_%s.so",
                 (long)getpid(), duplicate_index, plugin_name);
        if (copy_file(lib_path, load_path) != 0) { // Copy the file to the temporary path
            fprintf(stderr,
                    "[ERROR][main] failed to duplicate '%s' -> '%s'\n",
                    lib_path, load_path);
            return -1;
        }
        is_temp = 1;
    }
    
    void* handle = dlopen(load_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr,
                "[ERROR][main] dlopen('%s') failed: %s\n",
                load_path, dlerror());
        if (is_temp) {
            unlink(load_path);
        }
        return -1;
    }

    plugin_t* new_arr = realloc(plugins, (plugin_count + 1) * sizeof(plugin_t));
    if (!new_arr) {
        dlclose(handle);
        if (is_temp) {
            unlink(load_path);
        }
        return -1;
    }
    plugins = new_arr;

    plugin_t* plugin = &plugins[plugin_count];
    memset(plugin, 0, sizeof(plugin_t));
    plugin->handle        = handle;
    plugin->init          = (plugin_init_func)dlsym(handle, "plugin_init");
    plugin->fini          = (plugin_fini_func)dlsym(handle, "plugin_fini");
    plugin->place_work    = (plugin_place_work_func)dlsym(handle, "plugin_place_work");
    plugin->attach        = (plugin_attach_func)dlsym(handle, "plugin_attach");
    plugin->wait_finished = (plugin_wait_finished_func)dlsym(handle, "plugin_wait_finished");
    plugin->get_name      = (plugin_get_name_func)dlsym(handle, "plugin_get_name");
    //plugin->is_temp_copy  = is_temp;
    //plugin->loaded_path   = strdup(load_path);

    // Check if all required functions are found
    if (!plugin->init || !plugin->fini || !plugin->place_work || 
        !plugin->attach || !plugin->wait_finished || !plugin->get_name) {
            fprintf(stderr,
                "[ERROR][main] dlsym() failed for plugin '%s': missing symbol\n",plugin_name); // Print the error message
        dlclose(handle); // Close the shared library
        if (is_temp) {  // If the plugin is a temporary copy, unlink the temporary file
            unlink(load_path); // Unlink the temporary file
        }
        return -1;
    }
    
    plugin->name = strdup(plugin_name); // Set the plugin name
    plugin->is_temp_copy  = is_temp;    // Set the is_temp_copy flag
    plugin->loaded_path   = is_temp ? strdup(load_path) : NULL; // Set the loaded_path
    plugin_count++; // Increment the plugin count
    return 0; // Return 0 on success
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
    
    // Attach each plugin to the next one in the chain
    for (int i = 0; i < plugin_count - 1; i++) {
        plugins[i].attach(plugins[i + 1].place_work);
    }

    return 0; // Return 0 on success
}


/* Clean up all plugins. Calls plugin_fini on each, closes the handles,
 * and unlinks any temporary .so files we created.
 */
void fini_plugins(void) {
    for (int i = 0; i < plugin_count; i++) {
        if (plugins[i].fini) {
            const char* error = plugins[i].fini();
            if (error) {
                fprintf(stderr, "Error finalizing plugin %s: %s\n", plugins[i].name, error);
            }
        }
        // Close the shared library
        if (plugins[i].handle) {
            dlclose(plugins[i].handle);
        }
        // Unlink the temporary file if it is a temporary copy
        if (plugins[i].is_temp_copy && plugins[i].loaded_path) { // If the plugin is a temporary copy and the loaded_path is not NULL
            unlink(plugins[i].loaded_path);
            free(plugins[i].loaded_path);
        }    
        // Free the plugin name
        if (plugins[i].name) { // If the plugin name is not NULL
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
        print_usage(argv[0]);
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
