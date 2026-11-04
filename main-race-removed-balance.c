#include <stdio.h>

#include "common_threads.h"

int balance = 0;

void* worker(void* arg) {
    // balance++; Fjernet økning.
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t p;
    Pthread_create(&p, NULL, worker, NULL);
    balance++;
    Pthread_join(p, NULL);
    return 0;
}
