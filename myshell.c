/*
 * Práctica de Sistemas Operativos - MiniShell
 * Autores: Héctor Julián Alijas y Daniel Martín
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "parser.h"

#define PROMPT "msh> "
/* Límites */
#define MAX_LINE 1024
#define MAX_JOBS 20
#define MAX_PIDS_PER_JOB 32

/* Estados posibles de un trabajo en background */
typedef enum { LIBRE, EJECUTANDO, FINALIZADO } estado_t;

/* Estructura para gestionar trabajos en segundo plano */
typedef struct {
    pid_t pids[MAX_PIDS_PER_JOB];
    int npids;
    char mandato[MAX_LINE];
    estado_t estado;
} job_t;

job_t jobs[MAX_JOBS];

/* Variable global para reenvío de señales */
static volatile pid_t fg_pid = 0;

/* --- Manejadores de Señales --- */

/* Manejador de SIGINT (Ctrl+C) */
void manejador_ctrl_c(int sig) {
    (void)sig;
    signal(SIGINT, manejador_ctrl_c);

    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
    } else {
        printf("\n");
        fflush(stdout);
    }
}

/* Manejador de SIGQUIT (Ctrl+\) */
void manejador_ctrl_quit(int sig) {
    (void)sig;
    signal(SIGQUIT, manejador_ctrl_quit);

    if (fg_pid > 0) {
        kill(fg_pid, SIGQUIT);
    }
}

/* --- Gestión de Jobs --- */

/* Inicialización de la tabla de trabajos al arrancar */
void init_jobs() {
    int i;
    for (i = 0; i < MAX_JOBS; i++) {
        jobs[i].estado = LIBRE;
        jobs[i].npids = 0;
    }
}

/* Añadir un nuevo trabajo a la primera posición libre */
void add_job(pid_t *pids, int n, char *linea) {
    int i, j;
    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].estado == LIBRE) {
            jobs[i].npids = n;
            for(j = 0; j < n; j++) {
                jobs[i].pids[j] = pids[j];
            }
            jobs[i].estado = EJECUTANDO;
            strncpy(jobs[i].mandato, linea, MAX_LINE - 1);
            jobs[i].mandato[strcspn(jobs[i].mandato, "\n")] = 0;

            printf("[%d] %d\n", i + 1, pids[n-1]);
            return;
        }
    }
    fprintf(stderr, "jobs: límite de trabajos alcanzado\n");
}

/* Limpiar procesos terminados */
void check_jobs(int notificar) {
    int i, j;
    int status;
    pid_t pid;
    int vivos;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (i = 0; i < MAX_JOBS; i++) {
            if (jobs[i].estado == EJECUTANDO) {
                vivos = 0;
                for (j = 0; j < jobs[i].npids; j++) {
                    if (jobs[i].pids[j] == pid) {
                        jobs[i].pids[j] = 0;
                    }
                    if (jobs[i].pids[j] > 0) {
                        vivos++;
                    }
                }
                if (vivos == 0) {
                    jobs[i].estado = FINALIZADO;
                    if (notificar) {
                        printf("[%d]+ Done\t%s\n", i + 1, jobs[i].mandato);
                        jobs[i].estado = LIBRE;
                    }
                }
            }
        }
    }
}

/* --- Comandos Internos --- */

/* Comando 'jobs' */
void builtin_jobs() {
    check_jobs(0); /* Actualizar estados sin imprimir notificaciones automáticas */
    int i;
    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].estado == EJECUTANDO) {
            printf("[%d]+ Running\t%s\n", i + 1, jobs[i].mandato);
        } else if (jobs[i].estado == FINALIZADO) {
            printf("[%d]+ Done\t%s\n", i + 1, jobs[i].mandato);
            jobs[i].estado = LIBRE; /* Limpiar tras mostrar */
        }
    }
}

/* Comando 'fg' */
void builtin_fg(char **argv) {
    int pos = -1;
    int i, j;
    int status;

    check_jobs(0);

    if (argv[1] == NULL) {
        for (i = MAX_JOBS - 1; i >= 0; i--) {
            if (jobs[i].estado == EJECUTANDO) {
                pos = i;
                break;
            }
        }
    } else {
        pos = atoi(argv[1]) - 1;
    }

    /* Validaciones de errores */
    if (pos < 0 || pos >= MAX_JOBS || jobs[pos].estado == LIBRE) {
        fprintf(stderr, "fg: no existe ese trabajo\n");
        return;
    }

    if (jobs[pos].estado == FINALIZADO) {
        fprintf(stderr, "fg: el trabajo ya ha finalizado\n");
        jobs[pos].estado = LIBRE;
        return;
    }

    printf("%s\n", jobs[pos].mandato);

    for (j = 0; j < jobs[pos].npids; j++) {
        if (jobs[pos].pids[j] > 0) {
            fg_pid = jobs[pos].pids[j];
            waitpid(jobs[pos].pids[j], &status, 0);

            if (WIFSIGNALED(status)) {
                if (WTERMSIG(status) == SIGQUIT) {
                    printf("Quit (core dumped)\n");
                } else if (WTERMSIG(status) == SIGINT) {
                    printf("\n");
                }
            }
        }
    }

    fg_pid = 0;
    jobs[pos].estado = LIBRE;
}

/* Comando 'cd' */
void builtin_cd(char **argv) {
    char *dir;
    char cwd[MAX_LINE];

    if (argv[1] == NULL) {
        dir = getenv("HOME");
        if (dir == NULL) {
            fprintf(stderr, "cd: variable HOME no definida\n");
            return;
        }
    } else {
        dir = argv[1];
    }

    if (chdir(dir) != 0) {
        fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
    } else {
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        }
    }
}

