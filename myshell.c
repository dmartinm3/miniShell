/*
 * MiniShell – Práctica SSOO
 * ================================================================
 * REQUISITOS IMPLEMENTADOS (según enunciado):
 *  - Prompt "msh> ".
 *  - Ejecución de mandatos con tuberías '|'.
 *  - Redirecciones:
 *      * '< fichero'  -> solo en el PRIMER mandato.
 *      * '> fichero'  -> solo en el ÚLTIMO mandato.
 *      * '>& fichero' -> solo en el ÚLTIMO mandato (stderr).
 *  - Ejecución en background con '&' (no bloquea la shell).
 *  - Listado de trabajos 'jobs' (con actualización de estado).
 *  - 'fg <n>' trae el trabajo n al foreground y espera a TODA su tubería.
 *  - Manejo de señales: la shell ignora SIGINT/SIGQUIT; los hijos usan por defecto.
 *
 * DISEÑO DE JOBS:
 *  - Cada trabajo representa una línea ejecutada en background.
 *  - Guardamos todos los PIDs de la tubería para poder:
 *      * Mostrar su estado en 'jobs'.
 *      * Esperar a TODOS en 'fg <n>'.
 *  - Actualizamos estados con waitpid(..., WNOHANG) para evitar zombies.
 *
 * NOTAS:
 *  - NO usamos system(), ni bash externos. Todo con fork/execvp/pipe/dup2/waitpid.
 *  - Código robusto ante errores de E/S y con cierres de descriptores.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "parser.h"   /* Proporcionado: tokenize(), tline, tcommand */

#define SHELL_PROMPT "msh> "

/* ====== Parámetros de limites razonables ====== */
#define MAX_JOBS        128
#define MAX_CMDS_INJOB  32   /* nº máx. de procesos por pipeline que guardaremos */
#define CMDSTR_LEN      512  /* longitud para guardar la línea resumida del job */

/* ====== Estructuras para 'jobs' ====== */
typedef enum { JOB_RUNNING = 0, JOB_DONE = 1 } job_state_t;

typedef struct {
    pid_t   pids[MAX_CMDS_INJOB];   /* PIDs de todos los procesos de la tubería */
    int     npids;                  /* cuántos se guardaron */
    char    cmdline[CMDSTR_LEN];    /* texto resumen del trabajo */
    job_state_t state;              /* RUNNING / DONE */
    bool    used;                   /* si esta entrada está ocupada */
} job_t;

static job_t jobs[MAX_JOBS];

/* ====== Señales: la shell ignora Ctrl+C / Ctrl+\ ====== */
static void install_shell_signal_handlers(void) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT,  &sa, NULL);   /* Ctrl+C no mata la shell */
    sigaction(SIGQUIT, &sa, NULL);   /* Ctrl+\ idem */
}

/* En cada hijo restauramos comportamiento por defecto para que Ctrl+C le afecte. */
static void restore_default_signals_in_child(void) {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sa.sa_handler = SIG_DFL;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
}

/* ====== Utilidades para errores y redirecciones ====== */
static void die_child(const char *fmt, ...) {
    /* Mensaje y salida segura en proceso hijo */
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    _exit(1);
}

static void open_and_dup_or_exit(const char *path, int flags, mode_t mode, int target_fd, const char *what) {
    int fd = open(path, flags, mode);
    if (fd < 0) die_child("%s: %s: %s\n", what, path, strerror(errno));
    if (dup2(fd, target_fd) < 0) {
        int saved = errno;
        close(fd);
        die_child("dup2(%s -> %d) falló: %s\n", path, target_fd, strerror(saved));
    }
    close(fd);
}

/* ====== Gestión de jobs ====== */

/* Recolecta no-bloqueante cualquier hijo que haya terminado y
   actualiza el estado de los jobs a DONE si ya no queda ningún PID vivo. */
