/*
Práctica de Sistemas Operativos - MiniShell
Autoría: Héctor Julián Alijas y Daniel Martín Muñoz
*/

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/types.h>
#include <limits.h>
#include "parser.h"

#define PROMPT "msh> "
#define MAX_TRABAJOS 128
#define MAX_PROC_EN_TUBERIA 32
#define TAM_RESUMEN 512

typedef enum
{
    trabajoEnEjec = 0,
    trabajoTerminado = 1
} estadoTrabajo_t;

/* Entrada en la tabla de trabajos en background */
typedef struct
{
    pid_t pids[MAX_PROC_EN_TUBERIA];
    int numPids;
    char resumen[TAM_RESUMEN];
    estadoTrabajo_t estado;
    bool usado;
} trabajo_t;

static trabajo_t trabajos[MAX_TRABAJOS];

static void manejador_sigint(int sig)
{
    (void)sig;
    ssize_t r = write(STDOUT_FILENO, "\n", 1);
    (void)r;
}

static void instalarSenalesShell(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = manejador_sigint;

    sigaction(SIGINT, &sa, NULL); // Ctrl+C → salto y EINTR
    sa.sa_handler = SIG_IGN;      // SIGQUIT lo ignoramos
    sigaction(SIGQUIT, &sa, NULL);
}

/* Los procesos ejecutados restauran la acción por defecto de las señales. */
static void restaurarSenalesEnHijo(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

/* Imprime un error y termina solo el proceso hijo. */
static void morirEnHijo(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    _exit(1);
}

/* Abre un fichero y redirige al descriptor objetivo; si falla, termina el hijo. */
static void abrirYDuplicarOSalir(const char *ruta, int flags, mode_t modo,
                                 int fdObjetivo, const char *que)
{
    int fd = open(ruta, flags, modo);
    if (fd < 0)
        morirEnHijo("%s: %s: %s\n", que, ruta, strerror(errno));
    if (dup2(fd, fdObjetivo) < 0)
    {
        int e = errno;
        close(fd);
        morirEnHijo("dup2 falló: %s\n", strerror(e));
    }
    close(fd);
}

/* Mensaje cuando no existe el mandato. */
static void mandatoNoEncontrado(const char *cmd)
{
    fprintf(stderr, "Mandato: Error. No se encuentra el mandato %s\n", cmd);
}

/* Recolecta hijos terminados sin bloquear y actualiza estados de trabajos. */
static void actualizarTrabajosNoBloqueante(void)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        for (int j = 0; j < MAX_TRABAJOS; ++j)
        {
            if (!trabajos[j].usado)
                continue;
            for (int k = 0; k < trabajos[j].numPids; ++k)
            {
                if (trabajos[j].pids[k] == pid)
                {
                    trabajos[j].pids[k] = 0;
                    break;
                }
            }
        }
    }
    for (int j = 0; j < MAX_TRABAJOS; ++j)
    {
        if (!trabajos[j].usado || trabajos[j].estado == trabajoTerminado)
            continue;
        bool algunoVivo = false;
        for (int k = 0; k < trabajos[j].numPids; ++k)
        {
            if (trabajos[j].pids[k] > 0)
            {
                algunoVivo = true;
                break;
            }
        }
        if (!algunoVivo)
            trabajos[j].estado = trabajoTerminado;
    }
}

/* Añade un trabajo en background y devuelve su índice 1..N. */
static int anadirTrabajo(pid_t *pids, int num, const char *resumen)
{
    actualizarTrabajosNoBloqueante();
    for (int j = 0; j < MAX_TRABAJOS; ++j)
    {
        if (!trabajos[j].usado)
        {
            trabajos[j].usado = true;
            trabajos[j].estado = trabajoEnEjec;
            trabajos[j].numPids = (num > MAX_PROC_EN_TUBERIA) ? MAX_PROC_EN_TUBERIA : num;
            for (int k = 0; k < trabajos[j].numPids; ++k)
                trabajos[j].pids[k] = pids[k];
            if (resumen)
            {
                snprintf(trabajos[j].resumen, TAM_RESUMEN, "%s", resumen);
            }
            else
            {
                snprintf(trabajos[j].resumen, TAM_RESUMEN, "(pipeline)");
            }
            return j + 1;
        }
    }
    fprintf(stderr, "jobs: límite de trabajos alcanzado\n");
    return -1;
}

/* Lista los trabajos y su estado. */
static void builtinJobs(void)
{
    actualizarTrabajosNoBloqueante();
    bool alguno = false;
    for (int j = 0; j < MAX_TRABAJOS; ++j)
    {
        if (!trabajos[j].usado)
            continue;
        alguno = true;
        printf("[%d]\t%s\t%s\n",
               j + 1,
               trabajos[j].estado == trabajoEnEjec ? "Running" : "Done",
               trabajos[j].resumen[0] ? trabajos[j].resumen : "(pipeline)");
    }
    if (!alguno)
        printf("No hay trabajos.\n");
}