/* Comando 'exit' */
void builtin_exit(char **argv) {
    int status = 0;
    if (argv[1] != NULL) status = atoi(argv[1]);
    exit(status);
}

/* --- Main --- */

int main(void) {
    char buf[MAX_LINE];
    tline *line;
    int i, j;
    pid_t pid;
    int **pipes;
    pid_t pids_hijos[MAX_PIDS_PER_JOB];
    int status;

    /* Configuración inicial de señales */
    signal(SIGINT, manejador_ctrl_c);
    signal(SIGQUIT, manejador_ctrl_quit);

    init_jobs();

    while (1) {
        check_jobs(1); /* Limpiar zombies y notificar antes del prompt */
        printf("%s", PROMPT);

        /* Lectura de prompt */
        if (fgets(buf, MAX_LINE, stdin) == NULL) {
            if (ferror(stdin) && errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            break;
        }

        line = tokenize(buf);
        if (line == NULL || line->ncommands == 0) continue;

        /* Comandos Internos */
        if (strcmp(line->commands[0].argv[0], "exit") == 0) {
            builtin_exit(line->commands[0].argv);
        }
        if (strcmp(line->commands[0].argv[0], "cd") == 0) {
            builtin_cd(line->commands[0].argv);
            continue;
        }
        if (strcmp(line->commands[0].argv[0], "jobs") == 0) {
            builtin_jobs();
            continue;
        }
        if (strcmp(line->commands[0].argv[0], "fg") == 0) {
            builtin_fg(line->commands[0].argv);
            continue;
        }

        /* Gestión de pipes */
        pipes = NULL;
        if (line->ncommands > 1) {
            pipes = (int **)malloc((line->ncommands - 1) * sizeof(int *));
            for (i = 0; i < line->ncommands - 1; i++) {
                pipes[i] = (int *)malloc(2 * sizeof(int));
                if (pipe(pipes[i]) < 0) {
                    perror("pipe");
                    exit(1);
                }
            }
        }

        /* Bucle de creación de hijos */
        for (i = 0; i < line->ncommands; i++) {
            pid = fork();

            if (pid < 0) {
                perror("fork");
                exit(1);
            }

            /* --- Poceso hijo --- */
            if (pid == 0) {
                if (line->background) {
                    setpgid(0, 0);
                } else {
                    signal(SIGINT, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);
                }

                /* Redirecciones */
                if (i == 0 && line->redirect_input != NULL) {
                    FILE *f = fopen(line->redirect_input, "r");
                    if (f == NULL) {
                        fprintf(stderr, "%s: Error. %s\n", line->redirect_input, strerror(errno));
                        exit(1);
                    }
                    dup2(fileno(f), STDIN_FILENO);
                    fclose(f);
                }

                if (i == line->ncommands - 1 && line->redirect_output != NULL) {
                    FILE *f = fopen(line->redirect_output, "w");
                    if (f == NULL) {
                        fprintf(stderr, "%s: Error. %s\n", line->redirect_output, strerror(errno));
                        exit(1);
                    }
                    dup2(fileno(f), STDOUT_FILENO);
                    fclose(f);
                }

                if (line->redirect_error != NULL) {
                    FILE *f = fopen(line->redirect_error, "w");
                    if (f == NULL) {
                        fprintf(stderr, "%s: Error. %s\n", line->redirect_error, strerror(errno));
                        exit(1);
                    }
                    dup2(fileno(f), STDERR_FILENO);
                    fclose(f);
                }

                /* Conexión de tuberías */
                if (line->ncommands > 1) {
                    if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);
                    if (i < line->ncommands - 1) dup2(pipes[i][1], STDOUT_FILENO);
                    for (j = 0; j < line->ncommands - 1; j++) {
                        close(pipes[j][0]);
                        close(pipes[j][1]);
                    }
                }

                /* Comprobación de existencia del mandato antes de ejecutar */
                if (line->commands[i].filename == NULL) {
                    fprintf(stderr, "Mandato: Error. No se encuentra el mandato %s\n", line->commands[i].argv[0]);
                    exit(1);
                }

                execvp(line->commands[i].filename, line->commands[i].argv);

                /* Fallback por seguridad */
                fprintf(stderr, "Mandato: Error. No se encuentra el mandato %s\n", line->commands[i].argv[0]);
                exit(1);
            }

            /* Proceso padre: guardar PID del hijo creado */
            if (i < MAX_PIDS_PER_JOB) pids_hijos[i] = pid;
        }

        /* Limpieza de pipes en el padre */
        if (line->ncommands > 1) {
            for (i = 0; i < line->ncommands - 1; i++) {
                close(pipes[i][0]);
                close(pipes[i][1]);
                free(pipes[i]);
            }
            free(pipes);
        }

        /* Gestión foreground / background */
        if (line->background) {
            add_job(pids_hijos, line->ncommands, buf);
        } else {
            for (i = 0; i < line->ncommands; i++) {
                fg_pid = pids_hijos[i];
                waitpid(pids_hijos[i], &status, 0);

                if (WIFSIGNALED(status)) {
                    if (WTERMSIG(status) == SIGQUIT) {
                        printf("Quit (core dumped)\n");
                    } else if (WTERMSIG(status) == SIGINT) {
                        printf("\n");
                    }
                }
            }
            fg_pid = 0;
        }
    }
    return 0;
}