#include "monitor.h"
#include <stdlib.h>

// Initialize the monitor: mutex, condition variable, and state flag
int monitor_init(monitor_t* monitor) {
    if (!monitor) return -1;  // if monitor is NULL, return error
    // Initialize mutex for thread safety
    if (pthread_mutex_init(&monitor->mutex, NULL) != 0) return -1;
    // Initialize condition variable for signaling
    if (pthread_cond_init(&monitor->condition, NULL) != 0) {
        // Clean up mutex if condition variable init fails
        pthread_mutex_destroy(&monitor->mutex);
        return -1;
    }
    monitor->signaled = 0; // No signal yet - monitor starts in non-signaled state
    return 0;
}

// Destroy the monitor and free its resources
void monitor_destroy(monitor_t* monitor) {
    if (!monitor) return;  // if monitor is NULL, exit safely
    pthread_cond_destroy(&monitor->condition);  // destroy condition first
    pthread_mutex_destroy(&monitor->mutex);     // then destroy mutex
}

// Signal the monitor: set the state flag and wake up all waiting threads
void monitor_signal(monitor_t* monitor) {
    if (!monitor) return;  // if monitor is NULL, exit safely
    pthread_mutex_lock(&monitor->mutex);
    monitor->signaled = 1; // Remember that signal was sent
    pthread_cond_broadcast(&monitor->condition); // Wake up all waiting threads
    // broadcast instead of signal - all threads should wake up
    pthread_mutex_unlock(&monitor->mutex);
}

// Reset the monitor: clear the state flag
void monitor_reset(monitor_t* monitor) {
    if (!monitor) return;  // if monitor is NULL, exit safely
    pthread_mutex_lock(&monitor->mutex);
    monitor->signaled = 0; // Clear signal state
    pthread_mutex_unlock(&monitor->mutex);
}

// Wait until the monitor is signaled (infinite wait)
// If signal was already sent, returns immediately
int monitor_wait(monitor_t* monitor) {
    if (!monitor) return -1;  // if monitor is NULL, return error
    pthread_mutex_lock(&monitor->mutex);
    while (!monitor->signaled) {  // while signaled is FALSE (0)
        // Wait for signal (condition variable)
        // pthread_cond_wait unlocks mutex while waiting, locks again when woken
        pthread_cond_wait(&monitor->condition, &monitor->mutex);
    }
    // Signal remains set until manual reset - this is manual reset behavior
    // monitor->signaled = 0; // Removed auto-reset for proper monitor behavior
    pthread_mutex_unlock(&monitor->mutex);
    return 0;
}