#include <stdio.h>
#include "../../plugins/sync/consumer_producer.h"

int main() {
    consumer_producer_t queue;
    consumer_producer_init(&queue, 0);
    
    printf("Testing zero capacity queue...\n");
    
    const char* result = consumer_producer_put(&queue, "test");
    printf("Put result: %s\n", result ? result : "NULL");
    
    char* item = consumer_producer_get(&queue);
    printf("Get result: %s\n", item ? item : "NULL");
    
    consumer_producer_destroy(&queue);
    return 0;
}
