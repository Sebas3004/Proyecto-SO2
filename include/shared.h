#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>

/* ─── Claves IPC ─────────────────────────────────────────────── */
#define SHM_KEY       0x1234   /* Memoria compartida principal    */
#define SEM_KEY       0x5678   /* Semáforo de región crítica      */
#define SEM_LOG_KEY   0x9ABC   /* Semáforo para la bitácora       */

/* ─── Tamaños ────────────────────────────────────────────────── */
#define MEM_TOTAL_PAGES  64    /* Páginas totales en la memoria   */
#define MAX_PROCESSES    32    /* Máx procesos simultáneos        */
#define MAX_SEGMENTS      5    /* Máx segmentos por proceso       */
#define LOG_FILE         "bitacora.log"

/* ─── Algoritmo ──────────────────────────────────────────────── */
#define ALG_PAGINACION   1
#define ALG_SEGMENTACION 2

/* ─── Estado del proceso ─────────────────────────────────────── */
#define PROC_LIBRE       0
#define PROC_BUSCANDO    1   /* Buscando espacio (en región crítica) */
#define PROC_EN_MEMORIA  2   /* Durmiendo con memoria asignada       */
#define PROC_BLOQUEADO   3   /* Esperando semáforo                   */
#define PROC_MUERTO      4   /* Murió por falta de espacio           */
#define PROC_TERMINADO   5   /* Terminó normalmente                  */

/* ─── Entrada de proceso en la tabla ────────────────────────────*/
typedef struct {
    pid_t  pid;
    int    estado;
    int    algoritmo;          /* ALG_PAGINACION o ALG_SEGMENTACION */
    int    tiempo_sleep;       /* segundos que duerme               */

    /* Paginación */
    int    num_paginas;
    int    paginas[10];        /* índices de página asignados       */

    /* Segmentación */
    int    num_segmentos;
    int    tam_segmento[MAX_SEGMENTS];   /* espacios por segmento   */
    int    seg_inicio[MAX_SEGMENTS];     /* frame de inicio         */
} ProcEntry;

/* ─── Memoria compartida principal ───────────────────────────── */
typedef struct {
    /* Marco de páginas: 0=libre, PID=ocupado */
    pid_t  marcos[MEM_TOTAL_PAGES];

    /* Tabla de procesos */
    ProcEntry procs[MAX_PROCESSES];

    /* Configuración global */
    int    algoritmo;          /* elegido por el Productor          */
    int    total_paginas;      /* configurado por el Inicializador  */
    int    inicializado;       /* flag: 1 si el shm está listo      */
    int    finalizar;          /* flag: 1 para que el Productor muera */
} SharedMem;

/* ─── Unión requerida por semctl ─────────────────────────────── */
union semun {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
};

/* ─── Operaciones de semáforo ────────────────────────────────── */
static inline void sem_wait_op(int semid, int num) {
    struct sembuf op = { (unsigned short)num, -1, 0 };
    semop(semid, &op, 1);
}
static inline void sem_signal_op(int semid, int num) {
    struct sembuf op = { (unsigned short)num, 1, 0 };
    semop(semid, &op, 1);
}

#endif /* SHARED_H */
