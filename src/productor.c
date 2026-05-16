/*
 * productor.c
 * Genera "procesos" como threads cada 30-60 s (aleatorio).
 * Cada thread:
 *   1. Pide semáforo de memoria
 *   2. Busca espacio (paginación o segmentación)
 *   3. Escribe en bitácora
 *   4. Devuelve semáforo
 *   5. Duerme el tiempo asignado
 *   6. Pide semáforo, libera memoria, escribe bitácora, devuelve semáforo
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "../include/shared.h"

/* ── Globales accesibles por todos los threads ── */
static int      g_shmid;
static int      g_semid;
static int      g_semlog;
static SharedMem *g_shm;

/* ── Utilidades ─────────────────────────────────────────────── */
static int rand_range(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}

static void escribir_log(const char *mensaje) {
    sem_wait_op(g_semlog, 0);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        time_t t = time(NULL);
        struct tm *ti = localtime(&t);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ti);
        fprintf(f, "[%s] %s\n", buf, mensaje);
        fclose(f);
    }
    sem_signal_op(g_semlog, 0);
}

/* ── Buscar slot libre en la tabla de procesos ── */
static int buscar_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (g_shm->procs[i].estado == PROC_LIBRE ||
            g_shm->procs[i].estado == PROC_TERMINADO ||
            g_shm->procs[i].estado == PROC_MUERTO)
            return i;
    return -1;
}

/* ── PAGINACIÓN: buscar N páginas libres ── */
static int asignar_paginas(ProcEntry *pe) {
    int encontradas = 0;
    int indices[10];
    int total = g_shm->total_paginas;

    for (int i = 0; i < total && encontradas < pe->num_paginas; i++) {
        if (g_shm->marcos[i] == 0) {
            indices[encontradas++] = i;
        }
    }
    if (encontradas < pe->num_paginas) return 0; /* no hay espacio */

    for (int i = 0; i < pe->num_paginas; i++) {
        g_shm->marcos[indices[i]] = pe->pid;
        pe->paginas[i] = indices[i];
    }
    return 1;
}

/* ── SEGMENTACIÓN: buscar bloques contiguos para cada segmento ── */
static int asignar_segmentos(ProcEntry *pe) {
    int total = g_shm->total_paginas;

    for (int s = 0; s < pe->num_segmentos; s++) {
        int tam  = pe->tam_segmento[s];
        int found = 0;

        for (int i = 0; i <= total - tam && !found; i++) {
            int libre = 1;
            for (int j = i; j < i + tam; j++)
                if (g_shm->marcos[j] != 0) { libre = 0; break; }

            if (libre) {
                for (int j = i; j < i + tam; j++)
                    g_shm->marcos[j] = pe->pid;
                pe->seg_inicio[s] = i;
                found = 1;
            }
        }
        if (!found) {
            /* Revertir segmentos ya asignados */
            for (int k = 0; k < s; k++)
                for (int j = pe->seg_inicio[k];
                         j < pe->seg_inicio[k] + pe->tam_segmento[k]; j++)
                    g_shm->marcos[j] = 0;
            return 0;
        }
    }
    return 1;
}

/* ── Liberar memoria de un proceso ── */
static void liberar_memoria(ProcEntry *pe) {
    if (pe->algoritmo == ALG_PAGINACION) {
        for (int i = 0; i < pe->num_paginas; i++)
            g_shm->marcos[pe->paginas[i]] = 0;
    } else {
        for (int s = 0; s < pe->num_segmentos; s++)
            for (int j = pe->seg_inicio[s];
                     j < pe->seg_inicio[s] + pe->tam_segmento[s]; j++)
                g_shm->marcos[j] = 0;
    }
}