static void jobs_reap_nonblocking(void) {
    int status;
    pid_t pid;
    /* Recolectamos todos los hijos terminados (no bloquea) */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        /* Marcamos ese PID como finalizado en su job */
        for (int j = 0; j < MAX_JOBS; ++j) {
            if (!jobs[j].used) continue;
            for (int k = 0; k < jobs[j].npids; ++k) {
                if (jobs[j].pids[k] == pid) {
                    jobs[j].pids[k] = 0; /* 0 == fin */
                    break;
                }
            }
        }
    }

    /* Recalcular estado de cada job: si todos sus PIDs son 0 -> DONE */
    for (int j = 0; j < MAX_JOBS; ++j) {
        if (!jobs[j].used || jobs[j].state == JOB_DONE) continue;
        bool any_alive = false;
        for (int k = 0; k < jobs[j].npids; ++k) {
            if (jobs[j].pids[k] > 0) { any_alive = true; break; }
        }
        if (!any_alive) jobs[j].state = JOB_DONE;
    }
}

/* Inserta un trabajo nuevo en la tabla, devuelve su índice 1..N para mostrar en pantalla. */
static int jobs_add(pid_t *pids, int npids, const char *cmdline) {
    jobs_reap_nonblocking(); /* mantenemos limpio antes de insertar */
    for (int j = 0; j < MAX_JOBS; ++j) {
        if (!jobs[j].used) {
            jobs[j].used  = true;
            jobs[j].state = JOB_RUNNING;
            jobs[j].npids = (npids > MAX_CMDS_INJOB) ? MAX_CMDS_INJOB : npids;
            for (int k = 0; k < jobs[j].npids; ++k) jobs[j].pids[k] = pids[k];
            /* Copia segura del comando */
            if (cmdline) {
                strncpy(jobs[j].cmdline, cmdline, CMDSTR_LEN - 1);
                jobs[j].cmdline[CMDSTR_LEN - 1] = '\0';
            } else {
                snprintf(jobs[j].cmdline, CMDSTR_LEN, "(pipeline)");
            }
            return j + 1; /* numeración humana 1..MAX_JOBS */
        }
    }
    fprintf(stderr, "jobs: límite de trabajos alcanzado\n");
    return -1;
}

/* Imprime la lista de jobs con estado actualizado. */
static void builtin_jobs(void) {
    jobs_reap_nonblocking();
    bool any = false;
    for (int j = 0; j < MAX_JOBS; ++j) {
        if (!jobs[j].used) continue;
        any = true;
        printf("[%d]\t%s\t%s\n",
               j + 1,
               jobs[j].state == JOB_RUNNING ? "Running" : "Done",
               jobs[j].cmdline[0] ? jobs[j].cmdline : "(pipeline)");
    }
    if (!any) printf("No hay trabajos.\n");
}

/* fg <n>: trae el trabajo n al foreground y espera a TODOS sus procesos. */
static void builtin_fg(int idx1based) {
    if (idx1based <= 0 || idx1based > MAX_JOBS || !jobs[idx1based - 1].used) {
        fprintf(stderr, "fg: número de job inválido\n");
        return;
    }
    job_t *jb = &jobs[idx1based - 1];
    jobs_reap_nonblocking();
    if (jb->state == JOB_DONE) {
        fprintf(stderr, "fg: el trabajo ya ha finalizado\n");
        return;
    }

    printf("Reanudando [%d] %s\n", idx1based, jb->cmdline[0] ? jb->cmdline : "(pipeline)");
    fflush(stdout);

    /* Esperamos a TODOS los PIDs vivos de este job (bloqueante) */
    for (;;) {
        bool any_alive = false;
        for (int k = 0; k < jb->npids; ++k) {
            if (jb->pids[k] > 0) {
                any_alive = true;
                int status;
                pid_t w = waitpid(jb->pids[k], &status, 0); /* bloquea ese PID */
                if (w > 0) jb->pids[k] = 0;
            }
        }
        if (!any_alive) break;
        /* Si quedaran carreras, el bucle vuelve a comprobar */
    }

    jb->state = JOB_DONE;
}

