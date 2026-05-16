/*
 * espia.c
 * Programa interactivo que puede correr en cualquier momento.
 * Muestra estado de la memoria y de los procesos.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "../include/shared.h"

/* ── Colores ANSI ── */
#define ANSI_RESET   "\x1b[0m"
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[33m"
#define ANSI_CYAN    "\x1b[36m"
#define ANSI_BOLD    "\x1b[1m"

static SharedMem *g_shm;

/* ── Mostrar mapa de memoria ─────────────────────────────────── */
static void mostrar_memoria(void) {
    int total = g_shm->total_paginas;
    int alg   = g_shm->algoritmo;

    printf("\n" ANSI_BOLD "═══ ESTADO DE LA MEMORIA (%s) ══════════════" ANSI_RESET "\n",
           alg == ALG_PAGINACION ? "Paginación" : "Segmentación");
    printf("Marcos totales: %d\n\n", total);

    /* Mapa visual de marcos */
    printf("Mapa de marcos (■=ocupado, □=libre):\n");
    for (int i = 0; i < total; i++) {
        if (i % 16 == 0) printf("  [%02d] ", i);
        if (g_shm->marcos[i] == 0)
            printf(ANSI_GREEN "□" ANSI_RESET " ");
        else
            printf(ANSI_RED "■" ANSI_RESET " ");
        if ((i + 1) % 16 == 0 || i == total - 1) printf("\n");
    }

    /* Tabla detallada por proceso */
    printf("\nDetalle por proceso:\n");
    printf("  %-10s %-12s %-12s %s\n", "PID", "Tipo", "Estado", "Marcos/Segmentos");
    printf("  %-10s %-12s %-12s %s\n", "---", "----", "------", "----------------");

    int alguno = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        ProcEntry *pe = &g_shm->procs[i];
        if (pe->estado == PROC_LIBRE ||
            pe->estado == PROC_TERMINADO ||
            pe->estado == PROC_MUERTO)
            continue;

        alguno = 1;
        const char *est_str;
        const char *color;
        switch (pe->estado) {
            case PROC_BUSCANDO:   est_str = "Buscando";   color = ANSI_YELLOW; break;
            case PROC_EN_MEMORIA: est_str = "En memoria"; color = ANSI_GREEN;  break;
            case PROC_BLOQUEADO:  est_str = "Bloqueado";  color = ANSI_RED;    break;
            default:              est_str = "?";          color = ANSI_RESET;  break;
        }

        char detalle[256] = "";
        if (pe->algoritmo == ALG_PAGINACION) {
            snprintf(detalle, sizeof(detalle), "páginas=%d [", pe->num_paginas);
            for (int j = 0; j < pe->num_paginas; j++) {
                char tmp[8];
                snprintf(tmp, sizeof(tmp), "%d", pe->paginas[j]);
                strcat(detalle, tmp);
                if (j < pe->num_paginas - 1) strcat(detalle, ",");
            }
            strcat(detalle, "]");
        } else {
            snprintf(detalle, sizeof(detalle), "segs=%d ", pe->num_segmentos);
            for (int j = 0; j < pe->num_segmentos; j++) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "(ini=%d,tam=%d)",
                         pe->seg_inicio[j], pe->tam_segmento[j]);
                strcat(detalle, tmp);
            }
        }

        printf("  %-10d %-12s %s%-12s" ANSI_RESET " %s\n",
               pe->pid,
               pe->algoritmo == ALG_PAGINACION ? "Paginación" : "Segmentación",
               color, est_str, detalle);
    }
    if (!alguno) printf("  (No hay procesos activos en memoria)\n");

    /* Estadísticas */
    int libres = 0;
    for (int i = 0; i < total; i++)
        if (g_shm->marcos[i] == 0) libres++;
    printf("\nMarcos libres: %d / %d  (%.1f%% libre)\n\n",
           libres, total, 100.0 * libres / total);
}

