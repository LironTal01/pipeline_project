#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include "../../plugins/sync/consumer_producer.h"

#define QUEUE_CAPACITY 10
#define NUM_EXTREME_TESTS 8

// Global variables for extreme tests
static volatile int test_failed = 0;
static volatile int test_timeout = 0;

// Signal handler for timeout
void timeout_handler(int sig) {
    test_timeout = 1;
    printf("⏰ TEST TIMEOUT - Test took too long!\n");
}

// Set up timeout for tests
void setup_timeout(int seconds) {
    test_timeout = 0;
    signal(SIGALRM, timeout_handler);
    alarm(seconds);
}

void cleanup_timeout() {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

// Test data structure
typedef struct {
    consumer_producer_t* queue;
    int id;
    int should_fail;
    int items_to_produce;
    int items_to_consume;
} thread_data_t;

// Producer thread for extreme tests
void* extreme_producer_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    char buffer[100];
    
    for (int i = 0; i < data->items_to_produce && !test_failed && !test_timeout; ++i) {
        snprintf(buffer, sizeof(buffer), "EXTREME_ITEM_%d_FROM_PRODUCER_%d", i, data->id);
        
        const char* result = consumer_producer_put(data->queue, buffer);
        if (data->should_fail && result != NULL) {
            printf("❌ Producer %d: Expected failure but got success: %s\n", data->id, result);
            test_failed = 1;
            break;
        } else if (!data->should_fail && result != NULL) {
            printf("❌ Producer %d: Expected success but got failure: %s\n", data->id, result);
            test_failed = 1;
            break;
        }
        
        if (!data->should_fail) {
            printf("Producer %d: Put '%s'\n", data->id, buffer);
        }
        
        // Random delay to create race conditions
        usleep(rand() % 1000);
    }
    
    printf("Producer %d: Finished producing.\n", data->id);
    return NULL;
}

// Consumer thread for extreme tests
void* extreme_consumer_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    int items_received = 0;
    
    while (items_received < data->items_to_consume && !test_failed && !test_timeout) {
        char* item = consumer_producer_get(data->queue);
        if (item != NULL) {
            printf("Consumer %d: Got '%s'\n", data->id, item);
            free(item);
            items_received++;
        } else {
            // Queue is finished
            break;
        }
        
        // Random delay to create race conditions
        usleep(rand() % 1000);
    }
    
    printf("Consumer %d: Finished consuming %d items.\n", data->id, items_received);
    return NULL;
}

/**
 * בדיקה קיצונית 1: Stress Test - הרבה Producers ו-Consumers
 * 10 Producers, 10 Consumers, 1000 פריטים כל אחד
 */
void test_extreme_stress() {
    printf("🔥 EXTREME TEST 1: Stress Test (10 Producers, 10 Consumers, 1000 items each)\n");
    setup_timeout(30);
    
    consumer_producer_t queue;
    consumer_producer_init(&queue, QUEUE_CAPACITY);
    
    const int NUM_PRODUCERS = 10;
    const int NUM_CONSUMERS = 10;
    const int ITEMS_PER_PRODUCER = 1000;
    
    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];
    thread_data_t prod_data[NUM_PRODUCERS];
    thread_data_t cons_data[NUM_CONSUMERS];
    
    // Create producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        prod_data[i] = (thread_data_t){&queue, i + 1, 0, ITEMS_PER_PRODUCER, 0};
        pthread_create(&producers[i], NULL, extreme_producer_thread, &prod_data[i]);
    }
    
    // Create consumers
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        cons_data[i] = (thread_data_t){&queue, i + 1, 0, 0, ITEMS_PER_PRODUCER};
        pthread_create(&consumers[i], NULL, extreme_consumer_thread, &cons_data[i]);
    }
    
    // Wait for all producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pthread_join(producers[i], NULL);
    }
    
    // Signal finished
    consumer_producer_signal_finished(&queue);
    
    // Wait for all consumers
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        pthread_join(consumers[i], NULL);
    }
    
    cleanup_timeout();
    consumer_producer_destroy(&queue);
    
    if (test_failed || test_timeout) {
        printf("❌ EXTREME TEST 1 FAILED\n\n");
    } else {
        printf("✅ EXTREME TEST 1 PASSED\n\n");
    }
}

/**
 * בדיקה קיצונית 2: Memory Pressure Test
 * מנסה ליצור הרבה פריטים גדולים
 */
