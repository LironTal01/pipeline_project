#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "plugins/sync/consumer_producer.h"

#define NUM_PRODUCERS 2
#define NUM_CONSUMERS 2
#define NUM_ITEMS 10
#define QUEUE_CAPACITY 5

// מבנה נתונים שישמש את התהליכונים
typedef struct {
    consumer_producer_t* queue;
    int id;
} thread_data_t;

// פונקציה לתהליכון Producer
void* producer_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    for (int i = 0; i < NUM_ITEMS; ++i) {
        char buffer[50];
        // יצירת מחרוזת ייחודית
        sprintf(buffer, "Item %d from producer %d", i, data->id);
        const char* err = consumer_producer_put(data->queue, buffer);
        if (err) {
            fprintf(stderr, "Producer %d failed to put item %d: %s\n", data->id, i, err);
            break;
        }
        // הדפסה לבדיקה ויזואלית
        printf("Producer %d: Put '%s'\n", data->id, buffer);
    }
    printf("Producer %d: Finished producing.\n", data->id);
    return NULL;
}

// פונקציה לתהליכון Consumer
void* consumer_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    char* item;
    // לולאה שתסתיים רק כאשר התור ריק וסומן כ'finished'
    while ((item = consumer_producer_get(data->queue)) != NULL) {
        printf("Consumer %d: Got '%s'\n", data->id, item);
        free(item); // שחרור הזיכרון שהתקבל
    }
    printf("Consumer %d: Queue is finished and empty. Exiting.\n", data->id);
    return NULL;
}

/**
 * בדיקה מס' 1: Happy Path - Producers and Consumers working together.
 * מפעילה מספר Producers ו-Consumers במקביל. התור אמור לטפל בהם בצורה בטוחה, 
 * לחסום את ה-Producers כאשר הוא מלא ואת ה-Consumers כאשר הוא ריק.
 */
void test_basic_producer_consumer() {
    printf("--- Test 1: Basic Producer-Consumer ---\n");
    consumer_producer_t queue;
    consumer_producer_init(&queue, QUEUE_CAPACITY);

    pthread_t producers[NUM_PRODUCERS];
    pthread_t consumers[NUM_CONSUMERS];
    thread_data_t prod_data[NUM_PRODUCERS];
    thread_data_t cons_data[NUM_CONSUMERS];

    // יצירת תהליכוני Producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        prod_data[i] = (thread_data_t){&queue, i + 1};
        pthread_create(&producers[i], NULL, producer_thread, &prod_data[i]);
    }

    // יצירת תהליכוני Consumers
    // התיקון הקריטי כאן: יוצרים את ה-consumers לפני שמחכים ל-producers.
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        cons_data[i] = (thread_data_t){&queue, i + 1};
        pthread_create(&consumers[i], NULL, consumer_thread, &cons_data[i]);
    }

    // המתנה לסיום כל ה-Producers
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        pthread_join(producers[i], NULL);
    }

    // שליחת אות סיום לתור לאחר שה-Producers סיימו
    consumer_producer_signal_finished(&queue);

    // המתנה לסיום כל ה-Consumers
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        pthread_join(consumers[i], NULL);
    }
    
    consumer_producer_destroy(&queue);
    printf("Test 1 passed.\n\n");
}

/**
 * בדיקה מס' 2: Empty Queue - Blocking get.
 * מפעילה תהליכון Consumer יחיד כאשר התור ריק. הוא אמור להיחסם, ואז Producer יחיד
 * מופעל ומוסיף פריט אחד. הבדיקה מוודאת שה-Consumer מתעורר כראוי.
 */
void test_get_on_empty_queue() {
    printf("--- Test 2: Get on empty queue (blocking) ---\n");
    consumer_producer_t queue;
    consumer_producer_init(&queue, 1);

    pthread_t consumer_t;
    thread_data_t consumer_data = {&queue, 1};

    pthread_create(&consumer_t, NULL, consumer_thread, &consumer_data);
    sleep(1); // נותן ל-consumer להיחסם
    
    // מוסיף פריט אחד, מה שאמור לעורר את ה-consumer
    consumer_producer_put(&queue, "Hello, world!");

    // מסיים את התהליך באופן מסודר
    consumer_producer_signal_finished(&queue);
    pthread_join(consumer_t, NULL);

    consumer_producer_destroy(&queue);
    printf("Test 2 passed.\n\n");
}