/* Devuelve el último índice de trabajo usado (o -1 si no hay). */
static int ultimoTrabajoUsado(void)
{
    for (int j = MAX_TRABAJOS - 1; j >= 0; --j)
    {
        if (trabajos[j].usado)
            return j + 1;
    }
    return -1;
}

/* Trae un trabajo al foreground y espera a todos sus procesos. */
static void builtinFg(int indice1)
{
    if (indice1 <= 0 || indice1 > MAX_TRABAJOS || !trabajos[indice1 - 1].usado)
    {
        fprintf(stderr, "fg: número de job inválido\n");
        return;
    }
    trabajo_t *jb = &trabajos[indice1 - 1];
    actualizarTrabajosNoBloqueante();
    if (jb->estado == trabajoTerminado)
    {
        fprintf(stderr, "fg: el trabajo ya ha finalizado\n");
        return;
    }
    printf("Reanudando [%d] %s\n", indice1, jb->resumen[0] ? jb->resumen : "(pipeline)");
    fflush(stdout);
    for (;;)
    {
        bool algunoVivo = false;
        for (int k = 0; k < jb->numPids; ++k)
        {
            if (jb->pids[k] > 0)
            {
                algunoVivo = true;
                int status;
                pid_t w;
                do
                {
                    w = waitpid(jb->pids[k], &status, 0);
                } while (w < 0 && errno == EINTR);
                if (w > 0)
                    jb->pids[k] = 0;
            }
        }
        if (!algunoVivo)
            break;
    }
    jb->estado = trabajoTerminado;
    jb->usado = false;
    jb->numPids = 0;
    jb->resumen[0] = '\0';
}

/* Construye un resumen legible de la línea para mostrar en jobs. */
static void construirResumenLinea(const tline *linea, char *dst, size_t tam)
{
    dst[0] = '\0';
    size_t queda = tam;
    for (int i = 0; i < linea->ncommands; ++i)
    {
        const tcommand *c = &linea->commands[i];
        for (int a = 0; a < c->argc; ++a)
        {
            int n = snprintf(dst + strlen(dst), queda, "%s%s",
                             (a ? " " : (i ? " | " : "")),
                             c->argv[a]);
            if (n < 0 || (size_t)n >= queda)
            {
                dst[tam - 1] = '\0';
                return;
            }
            queda -= (size_t)n;
        }
    }
    if (linea->redirect_input)
    {
        int n = snprintf(dst + strlen(dst), queda, " < %s", linea->redirect_input);
        if (n > 0)
            queda -= (size_t)n;
    }
    if (linea->redirect_output)
    {
        int n = snprintf(dst + strlen(dst), queda, " > %s", linea->redirect_output);
        if (n > 0)
            queda -= (size_t)n;
    }
    if (linea->redirect_error)
    {
        int n = snprintf(dst + strlen(dst), queda, " >& %s", linea->redirect_error);
        if (n > 0)
            queda -= (size_t)n;
    }
    if (linea->background)
    {
        (void)snprintf(dst + strlen(dst), queda, " &");
    }
}