void test_extreme_memory_pressure() {
    printf("💾 EXTREME TEST 2: Memory Pressure Test (Large items)\n");
    setup_timeout(20);
    
    consumer_producer_t queue;
    consumer_producer_init(&queue, 5);
    
    const int LARGE_ITEM_SIZE = 10000; // 10KB per item
    char* large_item = malloc(LARGE_ITEM_SIZE);
    memset(large_item, 'X', LARGE_ITEM_SIZE - 1);
    large_item[LARGE_ITEM_SIZE - 1] = '\0';
    
    // Fill queue with large items
    for (int i = 0; i < 5; ++i) {
        const char* result = consumer_producer_put(&queue, large_item);
        if (result != NULL) {
            printf("❌ Failed to put large item %d: %s\n", i, result);
            test_failed = 1;
        }
    }
    
    // Consume all items
    for (int i = 0; i < 5; ++i) {
        char* item = consumer_producer_get(&queue);
        if (item == NULL) {
            printf("❌ Failed to get large item %d\n", i);
            test_failed = 1;
            break;
        }
        free(item);
    }
    
    free(large_item);
    cleanup_timeout();
    consumer_producer_destroy(&queue);
    
    if (test_failed || test_timeout) {
        printf("❌ EXTREME TEST 2 FAILED\n\n");
    } else {
        printf("✅ EXTREME TEST 2 PASSED\n\n");
    }
}

/**
 * בדיקה קיצונית 3: Rapid Start/Stop Test
 * מתחיל ומפסיק הרבה threads במהירות
 */
void test_extreme_rapid_start_stop() {
    printf("⚡ EXTREME TEST 3: Rapid Start/Stop Test\n");
    setup_timeout(15);
    
    consumer_producer_t queue;
    consumer_producer_init(&queue, QUEUE_CAPACITY);
    
    const int NUM_CYCLES = 50;
    
    for (int cycle = 0; cycle < NUM_CYCLES && !test_failed && !test_timeout; ++cycle) {
        pthread_t producer, consumer;
        thread_data_t prod_data = {&queue, 1, 0, 10, 0};
        thread_data_t cons_data = {&queue, 1, 0, 0, 10};
        
        // Start producer and consumer
        pthread_create(&producer, NULL, extreme_producer_thread, &prod_data);
        pthread_create(&consumer, NULL, extreme_consumer_thread, &cons_data);
        
        // Wait for producer to finish
        pthread_join(producer, NULL);
        
        // Signal finished
        consumer_producer_signal_finished(&queue);
        
        // Wait for consumer
        pthread_join(consumer, NULL);
        
        // Reset for next cycle
        consumer_producer_destroy(&queue);
        consumer_producer_init(&queue, QUEUE_CAPACITY);
    }
    
    cleanup_timeout();
    consumer_producer_destroy(&queue);
    
    if (test_failed || test_timeout) {
        printf("❌ EXTREME TEST 3 FAILED\n\n");
    } else {
        printf("✅ EXTREME TEST 3 PASSED\n\n");
    }
}

/**
 * בדיקה קיצונית 4: Zero Capacity Test
 * בודק תור עם קיבולת 0
 */
void test_extreme_zero_capacity() {
    printf("🚫 EXTREME TEST 4: Zero Capacity Test\n");
    setup_timeout(10);
    
    consumer_producer_t queue;
    consumer_producer_init(&queue, 0);
    
    // Try to put item in zero-capacity queue
    const char* result = consumer_producer_put(&queue, "test");
    if (result == NULL) {
        printf("❌ Expected failure for zero capacity queue\n");
        test_failed = 1;
    } else {
        printf("✅ Correctly rejected item in zero capacity queue: %s\n", result);
    }
    
    // Try to get from empty zero-capacity queue
    char* item = consumer_producer_get(&queue);
    if (item != NULL) {
        printf("❌ Expected NULL from empty zero capacity queue\n");
        test_failed = 1;
        free(item);
    } else {
        printf("✅ Correctly returned NULL from empty zero capacity queue\n");
    }
    
    cleanup_timeout();
    consumer_producer_destroy(&queue);
    
    if (test_failed || test_timeout) {
        printf("❌ EXTREME TEST 4 FAILED\n\n");
    } else {
        printf("✅ EXTREME TEST 4 PASSED\n\n");
    }
}

/**
 * בדיקה קיצונית 5: Concurrent Shutdown Test
 * הרבה threads מנסים לסגור את התור במקביל
 */
void test_extreme_concurrent_shutdown() {
    printf("🔄 EXTREME TEST 5: Concurrent Shutdown Test\n");
    setup_timeout(15);
    
    consumer_producer_t queue;
    consumer_producer_init(&queue, QUEUE_CAPACITY);
    
    const int NUM_THREADS = 20;
    pthread_t threads[NUM_THREADS];
    
    // Create threads that will try to shutdown
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, (void*)consumer_producer_signal_finished, &queue);
    }
    
    // Wait for all threads
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }
    
    cleanup_timeout();
    consumer_producer_destroy(&queue);
    
    if (test_failed || test_timeout) {
        printf("❌ EXTREME TEST 5 FAILED\n\n");
    } else {
        printf("✅ EXTREME TEST 5 PASSED\n\n");
    }
}

/**
 * בדיקה קיצונית 6: Empty String Test
 * בודק פריטים ריקים ופריטים עם תווים מיוחדים
 */
