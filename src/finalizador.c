/*
 * finalizador.c
 * Señaliza al Productor para que se detenga.
 * Libera la memoria compartida y los semáforos.
 * Cierra la bitácora con un mensaje final.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include "../include/shared.h"

static void cerrar_bitacora(void) {
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;
    time_t t = time(NULL);
    struct tm *ti = localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ti);
    fprintf(f, "[%s] FINALIZADOR (PID=%d): Simulación terminada. Recursos liberados.\n",
            buf, (int)getpid());
    fprintf(f, "=== FIN DE LA BITÁCORA ===\n");
    fclose(f);
    printf("✔ Bitácora cerrada (%s)\n", LOG_FILE);
}

int main(void) {
    printf("╔══════════════════════════════════════╗\n");
    printf("║   FINALIZADOR - Proyecto Memoria     ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    /* ── 1. Obtener memoria compartida ── */
    int shmid = shmget(SHM_KEY, sizeof(SharedMem), 0666);
    if (shmid < 0) {
        fprintf(stderr, "No se encontró la memoria compartida. "
                        "¿Está corriendo la simulación?\n");
        exit(EXIT_FAILURE);
    }

    SharedMem *shm = (SharedMem *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); exit(EXIT_FAILURE); }

    /* ── 2. Señalizar al Productor que debe detenerse ── */
    shm->finalizar = 1;
    printf("✔ Señal de finalización enviada al Productor.\n");

    /* Pequeña pausa para que el Productor procese la señal */
    sleep(2);

    /* ── 3. Mostrar resumen final ── */
    printf("\nResumen de la simulación:\n");
    int buscando = 0, en_mem = 0, bloqueados = 0, muertos = 0, terminados = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        switch (shm->procs[i].estado) {
            case PROC_BUSCANDO:   buscando++;   break;
            case PROC_EN_MEMORIA: en_mem++;     break;
            case PROC_BLOQUEADO:  bloqueados++; break;
            case PROC_MUERTO:     muertos++;    break;
            case PROC_TERMINADO:  terminados++; break;
            default: break;
        }
    }
    printf("  En memoria al finalizar : %d\n", en_mem);
    printf("  Buscando espacio        : %d\n", buscando);
    printf("  Bloqueados              : %d\n", bloqueados);
    printf("  Muertos (sin espacio)   : %d\n", muertos);
    printf("  Terminados normalmente  : %d\n", terminados);

    /* ── 4. Cerrar bitácora ── */
    int semlog = semget(SEM_LOG_KEY, 1, 0666);
    if (semlog >= 0) {
        sem_wait_op(semlog, 0);
        cerrar_bitacora();
        sem_signal_op(semlog, 0);
    } else {
        cerrar_bitacora();  /* intentar igual */
    }

    /* ── 5. Liberar semáforos ── */
    int semid = semget(SEM_KEY, 1, 0666);
    if (semid >= 0) {
        if (semctl(semid, 0, IPC_RMID) == 0)
            printf("✔ Semáforo de región crítica eliminado.\n");
        else
            perror("semctl IPC_RMID SEM_KEY");
    }
    if (semlog >= 0) {
        if (semctl(semlog, 0, IPC_RMID) == 0)
            printf("✔ Semáforo de bitácora eliminado.\n");
        else
            perror("semctl IPC_RMID SEM_LOG_KEY");
    }

    /* ── 6. Liberar memoria compartida ── */
    shmdt(shm);
    if (shmctl(shmid, IPC_RMID, NULL) == 0)
        printf("✔ Memoria compartida liberada.\n");
    else
        perror("shmctl IPC_RMID");

    printf("\nSimulación finalizada correctamente.\n");
    return 0;
}
