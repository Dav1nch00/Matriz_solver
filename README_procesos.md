# Multiplicación de matrices paralela: de hilos a procesos

> Documentación de la versión con **procesos (`fork`)** de `matmul.c`, que originalmente usaba **hilos (`pthreads`)**.
> Curso: HPC (High Performance Computing) · Entorno: WSL + Debian

---

## Tabla de contenido

1. [Resumen](#1-resumen)
2. [Conceptos previos](#2-conceptos-previos)
3. [La llamada al sistema `fork()`](#3-la-llamada-al-sistema-fork)
4. [Memoria compartida entre procesos](#4-memoria-compartida-entre-procesos)
5. [Semáforos: ¿hacen falta?](#5-semáforos-hacen-falta)
6. [Medición del tiempo](#6-medición-del-tiempo)
7. [Cambios realizados al código](#7-cambios-realizados-al-código)
8. [Compilación y uso](#8-compilación-y-uso)
9. [Trabajar en WSL](#9-trabajar-en-wsl)
10. [Plan de experimentos y comparación hilos vs procesos](#10-plan-de-experimentos-y-comparación-hilos-vs-procesos)
11. [Puntos a matizar sobre los apuntes de clase](#11-puntos-a-matizar-sobre-los-apuntes-de-clase)
12. [Observaciones sobre el código original](#12-observaciones-sobre-el-código-original)
13. [Fuentes](#13-fuentes)

---

## 1. Resumen

El programa calcula **C = A × B** para matrices cuadradas de tamaño N×N con el algoritmo clásico de complejidad O(n³). La versión original reparte las filas de C entre **hilos**. La tarea de esta etapa es crear una **tercera versión** que reparta las filas entre **procesos**, repetir los mismos experimentos y analizar cuál es mejor y por qué.

| | Versión con hilos | Versión con procesos |
|---|---|---|
| Archivo | `matmul.c` | `matmul_procesos.c` |
| Creación del paralelismo | `pthread_create` | `fork()` |
| Espera del final | `pthread_join` | `waitpid()` |
| Memoria de `C` | Compartida de forma implícita | Compartida de forma **explícita** (`mmap`) |
| Medición de CPU | `getrusage(RUSAGE_SELF)` | `getrusage(RUSAGE_CHILDREN)` |
| Compilación | `gcc -O2 -pthread` | `gcc -O2` |

La interfaz, el formato de salida, el reparto de filas y la verificación se mantuvieron **iguales** para poder comparar de forma justa.

---

## 2. Conceptos previos

### Proceso
Un proceso es un programa en ejecución con su **propio espacio de direcciones** (código, datos, pila, montón) y sus propios descriptores de archivo. Dos procesos no ven la memoria del otro, a menos que se use un mecanismo explícito para compartirla.

### Hilo
Un hilo es una línea de ejecución **dentro de un proceso**. Todos los hilos de un proceso comparten el mismo espacio de direcciones, por lo que comparten datos de forma implícita (y por eso pueden aparecer condiciones de carrera).

### Hilos frente a procesos

| Aspecto | Hilos | Procesos |
|---|---|---|
| Memoria | Compartida por defecto | Separada por defecto |
| Compartir datos | Directo (variables, punteros) | Requiere mecanismo explícito (`mmap`, `shm_open`, pipes, etc.) |
| Aislamiento | Bajo: un fallo puede afectar a todo el proceso | Alto: un hijo que falla no corrompe a los demás |
| Programación | Más simple | Más trabajo (memoria compartida, `wait`, limpieza) |
| Costo de creación | Menor | Algo mayor (hay que copiar estructuras, como las tablas de páginas) |

### Concurrencia y paralelismo
- **Concurrencia**: varias tareas avanzan de forma intercalada (aunque haya un solo núcleo).
- **Paralelismo**: varias tareas se ejecutan **al mismo tiempo** en núcleos distintos.

---

## 3. La llamada al sistema `fork()`

`fork()` crea un proceso nuevo, el **hijo**, que es un duplicado del proceso que la llamó, el **padre**. Después de la llamada, ambos continúan ejecutando la instrucción siguiente.

### Valor de retorno

| Valor | Quién lo recibe | Significado |
|---|---|---|
| `< 0` | Padre | Error: no se pudo crear el hijo |
| `== 0` | **Hijo** | Estás en el proceso hijo |
| `> 0` | **Padre** | El valor es el PID del hijo |

```c
pid_t p = fork();
if (p < 0)       { /* error */ }
else if (p == 0) { /* código del HIJO */ }
else             { /* código del PADRE (p = PID del hijo) */ }
```

### Lo que enseña el artículo de GeeksforGeeks

1. **Crecimiento exponencial**: `n` llamadas seguidas a `fork()` producen 2ⁿ procesos. Si el `fork()` está dentro de un ciclo y los hijos siguen el ciclo, se crean más procesos de los deseados. **Por eso el hijo debe terminar con `_exit()` al acabar su trabajo.**
2. **Orden no determinista**: el sistema operativo decide si corre primero el padre o el hijo, así que el orden de la salida puede variar.
3. **Memoria separada**: si el padre y el hijo modifican una misma variable, cada uno modifica **su copia**. Este es el punto más importante para esta tarea.
4. **`fork()` frente a `exec()`**: `fork()` crea una copia del proceso; `exec()` reemplaza el programa del proceso actual por otro.

> Nota: el artículo describe `fork()` como una función "threading based". Esa frase es confusa: `fork()` crea **procesos**, no hilos.

### Lo que dice el manual de Linux (`man 2 fork`)

- El hijo recibe un PID propio y una copia del espacio de direcciones del padre, además de copias de los descriptores de archivo abiertos.
- **Copy-on-write (COW)**: en Linux no se copia toda la memoria al hacer `fork()`. Padre e hijo **comparten físicamente** las mismas páginas (marcadas como solo lectura) hasta que alguno **escribe** en una; en ese momento el kernel copia solo esa página. El costo inicial es duplicar las tablas de páginas y crear la estructura de tarea del hijo.
- `fork()` puede fallar con `EAGAIN` (límite de procesos alcanzado) o `ENOMEM` (memoria insuficiente en el kernel).

### Consecuencia para la multiplicación de matrices

| Matriz | ¿Quién la usa? | Qué pasa tras el `fork()` |
|---|---|---|
| `A` | Solo lectura | COW: **no se copia de verdad** |
| `B` | Solo lectura | COW: **no se copia de verdad** |
| `C` | Los hijos **escriben** | Cada hijo escribiría en **su propia copia** y el padre no vería nada |

Por eso `C` necesita memoria compartida explícita.

---

## 4. Memoria compartida entre procesos

Dos formas comunes en Linux:

### a) `mmap` anónimo compartido (la usada en este proyecto)

El padre crea la región **antes** del `fork()`, y los hijos la heredan: es la misma memoria física para todos.

```c
void *bloque = mmap(NULL, n * n * sizeof(long long),
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
```

- `MAP_SHARED`: los cambios son visibles para todos los procesos que comparten el mapeo.
- `MAP_ANONYMOUS`: no está respaldada por un archivo; la memoria arranca en ceros.
- Se libera con `munmap()`.
- Si `mmap` falla devuelve `MAP_FAILED`.

### b) Memoria compartida POSIX (`shm_open` + `ftruncate` + `mmap`)

Se crea un objeto con nombre con `shm_open()`, se le da tamaño con `ftruncate()` y se mapea con `mmap()`. Sirve incluso para procesos no emparentados. En Linux estos objetos viven en `/dev/shm` y **persisten** hasta que se llame a `shm_unlink()` o se reinicie el equipo; si el programa se cae sin limpiar, queda basura. Para esta tarea no hace falta.

### ¿Por qué `C` sigue usando la sintaxis `c[i][j]`?

`C` es un `long long **`: un arreglo de punteros a filas. El **arreglo de punteros** es un `malloc` normal (cada hijo hereda su copia), pero los punteros apuntan al **bloque de datos compartido**. Como el `mmap` se hizo antes del `fork()`, esa dirección virtual es válida en todos los procesos, y `c[i][j]` funciona igual en el padre y en los hijos.

```
Padre:  m ──► [ptr fila 0][ptr fila 1] ... [ptr fila n-1]   (malloc, privado)
                  │          │                   │
                  ▼          ▼                   ▼
              ┌──────────────────────────────────────┐
              │  bloque de datos de N×N (mmap)       │  ◄── compartido con los hijos
              └──────────────────────────────────────┘
```

---

## 5. Semáforos: ¿hacen falta?

Los semáforos evitan **secciones críticas**: zonas donde varios procesos modificarían el mismo dato a la vez.

En este programa **no son necesarios**: el reparto por filas hace que cada proceso escriba en filas **distintas** de `C`. No hay dos procesos escribiendo la misma celda.

Se necesitarían, por ejemplo, si varios procesos actualizaran un contador global o hicieran una reducción sobre una misma variable.

Lo que sí hace falta es **sincronizar el final**: el padre debe esperar a todos los hijos con `waitpid()`, que cumple el mismo papel que `pthread_join` en la versión con hilos.

---

## 6. Medición del tiempo

### Los tres tiempos

| Tiempo | Qué mide | En el código |
|---|---|---|
| **Wall clock** (tiempo de pared) | Tiempo real transcurrido de reloj, del inicio al fin de la multiplicación | `clock_gettime(CLOCK_MONOTONIC)` |
| **Tiempo de usuario** | Tiempo de CPU ejecutando el **código del programa** | `ru_utime` de `getrusage` |
| **Tiempo de sistema** | Tiempo de CPU que el **kernel** gastó trabajando para el proceso (llamadas al sistema, crear procesos, fallos de página) | `ru_stime` de `getrusage` |

Usuario + sistema = **tiempo de CPU**.

Cuando el proceso es sacado del procesador (fin de quantum) o espera por entrada/salida, ese tiempo **sí transcurre en el wall clock**, pero **no** cuenta como usuario ni como sistema, porque en ese momento no está usando CPU.

### Relación entre ellos

- Con **un solo hilo/proceso** sin interrupciones: `wall ≈ usuario + sistema`.
- Con **p** hilos/procesos en paralelo: el tiempo de CPU se **suma** entre todos, pero el reloj de pared avanza una sola vez.

```
Ejemplo ideal con 4 procesos:
  usuario ≈ 8.0 s   (4 procesos × 2 s)
  wall    ≈ 2.0 s

  paralelismo efectivo = (usuario + sistema) / wall ≈ 4
```

### Por qué el wall clock es la métrica principal

Lo que importa en un programa paralelo es **cuánto tarda en tener el resultado el usuario**. Con el wall clock se calculan:

```
Speedup     S(p) = T_wall(1) / T_wall(p)
Eficiencia  E(p) = S(p) / p
```

El tiempo de usuario **no sirve para el speedup** (tiende a mantenerse o subir al agregar procesos, porque es trabajo sumado), pero sirve como **diagnóstico**:

- Usuario **sube mucho** al aumentar procesos → trabajo extra o contención de memoria.
- Sistema **sube mucho** → sobrecarga de crear procesos o de cambios de contexto.

### Detalle clave en la versión con procesos

`getrusage(RUSAGE_SELF)` mide **solo al padre**, que casi no trabaja, y daría un tiempo de usuario cercano a cero. Por eso se usa **`RUSAGE_CHILDREN`**, que acumula a los hijos **ya terminados y esperados con `wait()`**. El wall clock no tiene este problema, porque se mide en el padre alrededor de todo el bloque `fork` + `wait`.

---

## 7. Cambios realizados al código

Se creó `matmul_procesos.c` a partir de `matmul.c`, cambiando lo mínimo necesario.

### 7.1 Lo que se mantiene igual

- Argumentos: `./matmul_procesos <tamaño> [limite] [verificar] [semilla] [procesos]` (el último pasó de "hilos" a "procesos").
- Mismas validaciones (tamaño mínimo 600, límite ≥ 0, procesos ≥ 1).
- Mismas funciones `crear_matriz`, `liberar_matriz`, `llenar_aleatorio` y `verificar`.
- Mismo reparto de filas: `n / num_procesos` filas para cada uno y el resto entre los primeros.
- Mismo formato de salida (pared, usuario, sistema, verificación).
- No se imprimen las matrices.

### 7.2 Cabeceras

Se quita `pthread.h` y se agregan `sys/wait.h` (para `waitpid`) y `sys/mman.h` (para `mmap`/`munmap`).

### 7.3 Matriz compartida (nuevo)

```c
static long long **crear_matriz_compartida(int n)
{
    long long **m = (long long **)malloc((size_t)n * sizeof(long long *));
    if (m == NULL) return NULL;
    void *bloque = mmap(NULL, (size_t)n * (size_t)n * sizeof(long long),
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (bloque == MAP_FAILED) { free(m); return NULL; }
    m[0] = (long long *)bloque;
    for (int i = 1; i < n; i++)
        m[i] = m[0] + (size_t)i * n;
    return m;
}

static void liberar_matriz_compartida(long long **m, int n)
{
    if (m != NULL) {
        munmap(m[0], (size_t)n * (size_t)n * sizeof(long long));
        free(m);
    }
}
```

En `main`, `A` y `B` siguen con `crear_matriz` (privadas, solo lectura) y **`C` usa `crear_matriz_compartida`**. Al final, `C` se libera con `liberar_matriz_compartida`.

### 7.4 Trabajo de cada proceso

La función que ejecutaba cada hilo (`void *multiplicar_rango(void *arg)`, con la estructura `tarea_hilo_t`) pasa a ser una función normal que recibe el rango de filas. El triple bucle interno es el mismo.

```c
static void multiplicar_rango(long long **a, long long **b, long long **c,
                              int n, int fila_inicio, int fila_fin)
{
    for (int i = fila_inicio; i < fila_fin; i++) {
        for (int j = 0; j < n; j++) {
            long long suma = 0;
            for (int k = 0; k < n; k++)
                suma += a[i][k] * b[k][j];
            c[i][j] = suma;
        }
    }
}
```

### 7.5 Creación y espera de procesos

`pthread_create` se reemplaza por `fork()`, y `pthread_join` por `waitpid()`:

```c
fflush(stdout);
fflush(stderr);   /* evita que el buffer se duplique en los hijos */

for (int t = 0; t < num_procesos; t++) {
    /* ... cálculo de inicio y fin del rango ... */
    pid_t pid = fork();
    if (pid < 0) { /* error: se deja de crear procesos */ break; }
    if (pid == 0) {
        /* ---- Código del HIJO ---- */
        multiplicar_rango(a, b, c, n, inicio, fin);
        _exit(EXIT_SUCCESS);   /* el hijo NO debe seguir el código del padre */
    }
    /* ---- Código del PADRE ---- */
    pids[creados++] = pid;
}

for (int t = 0; t < creados; t++) {
    int estado = 0;
    if (waitpid(pids[t], &estado, 0) < 0) { /* error */ }
    else if (!WIFEXITED(estado) || WEXITSTATUS(estado) != 0) { /* hijo falló */ }
}
```

Puntos importantes:

- **`_exit()` en el hijo**: sin él, el hijo seguiría ejecutando el código del padre (y crearía más procesos, como el crecimiento exponencial explicado en la sección 3). Se usa `_exit` y no `exit` para no vaciar dos veces los buffers de `stdio` heredados.
- **`fflush` antes de `fork`**: evita que texto pendiente en el buffer de salida se imprima varias veces.
- **Se comprueba el resultado de cada hijo**: `WIFEXITED` y `WEXITSTATUS` permiten detectar si un hijo terminó de forma anormal. Con hilos esto no era necesario. Por eso `multiplicar` ahora **devuelve un código de error** y `main` lo revisa.
- **Si `fork()` falla** a mitad, el padre espera igualmente a los hijos ya creados antes de reportar el error.

### 7.6 Medición

`RUSAGE_SELF` cambia a `RUSAGE_CHILDREN` (ver sección 6). El wall clock se mantiene igual.

### 7.7 Resumen de equivalencias

| Hilos | Procesos |
|---|---|
| `pthread_t *hilos` | `pid_t *pids` |
| `pthread_create(...)` | `fork()` + `if (pid == 0)` |
| `pthread_join(...)` | `waitpid(...)` |
| `return NULL` al final del hilo | `_exit(EXIT_SUCCESS)` |
| `C` con `malloc` | `C` con `mmap(MAP_SHARED \| MAP_ANONYMOUS)` |
| `-pthread` | (no se necesita) |

### 7.8 Pruebas realizadas

Con matrices de 800×800 y semilla 42, ejecutando con 1, 2 y 4 procesos:

- Compila sin advertencias con `-Wall -Wextra`.
- La verificación por muestreo dio **OK** en los tres casos, lo que confirma que el padre ve los resultados que escriben los hijos en la memoria compartida.
- Los casos de error (tamaño menor a 600, procesos igual a 0) devuelven código de error con su mensaje.

> **Importante:** el entorno donde se hicieron estas pruebas tenía **un solo núcleo**, así que los tiempos con 1, 2 y 4 procesos salieron casi iguales. **No se midió speedup.** Esos datos deben salir de las máquinas del grupo con varios núcleos.

---

## 8. Compilación y uso

```bash
# Compilar (Linux / WSL)
gcc -O2 matmul_procesos.c -o matmul_procesos

# Si falta gcc en Debian:
sudo apt install build-essential

# Ejecutar: tamaño 1000, límite 100, verificar, semilla 42, 4 procesos
./matmul_procesos 1000 100 1 42 4

# Usando todos los núcleos disponibles (valor por defecto)
./matmul_procesos 1000
```

Salida de ejemplo:

```
Multiplicacion de matrices 800x800 completada.
Limite de valores: 100 | Semilla: 42 | Procesos: 4
Tiempo de pared: 1.018 segundos
Tiempo de usuario: 0.998 segundos
Tiempo de sistema: 0.012 segundos
Verificacion: OK (muestra de celdas recomputada sin discrepancias).
```

---

## 9. Trabajar en WSL

**WSL** (Windows Subsystem for Linux) permite ejecutar Linux dentro de Windows; **Debian** es la distribución instalada encima. Lo que importa es la versión de WSL:

- **WSL 2** (la predeterminada): usa un **kernel de Linux real** en una máquina virtual ligera. `fork`, `mmap`, `shm_open`, `pthreads` y semáforos POSIX se comportan como en Linux normal. Todo lo explicado en este documento aplica tal cual.
- **WSL 1**: traduce llamadas del sistema a Windows; funciona, pero con más rarezas y menos rendimiento.

Para comprobarlo, en PowerShell o CMD:

```
wsl -l -v
```

### Qué tener en cuenta en los experimentos

1. **Núcleos y memoria visibles**: WSL 2 solo ve lo que Windows le asigna. Se consultan con `nproc` y `free -h`. Se pueden fijar límites con `C:\Users\<usuario>\.wslconfig` (`processors=`, `memory=` bajo `[wsl2]`) y reiniciar con `wsl --shutdown`.
2. **Núcleos lógicos frente a físicos**: `nproc` cuenta hilos lógicos; con hyperthreading el speedup suele estancarse antes de llegar a ese número. Conviene anotar en el informe el modelo del procesador, y sus núcleos físicos y lógicos.
3. **Más ruido en las mediciones**: hay Windows y una máquina virtual debajo.
   - Cerrar programas pesados durante las pruebas.
   - Conectar el portátil a la corriente.
   - Hacer una corrida de calentamiento que se descarta.
   - Repetir cada medición varias veces (por ejemplo 5) y usar promedio o mediana.
4. **Ubicación del código**: trabajar en el sistema de archivos de Linux (`~/hpc/`) y no en `/mnt/c/...`, que es más lento.
5. **Tamaño de las matrices**: cada matriz ocupa `n² × 8` bytes (`long long`). Tres matrices con `n = 10000` son unos 2,4 GB. Elegir tamaños que quepan cómodamente en la RAM que ve WSL, para no caer en swap y arruinar la medición.

---

## 10. Plan de experimentos y comparación hilos vs procesos

La tarea pide **repetir con procesos los mismos experimentos hechos con hilos** y **justificar cuál es mejor y por qué**.

### Condiciones para una comparación justa

- Mismos tamaños de matriz (por ejemplo 600, 1000, 1500, 2000…).
- Mismas semillas y mismo `limite`.
- Mismos números de hilos/procesos (por ejemplo 1, 2, 4, 8, y el número de núcleos lógicos).
- Misma máquina y mismas condiciones (ver sección 9).
- Varias repeticiones por configuración; reportar promedio o mediana.

### Métricas

| Métrica | Fórmula | Qué indica |
|---|---|---|
| Tiempo de pared | medido | Lo que realmente tarda |
| Speedup | `T_wall(1) / T_wall(p)` | Cuánto se acelera |
| Eficiencia | `S(p) / p` | Qué tan bien se aprovechan los núcleos |
| Paralelismo efectivo | `(usuario + sistema) / wall` | Cuántos núcleos se usaron de verdad |

### Hipótesis para contrastar con los datos

Son hipótesis, no resultados; deben **medirse**:

- El cómputo puro (O(n³)) debería rendir **casi igual** con hilos y con procesos, porque el trabajo domina sobre el costo de creación, sobre todo con `n` grande.
- Los **procesos** pueden tener algo más de sobrecarga al crearse (copiar tablas de páginas) y en tamaños pequeños (`n ≈ 600`) podría notarse.
- Los **hilos** son más simples de programar (memoria compartida implícita); los **procesos** exigen `mmap`, `waitpid` y limpieza, pero ofrecen **aislamiento**.
- Ambas versiones comparten el mismo cuello de botella de caché: el acceso `b[k][j]` recorre `B` por columnas, saltando de fila en fila. Ni hilos ni procesos lo resuelven.
- Crear más hilos o procesos que núcleos disponibles no mejora el tiempo y solo agrega cambios de contexto (el tiempo de sistema empieza a crecer).

### Cómo decidir "cuál es mejor"

Como dijo el profesor, depende del tipo de algoritmo, arquitectura o problema. Preguntas útiles:

- ¿Hay que **compartir muchos datos** entre las unidades de trabajo? → los hilos son más naturales.
- ¿Se necesita **aislamiento** ante fallos o seguridad? → los procesos.
- ¿Cuánto pesa la **creación** frente al cómputo? → depende del tamaño del problema.
- ¿Cuánta **complejidad de programación** se acepta?

---

## 11. Puntos a matizar sobre los apuntes de clase

Estos puntos conviene confirmarlos con el docente para no repetir en el informe algo impreciso.

| Apunte de clase | Matiz |
|---|---|
| "El quantum es por proceso, no por hilo" | En Linux, el kernel planifica **tareas**, y no distingue entre lo que el espacio de usuario ve como proceso (`fork`) y como hilo (`pthread_create`): ambos son planificados individualmente (modelo 1:1). Puede que el profesor hablara de un modelo teórico general; vale la pena preguntarle. Lo que sí es cierto es que crear muchísimos hilos o procesos, más que núcleos, solo añade cambios de contexto. |
| "Se duplica la memoria" al hacer `fork` | Conceptualmente sí (espacios de direcciones separados), pero en Linux se usa **copy-on-write**: las páginas se comparten físicamente hasta que alguien escribe. |
| El wall clock aparece junto al tiempo de usuario | Son distintos: el wall clock es el tiempo total transcurrido; el tiempo de usuario es solo la parte en que la CPU ejecutó código del programa. |
| El tiempo de sistema como "ser interrumpido por quantum o E/S" | Ser sacado del procesador o esperar E/S **cuenta en el wall clock**, pero **no** como tiempo de usuario ni de sistema. El tiempo de sistema es lo que el kernel trabaja en nombre del proceso. |
| "`fork` identifica cuándo se crea un hilo" | `fork()` crea **procesos**, no hilos. |
| Costo de hilos frente a procesos | Ambos usan por debajo la llamada al sistema `clone` con banderas distintas (los hilos comparten memoria con `CLONE_VM`, entre otras). Un `fork` es algo más caro por copiar tablas de páginas, pero no dramáticamente. |

---

## 12. Observaciones sobre el código original

Aplican a las dos versiones:

1. **Acceso a `B` por columnas**: en el bucle interno, `b[k][j]` recorre `B` saltando entre filas y genera muchos fallos de caché cuando `n` crece. Es el mayor cuello de botella. Se mejora con el orden de bucles **i-k-j**, con la **transposición de `B`**, o con **blocking/tiling**. Es una buena línea de trabajo posterior a la comparación.
2. **Casts redundantes**: en la versión con hilos, `(long long)t->a[i][k]` no hace nada porque ya es `long long`.
3. **Desbordamiento**: con `limite` muy alto y `n` grande, la suma podría exceder `long long`. Con valores razonables no ocurre.
4. **Aleatoriedad**: `(rand() * rango + rand()) % valores_posibles` da una distribución cercana a uniforme, aunque no perfecta.
5. **Verificación por muestreo**: se recomputan hasta 1000 celdas; no es una comprobación exhaustiva.
6. **Tiempo medido**: solo se mide la multiplicación, no la generación de las matrices, lo cual es correcto para analizar rendimiento.

Solo en la versión con hilos:

7. **Fuga en error de `pthread_create`**: si falla la creación de un hilo, se hace `exit` sin liberar todo. Es irrelevante en la práctica porque el proceso termina.

---

## 13. Fuentes

- **GeeksforGeeks**, "fork() system call" (enlace enviado por el docente): https://www.geeksforgeeks.org/c/fork-system-call/
- **Manual de Linux**, `man 2 fork` (man7.org): qué se hereda, copy-on-write y códigos de error.
- **Manual de Linux**, `mmap(2)`, `shm_open(3)`, `waitpid(2)`, `getrusage(2)`.
- **Lawlor** (cs.uaf.edu): artículo sobre `fork` y `mmap`, con un ejemplo de memoria compartida tras `fork()`.
- **OpenCSF** (James Madison University): capítulo sobre memoria compartida (`shm_open` + `mmap`).
- **Eli Bendersky**: publicación sobre `clone` y por qué hilos y procesos son casi lo mismo en Linux.
- **Libro recomendado**: *The Linux Programming Interface*, de Michael Kerrisk (referencia clásica sobre `fork`, `mmap`, semáforos POSIX y memoria compartida). No fue consultado directamente; se incluye como recomendación general.