/**
 * בדיקה מס' 3: Full Queue - Blocking put.
 * מוסיפה פריטים לתור עד שהוא מלא, ואז מפעילה Producer נוסף שאמור להיחסם.
 * לאחר מכן, Consumer בודד מוציא פריט, מה שאמור לשחרר את ה-Producer החסום.
 */
void test_put_on_full_queue() {
    printf("--- Test 3: Put on full queue (blocking) ---\n");
    consumer_producer_t queue;
    consumer_producer_init(&queue, QUEUE_CAPACITY);

    // ממלאים את התור עד הסוף
    for (int i = 0; i < QUEUE_CAPACITY; ++i) {
        consumer_producer_put(&queue, "Filler");
    }

    pthread_t consumer_t, producer_t;
    thread_data_t consumer_data = {&queue, 1};
    thread_data_t producer_data = {&queue, 2};

    pthread_create(&producer_t, NULL, producer_thread, &producer_data); // אמור להיחסם
    sleep(1); // נותן ל-producer להיחסם

    pthread_create(&consumer_t, NULL, consumer_thread, &consumer_data); // אמור לשחרר את ה-producer
    
    // מסיים את התהליך באופן מסודר
    consumer_producer_signal_finished(&queue);
    pthread_join(producer_t, NULL);
    pthread_join(consumer_t, NULL);
    
    consumer_producer_destroy(&queue);
    printf("Test 3 passed.\n\n");
}

/**
 * בדיקה מס' 4: Graceful Shutdown.
 * שולחת אות סיום לתור ריק. מוודאת ש-Consumer הממתין מקבל NULL ויוצא.
 */
void test_graceful_shutdown() {
    printf("--- Test 4: Graceful shutdown on empty queue ---\n");
    consumer_producer_t queue;
    consumer_producer_init(&queue, 5);

    pthread_t consumer_t;
    thread_data_t consumer_data = {&queue, 1};
    
    pthread_create(&consumer_t, NULL, consumer_thread, &consumer_data);
    sleep(1); // נותן ל-consumer להיחסם

    // שולח אות סיום בזמן שה-consumer חסום
    consumer_producer_signal_finished(&queue);
    
    // מוודא שה-consumer יוצא
    pthread_join(consumer_t, NULL);
    
    consumer_producer_destroy(&queue);
    printf("Test 4 passed.\n\n");
}

/**
 * בדיקה מס' 5: יציאה לאחר הוספת פריטים וסיום.
 * מוסיפה כמה פריטים לתור, ואז שולחת אות סיום. מוודא שה-Consumer מסיים את
 * כל הפריטים שנותרו לפני שהוא יוצא.
 */
void test_shutdown_with_remaining_items() {
    printf("--- Test 5: Shutdown with remaining items ---\n");
    consumer_producer_t queue;
    consumer_producer_init(&queue, 5);

    // מוסיף כמה פריטים
    consumer_producer_put(&queue, "Item A");
    consumer_producer_put(&queue, "Item B");

    pthread_t consumer_t;
    thread_data_t consumer_data = {&queue, 1};
    pthread_create(&consumer_t, NULL, consumer_thread, &consumer_data);
    
    // שולח אות סיום
    consumer_producer_signal_finished(&queue);
    
    // מוודא שה-consumer מסיים את כל הפריטים ויוצא
    pthread_join(consumer_t, NULL);
    
    consumer_producer_destroy(&queue);
    printf("Test 5 passed.\n\n");
}

int main() {
    test_basic_producer_consumer();
    test_get_on_empty_queue();
    test_put_on_full_queue();
    test_graceful_shutdown();
    test_shutdown_with_remaining_items();
    return 0;
}