#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include "consumer_producer.h"

/**
 * Initialize a new producer-consumer queue
 * @param queue: pointer to queue structure
 * @param capacity: maximum number of items (0 means no capacity)
 * @return: error message or NULL if success
 */
const char* consumer_producer_init(consumer_producer_t* queue, int capacity) {
    // Validate input parameters
    if (!queue || capacity < 0) return "Invalid arguments";
    
    // Allocate memory for item pointers (only if capacity > 0)
    queue->items = (capacity > 0) ? (char**)malloc(sizeof(char*) * capacity) : NULL;
    if (capacity > 0 && !queue->items) return "Memory allocation failed";
    
    // Initialize queue state
    queue->capacity = capacity;  // Max items that can be stored
    queue->count = 0;           // Current number of items
    queue->head = 0;            // Index to remove items from
    queue->tail = 0;            // Index to add items to
    queue->finished = 0;        // Flag indicating if queue is done
    
    // Initialize synchronization monitors with proper cleanup on failure
    if (monitor_init(&queue->not_full_monitor) != 0) {
        free(queue->items);
        return "Monitor init failed";
    }
    if (monitor_init(&queue->not_empty_monitor) != 0) {
        monitor_destroy(&queue->not_full_monitor);
        free(queue->items);
        return "Monitor init failed";
    }
    if (monitor_init(&queue->finished_monitor) != 0) {
        monitor_destroy(&queue->not_empty_monitor);
        monitor_destroy(&queue->not_full_monitor);
        free(queue->items);
        return "Monitor init failed";
    }
    
    return NULL;  // Success
}

/**
 * Clean up and free all resources used by the queue
 * @param queue: pointer to queue structure
 */
void consumer_producer_destroy(consumer_producer_t* queue) {
    if (!queue) return;
    
    // Signal finished to wake up any waiting threads
    consumer_producer_signal_finished(queue);
    
    // Free all remaining items in the queue
    if (queue->items && queue->capacity > 0) {
        for (int i = 0; i < queue->count; ++i) {
            int idx = (queue->head + i) % queue->capacity;
            free(queue->items[idx]);  // Free each string
        }
    }
    
    // Free the items array and monitors
    free(queue->items);
    monitor_destroy(&queue->not_full_monitor);
    monitor_destroy(&queue->not_empty_monitor);
    monitor_destroy(&queue->finished_monitor);
}

/**
 * Add an item to the queue (Producer operation)
 * @param queue: pointer to queue structure
 * @param item: string to add (will be copied)
 * @return: error message or NULL if success
 */
const char* consumer_producer_put(consumer_producer_t* queue, const char* item) {
    // Validate input parameters
    if (!queue || !item) return "Invalid arguments";
    
    // Create a copy of the string (producer owns the memory)
    char* new_item = strdup(item);
    if (!new_item) {
        return "Memory allocation failed";
    }
    
    // Enter critical section - use single mutex for all synchronization
    monitor_t* lock = &queue->not_empty_monitor;
    pthread_mutex_lock(&lock->mutex);
    
    // Special case: zero capacity queue rejects all items
    if (queue->capacity == 0) {
        pthread_mutex_unlock(&lock->mutex);
        free(new_item);
        return "Queue has zero capacity";
    }
    
    // Wait until there's space in the queue or queue is finished
    while (queue->count == queue->capacity && !queue->finished) {
        pthread_cond_wait(&lock->condition, &lock->mutex);  // Sleep until woken up
    }
    
    // Check if queue was finished while waiting
    if (queue->finished) {
        pthread_mutex_unlock(&lock->mutex);
        free(new_item);
        return "Queue finished";
    }
    
    // Add item to the queue (circular buffer)
    queue->items[queue->tail] = new_item;
    queue->tail = (queue->tail + 1) % queue->capacity;  // Wrap around
    queue->count++;
    
    // Wake up any waiting consumers
    pthread_cond_broadcast(&lock->condition);
    
    pthread_mutex_unlock(&lock->mutex);
    return NULL;  // Success
}

/**
 * Remove and return an item from the queue (Consumer operation)
 * @param queue: pointer to queue structure
 * @return: pointer to string (caller must free) or NULL if no items
 */
char* consumer_producer_get(consumer_producer_t* queue) {
    // Validate input parameters
    if (!queue) return NULL;
    
    // Enter critical section - use single mutex for all synchronization
    monitor_t* lock = &queue->not_empty_monitor;
    pthread_mutex_lock(&lock->mutex);
    
    // Special case: zero capacity queue has no items
    if (queue->capacity == 0) {
        pthread_mutex_unlock(&lock->mutex);
        return NULL;
    }
    
    // Wait until there are items in the queue or queue is finished
    while (queue->count == 0 && !queue->finished) {
        pthread_cond_wait(&lock->condition, &lock->mutex);  // Sleep until woken up
    }
    
    // Check if queue is finished and empty
    if (queue->count == 0) {
        pthread_mutex_unlock(&lock->mutex);
        return NULL;  // No more items available
    }
    
    // Remove item from the queue (circular buffer)
    char* item = queue->items[queue->head];
    queue->items[queue->head] = NULL;  // Clear the slot
    queue->head = (queue->head + 1) % queue->capacity;  // Wrap around
    queue->count--;
    
    // Wake up any waiting producers
    pthread_cond_broadcast(&lock->condition);
    
    pthread_mutex_unlock(&lock->mutex);
    return item;  // Caller must free this string
}

/**
 * Signal that no more items will be added to the queue
 * @param queue: pointer to queue structure
 */
void consumer_producer_signal_finished(consumer_producer_t* queue) {
    if (!queue) return;
    
    // Set finished flag under the same mutex as queue operations
    monitor_t* lock = &queue->not_empty_monitor;
    pthread_mutex_lock(&lock->mutex);
    queue->finished = 1;  // Mark queue as finished
    pthread_cond_broadcast(&lock->condition);  // Wake up all waiting threads
    pthread_mutex_unlock(&lock->mutex);
    
    // Signal the finished event for external listeners
    monitor_signal(&queue->finished_monitor);
}

/**
 * Wait until the queue is finished (no more items will be added)
 * @param queue: pointer to queue structure
 * @return: 0 on success, -1 on error
 */
int consumer_producer_wait_finished(consumer_producer_t* queue) {
    if (!queue) return -1;
    
    // Use the same mutex as other operations for consistency
    monitor_t* lock = &queue->not_empty_monitor;
    pthread_mutex_lock(&lock->mutex);
    
    // Wait until finished flag is set (with proper spurious wakeup handling)
    while (!queue->finished) {
        pthread_cond_wait(&lock->condition, &lock->mutex);
    }
    
    pthread_mutex_unlock(&lock->mutex);
    return 0;  // Success
}