/* ── Thread de proceso ─────────────────────────────────────────*/
static void *thread_proceso(void *arg) {
    (void)arg;
    char logbuf[512];
    pid_t tid = getpid(); /* usamos PID del proceso + pthread_self para ID */
    unsigned long tid_short = (unsigned long)pthread_self() % 100000;

    /* Buscar slot y preparar entrada */
    sem_wait_op(g_semid, 0);               /* ── ENTRADA REGIÓN CRÍTICA ── */

    int slot = buscar_slot();
    if (slot < 0) {
        sem_signal_op(g_semid, 0);
        snprintf(logbuf, sizeof(logbuf),
            "PROCESO tid=%lu SIN SLOT disponible. Murió.", tid_short);
        escribir_log(logbuf);
        printf("  [tid=%lu] Sin slot en tabla. Proceso muere.\n", tid_short);
        return NULL;
    }

    ProcEntry *pe = &g_shm->procs[slot];
    memset(pe, 0, sizeof(ProcEntry));
    pe->pid       = tid;                   /* PID del proceso padre */
    pe->algoritmo = g_shm->algoritmo;
    pe->estado    = PROC_BUSCANDO;

    if (pe->algoritmo == ALG_PAGINACION) {
        pe->num_paginas  = rand_range(1, 10);
        pe->tiempo_sleep = rand_range(20, 60);
    } else {
        pe->num_segmentos = rand_range(1, 5);
        for (int i = 0; i < pe->num_segmentos; i++)
            pe->tam_segmento[i] = rand_range(1, 3);
        pe->tiempo_sleep = rand_range(20, 60);
    }

    printf("  [tid=%lu] Buscando espacio (%s)...\n",
           tid_short,
           pe->algoritmo == ALG_PAGINACION ? "paginación" : "segmentación");

    /* Intentar asignar */
    int ok;
    if (pe->algoritmo == ALG_PAGINACION)
        ok = asignar_paginas(pe);
    else
        ok = asignar_segmentos(pe);

    if (!ok) {
        pe->estado = PROC_MUERTO;
        sem_signal_op(g_semid, 0);         /* ── SALIDA REGIÓN CRÍTICA ── */

        snprintf(logbuf, sizeof(logbuf),
            "PROCESO tid=%lu PID=%d | ACCIÓN=RECHAZADO | Sin espacio suficiente | %s",
            tid_short, tid,
            pe->algoritmo == ALG_PAGINACION ? "Paginación" : "Segmentación");
        escribir_log(logbuf);
        printf("  [tid=%lu] Sin espacio. Proceso muere.\n", tid_short);
        return NULL;
    }

    /* Registrar asignación en bitácora */
    if (pe->algoritmo == ALG_PAGINACION) {
        char marcos_str[256] = "marcos=[";
        for (int i = 0; i < pe->num_paginas; i++) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%d", pe->paginas[i]);
            strcat(marcos_str, tmp);
            if (i < pe->num_paginas - 1) strcat(marcos_str, ",");
        }
        strcat(marcos_str, "]");
        snprintf(logbuf, sizeof(logbuf),
            "PROCESO tid=%lu PID=%d | ACCIÓN=ASIGNACIÓN | Tipo=Paginación | Páginas=%d | %s | Tiempo=%ds",
            tid_short, tid, pe->num_paginas, marcos_str, pe->tiempo_sleep);
    } else {
        char segs_str[256] = "segmentos=[";
        for (int i = 0; i < pe->num_segmentos; i++) {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "(inicio=%d,tam=%d)",
                     pe->seg_inicio[i], pe->tam_segmento[i]);
            strcat(segs_str, tmp);
            if (i < pe->num_segmentos - 1) strcat(segs_str, ",");
        }
        strcat(segs_str, "]");
        snprintf(logbuf, sizeof(logbuf),
            "PROCESO tid=%lu PID=%d | ACCIÓN=ASIGNACIÓN | Tipo=Segmentación | Segmentos=%d | %s | Tiempo=%ds",
            tid_short, tid, pe->num_segmentos, segs_str, pe->tiempo_sleep);
    }
    escribir_log(logbuf);

    pe->estado = PROC_EN_MEMORIA;
    printf("  [tid=%lu] ✔ Asignado. Durmiendo %d s...\n", tid_short, pe->tiempo_sleep);
    sem_signal_op(g_semid, 0);             /* ── SALIDA REGIÓN CRÍTICA ── */

    /* ── Sleep del proceso ── */
    sleep(pe->tiempo_sleep);

    /* ── Liberar memoria (región crítica) ── */
    pe->estado = PROC_BLOQUEADO;
    sem_wait_op(g_semid, 0);              /* ── ENTRADA REGIÓN CRÍTICA ── */

    pe->estado = PROC_EN_MEMORIA;         /* marca que está liberando    */
    liberar_memoria(pe);

    snprintf(logbuf, sizeof(logbuf),
        "PROCESO tid=%lu PID=%d | ACCIÓN=DESASIGNACIÓN | Tipo=%s | Memoria liberada",
        tid_short, tid,
        pe->algoritmo == ALG_PAGINACION ? "Paginación" : "Segmentación");
    escribir_log(logbuf);

    pe->estado = PROC_TERMINADO;
    printf("  [tid=%lu] ✔ Memoria liberada. Proceso terminado.\n", tid_short);
    sem_signal_op(g_semid, 0);            /* ── SALIDA REGIÓN CRÍTICA ── */

    return NULL;
}

