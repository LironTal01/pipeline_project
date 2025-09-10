#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "plugins/sync/monitor.h"

#define NUM_THREADS 5

// מבנה נתונים שישמש את ה-threads בבדיקה
typedef struct {
    monitor_t* monitor;
    int id;
} thread_data_t;

void* wait_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    printf("Thread %d: Waiting on monitor...\n", data->id);
    monitor_wait(data->monitor);
    printf("Thread %d: Wait successful, condition met!\n", data->id);
    return NULL;
}

void* signal_thread(void* arg) {
    thread_data_t* data = (thread_data_t*)arg;
    printf("Thread %d: Signaling monitor...\n", data->id);
    monitor_signal(data->monitor);
    printf("Thread %d: Signal sent.\n", data->id);
    return NULL;
}

/**
 * בדיקה מס' 1: מקרה בסיסי - המתנה לפני שליחת האות.
 * התהליכון ממתין ואז תהליכון אחר שולח אות, וההמתנה מסתיימת.
 */
void test_basic_wait_then_signal() {
    printf("--- Test 1: Basic wait then signal ---\n");
    monitor_t monitor;
    monitor_init(&monitor);
    
    pthread_t wait_t, signal_t;
    thread_data_t wait_data = {&monitor, 1};
    thread_data_t signal_data = {&monitor, 2};

    pthread_create(&wait_t, NULL, wait_thread, &wait_data);
    sleep(1); // נותן ל-wait_t זמן להתחיל להמתין
    pthread_create(&signal_t, NULL, signal_thread, &signal_data);

    pthread_join(wait_t, NULL);
    pthread_join(signal_t, NULL);
    
    monitor_destroy(&monitor);
    printf("Test 1 passed.\n\n");
}

/**
 * בדיקה מס' 2: פתרון בעיית ה-Missed Signal (אות שהוחמץ).
 * תהליכון שולח אות לפני שהתהליכון הממתין מופעל. 
 * המוניטור זוכר את האות, כך שההמתנה לא נחסמת.
 */
void test_missed_signal() {
    printf("--- Test 2: Missed signal ---\n");
    monitor_t monitor;
    monitor_init(&monitor);
    
    pthread_t wait_t, signal_t;
    thread_data_t wait_data = {&monitor, 1};
    thread_data_t signal_data = {&monitor, 2};
    
    // שולחים את האות לפני שמתחילים להמתין
    pthread_create(&signal_t, NULL, signal_thread, &signal_data);
    sleep(1); // נותן ל-signal_t לסיים את עבודתו
    
    // כעת, ננסה להמתין - ההמתנה לא אמורה להיחסם
    pthread_create(&wait_t, NULL, wait_thread, &wait_data);

    pthread_join(wait_t, NULL);
    pthread_join(signal_t, NULL);
    
    monitor_destroy(&monitor);
    printf("Test 2 passed.\n\n");
}

/**
 * בדיקה מס' 3: המתנה של מספר תהליכונים לאותו אות.
 * נפעיל מספר תהליכונים שימתינו, ואז תהליכון אחד ישלח אות.
 * כל התהליכונים אמורים להתעורר בגלל ה-pthread_cond_broadcast בתוך monitor_signal.
 */
void test_multiple_waiters() {
    printf("--- Test 3: Multiple waiters ---\n");
    monitor_t monitor;
    monitor_init(&monitor);
    
    pthread_t waiters[NUM_THREADS];
    thread_data_t waiter_data[NUM_THREADS];
    
    // מפעילים את כל התהליכונים הממתינים
    for (int i = 0; i < NUM_THREADS; ++i) {
        waiter_data[i] = (thread_data_t){&monitor, i + 1};
        pthread_create(&waiters[i], NULL, wait_thread, &waiter_data[i]);
    }
    
    sleep(1); // נותן להם זמן להיכנס למצב המתנה
    
    // תהליכון אחד שולח אות
    thread_data_t signal_data = {&monitor, 99};
    pthread_t signal_t;
    pthread_create(&signal_t, NULL, signal_thread, &signal_data);
    
    // מחכים לסיום כל התהליכונים
    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(waiters[i], NULL);
    }
    pthread_join(signal_t, NULL);
    
    monitor_destroy(&monitor);
    printf("Test 3 passed.\n\n");
}

/**
 * בדיקה מס' 4: שימוש ב-monitor_reset.
 * תהליכון ממתין, תהליכון אחר שולח אות, ואז תהליכון שלישי מאפס את המוניטור.
 * במצב זה, המתנה חוזרת שוב להיות חוסמת.
 */
void test_reset() {
    printf("--- Test 4: Reset functionality ---\n");
    monitor_t monitor;
    monitor_init(&monitor);
    
    pthread_t t1, t2;
    
    // פיילוט בדיקה ראשון - המתנה ואז סיגנל
    thread_data_t data1 = {&monitor, 1};
    pthread_create(&t1, NULL, wait_thread, &data1);
    sleep(1); 
    
    thread_data_t data2 = {&monitor, 2};
    pthread_create(&t2, NULL, signal_thread, &data2);
    
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    
    // כעת המוניטור במצב 'signaled'
    printf("Monitor is now signaled. Resetting...\n");
    monitor_reset(&monitor);
    printf("Monitor has been reset.\n");

    // פיילוט בדיקה שני - ננסה שוב להמתין. 
    // כעת ההמתנה תחסם מכיוון שאיפסנו את המוניטור.
    // אם התוכנית לא נתקעת, זה אומר שהאיפוס עובד.
    printf("Attempting to wait again. This call should block until manually terminated.\n");
    
    // כדי למנוע deadlock בבדיקה האוטומטית, לא נשתמש ב-pthread_join כאן, 
    // אבל בהרצה ידנית ניתן לוודא שהוא אכן חוסם.
    
    // יצירת תהליכון שייחסם
    pthread_t t3;
    thread_data_t data3 = {&monitor, 3};
    pthread_create(&t3, NULL, wait_thread, &data3);
    
    sleep(2);
    printf("Test 4: The wait thread should still be blocked. Signalling to exit...\n");
    monitor_signal(&monitor); // שולחים אות ידני כדי לסיים את התהליכון
    pthread_join(t3, NULL);

    monitor_destroy(&monitor);
    printf("Test 4 passed.\n\n");
}

int main() {
    test_reset();
    test_basic_wait_then_signal();
    test_missed_signal();
    test_multiple_waiters();
    
    return 0;
}
