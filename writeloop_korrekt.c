#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int g_ant = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *writeloop(void *arg) {
    (void)arg;

    while (1) {
        pthread_mutex_lock(&lock);

        if (g_ant >= 10) {
            pthread_mutex_unlock(&lock);
            break;
        }

        g_ant++;
        usleep(rand() % 10);
        printf("%d\n", g_ant);

        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

int main(void) {
    pthread_t tid;
    pthread_create(&tid, NULL, writeloop, NULL);

    writeloop(NULL);

    pthread_join(tid, NULL);
    pthread_mutex_destroy(&lock);

    return 0;
}