/* ─────────────────────────────────────────────────────────────── */
int main(void) {
    srand((unsigned)time(NULL));

    /* ── Conectar a memoria compartida ── */
    g_shmid = shmget(SHM_KEY, sizeof(SharedMem), 0666);
    if (g_shmid < 0) {
        perror("shmget: Ejecute primero el inicializador");
        exit(EXIT_FAILURE);
    }
    g_shm = (SharedMem *)shmat(g_shmid, NULL, 0);
    if (g_shm == (void *)-1) { perror("shmat"); exit(EXIT_FAILURE); }

    if (!g_shm->inicializado) {
        fprintf(stderr, "Error: memoria no inicializada.\n");
        exit(EXIT_FAILURE);
    }

    /* ── Obtener semáforos ── */
    g_semid  = semget(SEM_KEY,     1, 0666);
    g_semlog = semget(SEM_LOG_KEY, 1, 0666);
    if (g_semid < 0 || g_semlog < 0) {
        perror("semget: semáforos no encontrados");
        exit(EXIT_FAILURE);
    }

    /* ── Elegir algoritmo ── */
    int alg = 0;
    while (alg != ALG_PAGINACION && alg != ALG_SEGMENTACION) {
        printf("Algoritmo: [1] Paginación  [2] Segmentación: ");
        if (scanf("%d", &alg) != 1) alg = 0;
    }
    g_shm->algoritmo = alg;
    printf("\nUsando: %s\n", alg == ALG_PAGINACION ? "Paginación" : "Segmentación");
    printf("Produciendo procesos (Ctrl+C para terminar)...\n\n");

    char logbuf[128];
    snprintf(logbuf, sizeof(logbuf),
        "PRODUCTOR PID=%d | Algoritmo=%s | Iniciado",
        (int)getpid(),
        alg == ALG_PAGINACION ? "Paginación" : "Segmentación");
    /* usamos la función inline con semáforos ya disponibles */
    sem_wait_op(g_semlog, 0);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        time_t t = time(NULL);
        struct tm *ti = localtime(&t);
        char tbuf[32];
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", ti);
        fprintf(f, "[%s] %s\n", tbuf, logbuf);
        fclose(f);
    }
    sem_signal_op(g_semlog, 0);

    /* ── Bucle principal: generar threads ── */
    while (!g_shm->finalizar) {
        int intervalo = rand_range(30, 60);
        printf("[Productor] Próximo proceso en %d s...\n", intervalo);

        /* Esperar en intervalos de 1 s para detectar señal de fin */
        for (int i = 0; i < intervalo; i++) {
            sleep(1);
            if (g_shm->finalizar) break;
        }
        if (g_shm->finalizar) break;

        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_proceso, NULL) != 0) {
            perror("pthread_create");
        } else {
            pthread_detach(tid);   /* no esperamos; el thread se limpia solo */
        }
    }

    printf("[Productor] Señal de finalización recibida. Saliendo.\n");
    shmdt(g_shm);
    return 0;
}
