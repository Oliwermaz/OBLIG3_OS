#include <stdio.h>     // printf()
#include <stdlib.h>    // atoi() og random()
#include <unistd.h>    // usleep()
#include <stdint.h>    // intptr_t, brukt til å sende trådnummer

// Makroer for tråder og semaforer som også sjekker returverdier.
#include "common_threads.h"

#ifdef linux
#include <semaphore.h>
#elif __APPLE__
#include "zemaphore.h"
#endif

// Øvre grenser for de tilfeldige pausene, målt i mikrosekunder.
#define MAX_MOD_READ 100
#define MAX_MOD_WRITE 100


/*
 * En leser–skriver-lås:
 *
 * - Flere readers kan lese samtidig.
 * - En writer må ha eksklusiv tilgang.
 * - Readers og writers skal ikke bruke de delte dataene samtidig.
 */
typedef struct _rwlock_t {
    // Holdes av enten én writer eller en gruppe readers.
    sem_t writelock;

    // Beskytter telleren readers når tråder melder seg inn eller ut.
    sem_t lock;

    // NY 1: En port som en writer kan stenge for nye readers.
    sem_t turnstile;

    // Antall readers som har meldt seg inn i readergruppen.
    int readers;
} rwlock_t;


/*
 * Initialiserer låsen før trådene starter.
 * Semaforverdien 1 betyr at én tillatelse er tilgjengelig.
 */
void rwlock_init(rwlock_t *lock) {
    lock->readers = 0;

    Sem_init(&lock->lock, 1);
    Sem_init(&lock->writelock, 1);

    // NY 2: Porten starter åpen.
    Sem_init(&lock->turnstile, 1);
}


/*
 * Skaffer lesetilgang.
 *
 * lock er en peker til låsestrukturen.
 * Operatoren -> brukes til å hente feltene i denne strukturen.
 */
void rwlock_acquire_readlock(rwlock_t *lock) {
    // NY 3: Vent dersom en writer holder porten stengt.
    Sem_wait(&lock->turnstile);

    // NY 4: Readeren slipper porten straks den har passert.
    // Dermed kan flere readers passere når ingen writer holder porten.
    Sem_post(&lock->turnstile);

    // Bare én tråd om gangen får endre reader-telleren.
    Sem_wait(&lock->lock);
    lock->readers++;

    // Den første readeren tar writelock på vegne av hele gruppen.
    // Hvis en writer skriver nå, må denne readeren vente.
    if (lock->readers == 1)
        Sem_wait(&lock->writelock);

    // Telleren er oppdatert. Andre readers kan nå melde seg inn.
    Sem_post(&lock->lock);
}


/*
 * Frigir lesetilgang når en reader er ferdig.
 * Denne funksjonen ble ikke endret i løsningen.
 */
void rwlock_release_readlock(rwlock_t *lock) {
    // Beskytt telleren mens denne readeren melder seg ut.
    Sem_wait(&lock->lock);
    lock->readers--;

    // Den siste readeren frigjør writelock.
    // En ventende writer kan da få tilgang.
    if (lock->readers == 0)
        Sem_post(&lock->writelock);

    Sem_post(&lock->lock);
}


/*
 * Skaffer eksklusiv skrivetilgang.
 */
void rwlock_acquire_writelock(rwlock_t *lock) {
    // NY 5: Steng porten før vi venter på tilgang til dataene.
    // Nye readers må da vente ved porten.
    Sem_wait(&lock->turnstile);

    // Vent til aktive readers eller en tidligere writer er ferdige.
    // Readers som allerede er inne, trenger ikke porten for å gå ut.
    Sem_wait(&lock->writelock);
}


/*
 * Frigir skrivetilgang etter at writeren er ferdig med dataene.
 */
void rwlock_release_writelock(rwlock_t *lock) {
    // NY 6: Åpne porten for neste reader eller writer.
    Sem_post(&lock->turnstile);

    // Gjør de delte dataene tilgjengelige igjen.
    Sem_post(&lock->writelock);
}


// Antall runder hver tråd skal utføre.
// Verdien settes før trådene opprettes og endres ikke etterpå.
int loops;

// Den delte verdien: writers øker den, readers leser den.
int value = 0;

// Alle trådene bruker den samme låsestrukturen.
rwlock_t lock;


/*
 * Koden som kjøres av hver reader-tråd.
 * arg inneholder nummeret til readeren.
 */
void *reader(void *arg) {
    int i;

    for (i = 0; i < loops; i++) {
        rwlock_acquire_readlock(&lock);

        // Lesing skjer mens readeren har lesetilgang.
        // Andre readers kan lese samtidig, men ingen writer kan skrive.
        printf("reader %ld reads %d\n", (intptr_t)arg, value);

        // Simulerer at lesingen tar litt tid.
        usleep(random() % MAX_MOD_READ);

        rwlock_release_readlock(&lock);

        // Pause mellom lesingene, etter at lesetilgangen er frigitt.
        usleep(random() % MAX_MOD_READ);
    }

    return NULL;
}


/*
 * Koden som kjøres av hver writer-tråd.
 * arg inneholder nummeret til writeren.
 */
void *writer(void *arg) {
    int i;

    for (i = 0; i < loops; i++) {
        rwlock_acquire_writelock(&lock);

        // Kritisk seksjon: Bare denne writeren får bruke value nå.
        value++;
        printf("writer %ld writes %d\n", (intptr_t)arg, value);

        // Simulerer at skrivingen tar litt tid.
        usleep(random() % MAX_MOD_WRITE);

        rwlock_release_writelock(&lock);

        // Pause mellom skrivingene, etter at låsene er frigitt.
        usleep(random() % MAX_MOD_WRITE);
    }

    return NULL;
}


int main(int argc, char *argv[]) {
    // Programnavnet og tre argumenter gir totalt fire argumenter.
    // Eksempel: ./reader-writer-fair 10 2 10
    assert(argc == 4);

    int num_readers = atoi(argv[1]);
    int num_writers = atoi(argv[2]);
    loops = atoi(argv[3]);

    // Lagrer tråd-ID-ene, slik at main kan vente på trådene senere.
    pthread_t pr[num_readers], pw[num_writers];

    // Initialisering må skje før noen tråder bruker låsen.
    rwlock_init(&lock);

    printf("begin\n");

    // Opprett readerne og gi hver av dem et nummer.
    intptr_t i;
    for (i = 0; i < num_readers; i++)
        Pthread_create(&pr[i], NULL, reader, (void *)i);

    // Opprett writerne og gi hver av dem et nummer.
    for (i = 0; i < num_writers; i++)
        Pthread_create(&pw[i], NULL, writer, (void *)i);

    // Vent på alle trådene før sluttverdien leses.
    // Trådene kjører videre selv om main venter i join().
    for (i = 0; i < num_readers; i++)
        Pthread_join(pr[i], NULL);

    for (i = 0; i < num_writers; i++)
        Pthread_join(pw[i], NULL);

    // Hver writer øker value én gang per runde.
    // For 2 writers og 10 runder forventer vi derfor verdien 20.
    printf("end: value %d\n", value);

    return 0;
}