/*
 * inicializador.c
 * Crea la memoria compartida y los semáforos.
 * Pide al usuario cuántas páginas tendrá la memoria.
 * Muere al terminar.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>
#include <unistd.h>
#include "../include/shared.h"

/* Escribe en la bitácora (sin semáforo, nadie más corre aún) */
static void log_event(const char *msg) {
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(f, "[%s] INICIALIZADOR (PID=%d): %s\n", buf, (int)getpid(), msg);
    fclose(f);
}

int main(void) {
    printf("╔══════════════════════════════════════╗\n");
    printf("║   INICIALIZADOR - Proyecto Memoria   ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    /* ── 1. Pedir cantidad de páginas ── */
    int total_paginas;
    do {
        printf("Ingrese la cantidad de marcos de página [8-%d]: ", MEM_TOTAL_PAGES);
        if (scanf("%d", &total_paginas) != 1) { total_paginas = 0; }
    } while (total_paginas < 8 || total_paginas > MEM_TOTAL_PAGES);

    /* ── 2. Crear / obtener memoria compartida ── */
    int shmid = shmget(SHM_KEY,
                       sizeof(SharedMem),
                       IPC_CREAT | IPC_EXCL | 0666);
    if (shmid < 0) {
        perror("shmget: ¿Ya existe una simulación en curso? Ejecute finalizador primero");
        exit(EXIT_FAILURE);
    }

    SharedMem *shm = (SharedMem *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    /* ── 3. Inicializar memoria compartida ── */
    memset(shm, 0, sizeof(SharedMem));
    shm->total_paginas = total_paginas;
    shm->algoritmo     = 0;      /* lo elige el Productor */
    shm->inicializado  = 1;
    shm->finalizar     = 0;

    for (int i = 0; i < MEM_TOTAL_PAGES; i++)
        shm->marcos[i] = 0;      /* 0 = libre */

    for (int i = 0; i < MAX_PROCESSES; i++)
        shm->procs[i].estado = PROC_LIBRE;

    shmdt(shm);

    /* ── 4. Crear semáforo de región crítica (1 recurso) ── */
    int semid = semget(SEM_KEY, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (semid < 0) {
        perror("semget SEM_KEY");
        /* limpiamos shm antes de salir */
        shmctl(shmget(SHM_KEY, 0, 0666), IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }
    union semun su;
    su.val = 1;
    semctl(semid, 0, SETVAL, su);

    /* ── 5. Crear semáforo para la bitácora ── */
    int semlog = semget(SEM_LOG_KEY, 1, IPC_CREAT | IPC_EXCL | 0666);
    if (semlog < 0) {
        perror("semget SEM_LOG_KEY");
        semctl(semid, 0, IPC_RMID);
        shmctl(shmget(SHM_KEY, 0, 0666), IPC_RMID, NULL);
        exit(EXIT_FAILURE);
    }
    su.val = 1;
    semctl(semlog, 0, SETVAL, su);

    /* ── 6. Crear / limpiar bitácora ── */
    FILE *f = fopen(LOG_FILE, "w");
    if (f) {
        fprintf(f, "=== BITÁCORA SIMULACIÓN PAGINACIÓN/SEGMENTACIÓN ===\n");
        fclose(f);
    }

    log_event("Memoria compartida creada.");
    log_event("Semáforos creados.");

    printf("\n✔ Memoria compartida creada  (shmid=%d, %d marcos)\n", shmid, total_paginas);
    printf("✔ Semáforo región crítica    (semid=%d)\n", semid);
    printf("✔ Semáforo bitácora          (semid=%d)\n", semlog);
    printf("✔ Bitácora inicializada      (%s)\n\n", LOG_FILE);
    printf("Listo. Ahora ejecute: ./productor\n");

    return 0;
}
