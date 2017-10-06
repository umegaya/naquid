#include "loop.h"
#include <thread>

const int kThreads = 4;

int main(int argc, char *argv[]){
    std::thread threads[kThreads];

    /* Create and set up the consumer thread */
    for (int i = 0; i < kThreads; i++) {
        threads[i] = std::thread([thread_loop] {
            fprintf(stderr, "thread(%p) start event loop\n", thread_loop);
            uv_run(thread_loop);
            fprintf(stderr, "thread(%p) finishes event loop\n", thread_loop);
        });
    }

    fprintf(stderr, "main thread sleep 5 seconds...\n");
    sleep(5);
    fprintf(stderr, "terminating workers\n");
    for (int i = 0; i < kThreads; i++) {
        uv_async_send(signals + i);        
    }
    fprintf(stderr, "wait terminating workers\n");
    for (int i = 0; i < kThreads; i++) {
        if (threads[i].joinable()) {
            threads[i].join();
        }
    }
    fprintf(stderr, "workers terminated\n");    
    return 0;
}