/* Ejecuta una línea: crea tuberías, aplica redirecciones y lanza procesos. */
static void ejecutarLinea(tline *linea)
{
    if (!linea || linea->ncommands <= 0)
        return;
    const int n = linea->ncommands;

    int (*tuberias)[2] = NULL;
    if (n > 1)
    {
        tuberias = calloc((size_t)(n - 1), sizeof(int[2]));
        if (!tuberias)
        {
            perror("calloc pipes");
            return;
        }
        for (int i = 0; i < n - 1; ++i)
        {
            if (pipe(tuberias[i]) < 0)
            {
                perror("pipe");
                for (int k = 0; k < i; ++k)
                {
                    close(tuberias[k][0]);
                    close(tuberias[k][1]);
                }
                free(tuberias);
                return;
            }
        }
    }

    pid_t pidsHijos[MAX_PROC_EN_TUBERIA];
    int numHijos = 0;

    for (int i = 0; i < n; ++i)
    {
        tcommand *cmd = &linea->commands[i];

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            if (tuberias)
            {
                for (int k = 0; k < n - 1; ++k)
                {
                    close(tuberias[k][0]);
                    close(tuberias[k][1]);
                }
                free(tuberias);
            }
            return;
        }

        if (pid == 0)
        {
            /* Restaurar señales SOLO si es foreground */
            if (linea->background)
            {
                // jobs en background NO deben morir con Ctrl+C
                struct sigaction sa;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = 0;
                sa.sa_handler = SIG_IGN;
                sigaction(SIGINT, &sa, NULL);
            }
            else
            {
                // foreground sí debe morir con Ctrl+C
                restaurarSenalesEnHijo();
            }

            if (i == 0 && linea->redirect_input)
            {
                abrirYDuplicarOSalir(linea->redirect_input, O_RDONLY, 0, STDIN_FILENO,
                                     "No se pudo abrir archivo de entrada");
            }
            if (i == n - 1 && linea->redirect_output)
            {
                abrirYDuplicarOSalir(linea->redirect_output,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644,
                                     STDOUT_FILENO, "No se pudo abrir archivo de salida");
            }
            if (i == n - 1 && linea->redirect_error)
            {
                abrirYDuplicarOSalir(linea->redirect_error,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644,
                                     STDERR_FILENO, "No se pudo abrir archivo de error");
            }

            if (tuberias)
            {
                if (i > 0)
                {
                    if (dup2(tuberias[i - 1][0], STDIN_FILENO) < 0)
                        morirEnHijo("dup2 stdin pipe: %s\n", strerror(errno));
                }
                if (i < n - 1)
                {
                    if (dup2(tuberias[i][1], STDOUT_FILENO) < 0)
                        morirEnHijo("dup2 stdout pipe: %s\n", strerror(errno));
                }
                for (int k = 0; k < n - 1; ++k)
                {
                    close(tuberias[k][0]);
                    close(tuberias[k][1]);
                }
            }

            if (!cmd->filename || !cmd->argv || !cmd->argv[0])
            {
                const char *mostrado = (cmd->argv && cmd->argv[0]) ? cmd->argv[0] : "(null)";
                mandatoNoEncontrado(mostrado);
                _exit(127);
            }
            execvp(cmd->filename, cmd->argv);
            mandatoNoEncontrado(cmd->argv[0]);
            _exit(127);
        }

        if (numHijos < MAX_PROC_EN_TUBERIA)
            pidsHijos[numHijos++] = pid;
    }

    if (tuberias)
    {
        for (int k = 0; k < n - 1; ++k)
        {
            close(tuberias[k][0]);
            close(tuberias[k][1]);
        }
        free(tuberias);
    }

    if (linea->background)
    {
        char resumen[TAM_RESUMEN];
        construirResumenLinea(linea, resumen, sizeof(resumen));
        int idx = anadirTrabajo(pidsHijos, numHijos, resumen);
        if (idx > 0)
        {
            pid_t ultimo = pidsHijos[numHijos - 1];
            printf("[%d] %d en background\n", idx, (int)ultimo);
            fflush(stdout);
        }
        return;
    }

    for (int i = 0; i < numHijos; ++i)
    {

        int status;
        while (waitpid(pidsHijos[i], &status, 0) < 0 && errno == EINTR)
        {
            /* repetir si EINTR */
        }

        /* Si el hijo murió por Ctrl+C y NO estamos en background → imprimir salto */
        if (!linea->background &&
            WIFSIGNALED(status) &&
            WTERMSIG(status) == SIGINT)
        {

            ssize_t ign = write(STDOUT_FILENO, "\n", 1);
            (void)ign; // evitar warning -Wunused-result
        }
    }
}

/* Cambia de directorio: cd [ruta] o cd (HOME). */
static void builtinCd(const tcommand *cmd)
{
    const char *destino = NULL;
    if (cmd->argc < 2)
    {
        destino = getenv("HOME");
        if (!destino || !*destino)
        {
            fprintf(stderr, "cd: variable HOME no definida\n");
            return;
        }
    }
    else
    {
        destino = cmd->argv[1];
    }
    if (chdir(destino) != 0)
    {
        fprintf(stderr, "cd: %s: %s\n", destino, strerror(errno));
        return;
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)))
    {
        printf("%s\n", cwd);
    }
}

int main(void)
{
    instalarSenalesShell();

    char *bufLinea = NULL;
    size_t tamBuf = 0;

    for (;;)
    {
        actualizarTrabajosNoBloqueante();
        fputs(PROMPT, stdout);
        fflush(stdout);

        ssize_t leidos = getline(&bufLinea, &tamBuf, stdin);
        if (leidos < 0)
        {
            if (errno == EINTR)
            {
                // Ctrl+C → nueva línea y nuevo prompt
                clearerr(stdin);
                continue;
            }
            putchar('\n');
            break; // EOF real
        }

        if (leidos == 1 && bufLinea[0] == '\n')
            continue;

        tline *linea = tokenize(bufLinea);
        if (!linea)
            continue;

        if (linea->ncommands == 1)
        {
            tcommand *cmd = &linea->commands[0];
            const char *cmd0 = (cmd->argv && cmd->argv[0]) ? cmd->argv[0] : NULL;

            if (cmd0 && strcmp(cmd0, "exit") == 0)
            {
                int status = 0;
                if (cmd->argc >= 2)
                    status = atoi(cmd->argv[1]);
                free(bufLinea);
                exit(status);
            }
            if (cmd0 && strcmp(cmd0, "jobs") == 0)
            {
                builtinJobs();
                continue;
            }
            if (cmd0 && strcmp(cmd0, "fg") == 0)
            {
                int idx;
                if (cmd->argc < 2)
                {
                    idx = ultimoTrabajoUsado();
                    if (idx < 0)
                    {
                        fprintf(stderr, "fg: no hay trabajos\n");
                        continue;
                    }
                }
                else
                {
                    idx = atoi(cmd->argv[1]);
                }
                builtinFg(idx);
                continue;
            }
            if (cmd0 && strcmp(cmd0, "cd") == 0)
            {
                builtinCd(cmd);
                continue;
            }
        }

        ejecutarLinea(linea);
    }

    free(bufLinea);
    return 0;
}