/* ====== Helpers de impresión del comando para jobs ====== */

/* Construye un resumen legible de la línea para guardarlo en 'jobs' */
static void build_cmdline_summary(const tline *line, char *dst, size_t dstlen) {
    /* Formato: cmd1 [args] | cmd2 [args] ... con redirecciones y & si procede */
    dst[0] = '\0';
    size_t left = dstlen;
    for (int i = 0; i < line->ncommands; ++i) {
        const tcommand *c = &line->commands[i];
        for (int a = 0; a < c->argc; ++a) {
            int n = snprintf(dst + strlen(dst), left, "%s%s",
                             (a ? " " : (i ? " | " : "")),
                             c->argv[a]);
            if (n < 0 || (size_t)n >= left) { dst[dstlen - 1] = '\0'; return; }
            left -= (size_t)n;
        }
    }
    if (line->redirect_input) {
        int n = snprintf(dst + strlen(dst), left, " < %s", line->redirect_input);
        if (n > 0) left -= (size_t)n;
    }
    if (line->redirect_output) {
        int n = snprintf(dst + strlen(dst), left, " > %s", line->redirect_output);
        if (n > 0) left -= (size_t)n;
    }
    if (line->redirect_error) {
        int n = snprintf(dst + strlen(dst), left, " >& %s", line->redirect_error);
        if (n > 0) left -= (size_t)n;
    }
    if (line->background) {
        int n = snprintf(dst + strlen(dst), left, " &");
        if (n > 0) (void)n;
    }
}

/* ====== Validación de redirecciones según reglas del enunciado ====== */
static bool redirections_semantics_ok(const tline *line) {
    if (!line || line->ncommands <= 0) return false;
    /* El parser ya separa la semántica global. Nosotros nos aseguramos de aplicarlas SOLO
       en el primer/último mandato. La comprobación detallada se realiza en ejecución
       (i==0 para '<', i==n-1 para '>' y '>&'). Aquí basta con aceptar. */
    return true;
}

