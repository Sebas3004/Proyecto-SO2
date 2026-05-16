# Simulación Paginación y Segmentación
**Sistemas Operativos — I Semestre 2026**

## Estructura del proyecto

```
proyecto_memoria/
├── include/
│   └── shared.h          ← Estructuras, constantes y claves IPC compartidas
├── src/
│   ├── inicializador.c   ← Programa 1: crea SHM y semáforos, luego muere
│   ├── productor.c       ← Programa 2: genera procesos como threads
│   ├── espia.c           ← Programa 3: monitor interactivo
│   └── finalizador.c     ← Programa 4: limpia todo y cierra bitácora
├── Makefile
└── README.md
```

## Compilación

```bash
make          # compila los 4 programas
make clean    # elimina binarios y bitácora
```

Requiere: `gcc`, `pthread`, Linux (semáforos System V).

## Ejecución (orden obligatorio)

### Terminal 1 — Inicializador
```bash
./inicializador
# Pide cantidad de marcos (ej: 32)
# Crea SHM + semáforos y termina
```

### Terminal 2 — Productor
```bash
./productor
# Pide algoritmo: 1=Paginación, 2=Segmentación
# Se queda corriendo generando procesos (threads) cada 30-60 s
# Terminar con Ctrl+C o ejecutar el finalizador
```

### Terminal 3 — Espía (cualquier momento)
```bash
./espia
# Menú interactivo:
#   1. Estado de la memoria
#   2. Estado de los procesos
#   3. Ambos
```

### Terminal 4 — Finalizador (para terminar todo)
```bash
./finalizador
# Señaliza al Productor, imprime resumen,
# libera SHM y semáforos, cierra bitácora.
```

## Diseño de la memoria compartida

| Campo         | Descripción                                      |
|---------------|--------------------------------------------------|
| `marcos[]`    | Array de PIDs — 0=libre, PID=ocupado             |
| `procs[]`     | Tabla de hasta 32 procesos con su estado         |
| `algoritmo`   | 1=Paginación / 2=Segmentación (elige Productor)  |
| `finalizar`   | Flag que el Finalizador pone en 1                |

## Semáforos utilizados

| Clave        | Uso                                              |
|--------------|--------------------------------------------------|
| `SEM_KEY`    | Región crítica — solo un proceso busca a la vez  |
| `SEM_LOG_KEY`| Acceso exclusivo a la bitácora                   |

Se usan **semáforos System V** (`semget`/`semop`) porque permiten comunicación entre **procesos separados** (no solo threads), que es el requisito del proyecto.

## Flujo de cada proceso (thread del Productor)

```
1. Pedir semáforo de memoria   (sem_wait)
2. Buscar espacio en marcos
3. Escribir en bitácora        (sem protegido)
4. Devolver semáforo de memoria (sem_signal)
5. Sleep (20-60 s)
6. Pedir semáforo de memoria   (sem_wait)
7. Liberar marcos
8. Escribir en bitácora
9. Devolver semáforo de memoria (sem_signal)
```

Si en el paso 2 no hay espacio → proceso muere (registrado en bitácora).