void test_extreme_empty_strings() {
    printf("📝 EXTREME TEST 6: Empty String Test\n");
    setup_timeout(10);
    
    consumer_producer_t queue;
    consumer_producer_init(&queue, QUEUE_CAPACITY);
    
    const char* test_strings[] = {
        "",           // Empty string
        " ",          // Single space
        "\n",         // Newline
        "\t",         // Tab
        "\0",         // Null character
        "שלום",       // Hebrew text
        "🚀🔥💯",     // Emojis
        NULL
    };
    
    for (int i = 0; test_strings[i] != NULL && !test_failed; ++i) {
        const char* result = consumer_producer_put(&queue, test_strings[i]);
        if (result != NULL) {
            printf("❌ Failed to put string %d: %s\n", i, result);
            test_failed = 1;
        } else {
            printf("✅ Put string %d: '%s'\n", i, test_strings[i]);
        }
    }
    
    // Consume all items
    for (int i = 0; i < 7; ++i) {
        char* item = consumer_producer_get(&queue);
        if (item == NULL) {
            printf("❌ Failed to get item %d\n", i);
            test_failed = 1;
            break;
        }
        printf("✅ Got item %d: '%s'\n", i, item);
        free(item);
    }
    
    cleanup_timeout();
    consumer_producer_destroy(&queue);
    
    if (test_failed || test_timeout) {
        printf("❌ EXTREME TEST 6 FAILED\n\n");
    } else {
        printf("✅ EXTREME TEST 6 PASSED\n\n");
    }
}

/**
 * בדיקה קיצונית 7: Producer-Only Test
 * רק Producers, בלי Consumers
 */
void test_extreme_producer_only() {
    printf("🏭 EXTREME TEST 7: Producer-Only Test\n");
    setup_timeout(10);
    
    consumer_producer_t queue;
    consumer_producer_init(&queue, QUEUE_CAPACITY);
    
    const int NUM_PRODUCERS = 5;
    pthread_t producers[NUM_PRODUCERS];
    thread_data_t prod_data[NUM_PRODUCERS];
    
    // Create producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        prod_data[i] = (thread_data_t){&queue, i + 1, 0, 100, 0};
        pthread_create(&producers[i], NULL, extreme_producer_thread, &prod_data[i]);
    }
    
    // Wait for all producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pthread_join(producers[i], NULL);
    }
    
    // Signal finished
    consumer_producer_signal_finished(&queue);
    
    cleanup_timeout();
    consumer_producer_destroy(&queue);
    
    if (test_failed || test_timeout) {
        printf("❌ EXTREME TEST 7 FAILED\n\n");
    } else {
        printf("✅ EXTREME TEST 7 PASSED\n\n");
    }
}

/**
 * בדיקה קיצונית 8: Consumer-Only Test
 * רק Consumers, בלי Producers
 */
void test_extreme_consumer_only() {
    printf("🛒 EXTREME TEST 8: Consumer-Only Test\n");
    setup_timeout(10);
    
    consumer_producer_t queue;
    consumer_producer_init(&queue, QUEUE_CAPACITY);
    
    const int NUM_CONSUMERS = 5;
    pthread_t consumers[NUM_CONSUMERS];
    thread_data_t cons_data[NUM_CONSUMERS];
    
    // Create consumers
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        cons_data[i] = (thread_data_t){&queue, i + 1, 0, 0, 0};
        pthread_create(&consumers[i], NULL, extreme_consumer_thread, &cons_data[i]);
    }
    
    // Signal finished immediately
    consumer_producer_signal_finished(&queue);
    
    // Wait for all consumers
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        pthread_join(consumers[i], NULL);
    }
    
    cleanup_timeout();
    consumer_producer_destroy(&queue);
    
    if (test_failed || test_timeout) {
        printf("❌ EXTREME TEST 8 FAILED\n\n");
    } else {
        printf("✅ EXTREME TEST 8 PASSED\n\n");
    }
}

int main() {
    printf("🔥🔥🔥 EXTREME CONSUMER-PRODUCER TESTS 🔥🔥🔥\n");
    printf("===============================================\n\n");
    
    srand(time(NULL));
    
    // Reset global state
    test_failed = 0;
    test_timeout = 0;
    
    // Run all extreme tests
    test_extreme_stress();
    test_extreme_memory_pressure();
    test_extreme_rapid_start_stop();
    test_extreme_zero_capacity();
    test_extreme_concurrent_shutdown();
    test_extreme_empty_strings();
    test_extreme_producer_only();
    test_extreme_consumer_only();
    
    printf("===============================================\n");
    if (test_failed || test_timeout) {
        printf("❌ SOME EXTREME TESTS FAILED!\n");
        return 1;
    } else {
        printf("✅ ALL EXTREME TESTS PASSED! 🎉\n");
        return 0;
    }
}