/* ====== Ejecución de una línea ====== */
static void execute_line(tline *line) {
    if (!line || line->ncommands <= 0) return;
    if (!redirections_semantics_ok(line)) {
        fprintf(stderr, "Error: redirecciones inválidas.\n");
        return;
    }

    const int n = line->ncommands;

    /* Preparamos (n-1) pipes si hay tubería */
    int (*pipes)[2] = NULL;
    if (n > 1) {
        pipes = calloc((size_t)(n - 1), sizeof(int[2]));
        if (!pipes) { perror("calloc pipes"); return; }
        for (int i = 0; i < n - 1; ++i) {
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
                for (int k = 0; k < i; ++k) { close(pipes[k][0]); close(pipes[k][1]); }
                free(pipes);
                return;
            }
        }
    }

    pid_t child_pids[MAX_CMDS_INJOB];
    int   child_count = 0;

    /* Lanzamos cada mandato */
    for (int i = 0; i < n; ++i) {
        tcommand *cmd = &line->commands[i];

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            /* cerrar y abortar */
            if (pipes) {
                for (int k = 0; k < n - 1; ++k) { close(pipes[k][0]); close(pipes[k][1]); }
                free(pipes);
            }
            return;
        }

        if (pid == 0) {
            /* ---- Hijo ---- */
            restore_default_signals_in_child();

            /* Redirecciones */
            if (i == 0 && line->redirect_input) {
                open_and_dup_or_exit(line->redirect_input, O_RDONLY, 0, STDIN_FILENO,
                                     "No se pudo abrir archivo de entrada");
            }
            if (i == n - 1 && line->redirect_output) {
                open_and_dup_or_exit(line->redirect_output,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644,
                                     STDOUT_FILENO, "No se pudo abrir archivo de salida");
            }
            if (i == n - 1 && line->redirect_error) {
                open_and_dup_or_exit(line->redirect_error,
                                     O_WRONLY | O_CREAT | O_TRUNC, 0644,
                                     STDERR_FILENO, "No se pudo abrir archivo de error");
            }

            /* Tubos */
            if (pipes) {
                /* stdin desde pipe anterior */
                if (i > 0) {
                    if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0)
                        die_child("dup2 stdin pipe: %s\n", strerror(errno));
                }
                /* stdout hacia pipe actual */
                if (i < n - 1) {
                    if (dup2(pipes[i][1], STDOUT_FILENO) < 0)
                        die_child("dup2 stdout pipe: %s\n", strerror(errno));
                }
                /* cerrar todos los extremos en el hijo */
                for (int k = 0; k < n - 1; ++k) {
                    close(pipes[k][0]);
                    close(pipes[k][1]);
                }
            }

            /* Ejecutar mandato */
            execvp(cmd->filename, cmd->argv);
            /* Si exec falla: */
            die_child("mandato: No se encuentra '%s' (%s)\n", cmd->filename, strerror(errno));
        }

        /* ---- Padre ---- */
        if (child_count < MAX_CMDS_INJOB) child_pids[child_count++] = pid;
    }

    /* Padre: cerramos todos los pipes */
    if (pipes) {
        for (int k = 0; k < n - 1; ++k) {
            close(pipes[k][0]); close(pipes[k][1]);
        }
        free(pipes);
    }

    if (line->background) {
        /* Registrar como job y NO esperar */
        char summary[CMDSTR_LEN];
        build_cmdline_summary(line, summary, sizeof(summary));
        int idx = jobs_add(child_pids, child_count, summary);
        if (idx > 0) {
            /* Mostrar algo tipo [N] pid=último */
            pid_t last = child_pids[child_count - 1];
            printf("[%d] %d en background\n", idx, (int)last);
            fflush(stdout);
        }
        return;
    }

    /* Foreground: esperar a TODOS los hijos de esta línea */
    for (int i = 0; i < child_count; ++i) {
        int status;
        while (waitpid(child_pids[i], &status, 0) < 0 && errno == EINTR) {
            /* Reintentar si fue interrumpido por señal */
        }
    }
}

/* ====== Bucle de lectura-ejecución ====== */

int main(void) {
    install_shell_signal_handlers();

    char  *buffer = NULL;
    size_t buflen = 0;

    for (;;) {
        /* Mantenemos la tabla de jobs limpia regularmente */
        jobs_reap_nonblocking();

        fputs(SHELL_PROMPT, stdout);
        fflush(stdout);

        ssize_t nread = getline(&buffer, &buflen, stdin);
        if (nread < 0) { putchar('\n'); break; }    /* EOF Ctrl+D o error -> salir */

        /* Línea vacía -> siguiente */
        if (nread == 1 && buffer[0] == '\n') continue;

        tline *line = tokenize(buffer);
        if (!line) continue;

        /* Builtins mínimos de la práctica */
        /* jobs */
        if (line->ncommands == 1 && strcmp(line->commands[0].filename, "jobs") == 0) {
            builtin_jobs();
            continue;
        }
        /* fg <n> */
        if (line->ncommands == 1 && strcmp(line->commands[0].filename, "fg") == 0) {
            if (line->commands[0].argc < 2) {
                fprintf(stderr, "Uso: fg <n>\n");
            } else {
                int idx = atoi(line->commands[0].argv[1]);
                builtin_fg(idx);
            }
            continue;
        }
        /* exit (opcional y habitual) */
        if (line->ncommands == 1 && strcmp(line->commands[0].filename, "exit") == 0) {
            break;
        }

        /* Ejecutar línea normal (con tuberías/redirecciones/& según 'line') */
        execute_line(line);
    }

    free(buffer);
    return 0;
}