/* ── Mostrar estado de procesos ──────────────────────────────── */
static void mostrar_procesos(void) {
    printf("\n" ANSI_BOLD "═══ ESTADO DE LOS PROCESOS ═════════════════" ANSI_RESET "\n");

    /* Buscando (en región crítica) */
    printf(ANSI_YELLOW "\n► En región crítica (buscando espacio):\n" ANSI_RESET);
    int alguno = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_shm->procs[i].estado == PROC_BUSCANDO) {
            printf("  PID=%d  tid=%d\n",
                   g_shm->procs[i].pid, g_shm->procs[i].pid);
            alguno = 1;
        }
    }
    if (!alguno) printf("  (ninguno)\n");

    /* En memoria (dormidos) */
    printf(ANSI_GREEN "\n► En memoria (sleep):\n" ANSI_RESET);
    alguno = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_shm->procs[i].estado == PROC_EN_MEMORIA) {
            ProcEntry *pe = &g_shm->procs[i];
            printf("  PID=%d  (%s)\n",
                   pe->pid,
                   pe->algoritmo == ALG_PAGINACION ? "Paginación" : "Segmentación");
            alguno = 1;
        }
    }
    if (!alguno) printf("  (ninguno)\n");

    /* Bloqueados (esperando semáforo) */
    printf(ANSI_RED "\n► Bloqueados (esperando semáforo):\n" ANSI_RESET);
    alguno = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_shm->procs[i].estado == PROC_BLOQUEADO) {
            printf("  PID=%d\n", g_shm->procs[i].pid);
            alguno = 1;
        }
    }
    if (!alguno) printf("  (ninguno)\n");

    /* Muertos por falta de espacio */
    printf(ANSI_RED "\n► Muertos (sin espacio):\n" ANSI_RESET);
    alguno = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_shm->procs[i].estado == PROC_MUERTO) {
            printf("  PID=%d\n", g_shm->procs[i].pid);
            alguno = 1;
        }
    }
    if (!alguno) printf("  (ninguno)\n");

    /* Terminados normalmente */
    printf(ANSI_CYAN "\n► Terminados normalmente:\n" ANSI_RESET);
    alguno = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (g_shm->procs[i].estado == PROC_TERMINADO) {
            printf("  PID=%d\n", g_shm->procs[i].pid);
            alguno = 1;
        }
    }
    if (!alguno) printf("  (ninguno)\n");

    printf("\n");
}

/* ─────────────────────────────────────────────────────────────── */
int main(void) {
    /* Conectar a memoria compartida (solo lectura conceptualmente) */
    int shmid = shmget(SHM_KEY, sizeof(SharedMem), 0666);
    if (shmid < 0) {
        perror("shmget: ¿Está corriendo el inicializador?");
        exit(EXIT_FAILURE);
    }
    g_shm = (SharedMem *)shmat(shmid, NULL, SHM_RDONLY);
    if (g_shm == (void *)-1) { perror("shmat"); exit(EXIT_FAILURE); }

    printf(ANSI_BOLD
           "╔══════════════════════════════════╗\n"
           "║   ESPÍA - Monitor de Simulación  ║\n"
           "╚══════════════════════════════════╝\n"
           ANSI_RESET);

    int opcion;
    do {
        printf("\n[1] Estado de la memoria\n"
               "[2] Estado de los procesos\n"
               "[3] Ambos\n"
               "[0] Salir\n"
               "Opción: ");
        if (scanf("%d", &opcion) != 1) { opcion = -1; }

        switch (opcion) {
            case 1: mostrar_memoria();   break;
            case 2: mostrar_procesos();  break;
            case 3:
                mostrar_memoria();
                mostrar_procesos();
                break;
            case 0: break;
            default: printf("Opción inválida.\n");
        }
    } while (opcion != 0);

    shmdt(g_shm);
    printf("Espía terminado.\n");
    return 0;
}
