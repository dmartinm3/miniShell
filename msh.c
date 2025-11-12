#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include "parser.h"

#define MAX_JOBS 100

typedef struct {
    pid_t pid;
    char command[256];
    int running;
} Job;

Job jobs[MAX_JOBS];
int job_count = 0;

// ====================== FUNCIONES AUXILIARES ======================
void add_job(pid_t pid, const char *cmd) {
    if (job_count < MAX_JOBS) {
        jobs[job_count].pid = pid;
        strncpy(jobs[job_count].command, cmd, sizeof(jobs[job_count].command) - 1);
        jobs[job_count].running = 1;
        job_count++;
    }
}

void update_jobs() {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].running) {
            int status;
            pid_t result = waitpid(jobs[i].pid, &status, WNOHANG);
            if (result > 0) jobs[i].running = 0;
        }
    }
}

void print_jobs() {
    update_jobs();
    for (int i = 0; i < job_count; i++) {
        printf("[%d]\t%s\t%s\n", i + 1,
               jobs[i].running ? "Running" : "Done",
               jobs[i].command);
    }
}

void bring_fg(int n) {
    if (n <= 0 || n > job_count) {
        printf("fg: número de job inválido\n");
        return;
    }
    Job *job = &jobs[n - 1];
    if (!job->running) {
        printf("fg: proceso ya finalizado\n");
        return;
    }
    printf("Reanudando [%d] %s\n", n, job->command);
    int status;
    waitpid(job->pid, &status, 0);
    job->running = 0;
}

void sigint_handler(int sig) {
    // Ignorar Ctrl+C en la shell
    printf("\n");
}

// ====================== EJECUCIÓN DE COMANDOS ======================
void ejecutar_linea(tline *line) {
    if (!line) return;
    if (line->ncommands == 0) return;

    // Comando f "cd"
    if (strcmp(line->commands[0].filename, "cd") == 0) {
        const char *dir = line->commands[0].argv[1];
        if (!dir) dir = getenv("HOME");
        if (chdir(dir) != 0)
            perror("cd");
        return;
    }

    // Comando interno "jobs"
    if (strcmp(line->commands[0].filename, "jobs") == 0) {
        print_jobs();
        return;
    }

    // Comando interno "fg"
    if (strcmp(line->commands[0].filename, "fg") == 0) {
        if (line->commands[0].argc < 2)
            printf("Uso: fg <número_job>\n");
        else
            bring_fg(atoi(line->commands[0].argv[1]));
        return;
    }

    int num = line->ncommands;
    int pipes[num - 1][2];

    for (int i = 0; i < num - 1; i++)
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            exit(1);
        }

    for (int i = 0; i < num; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(1);
        }

        if (pid == 0) { // hijo
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            // Redirecciones
            if (i == 0 && line->redirect_input) {
                int fd = open(line->redirect_input, O_RDONLY);
                if (fd < 0) {
                    perror("Error al abrir archivo de entrada");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (i == num - 1 && line->redirect_output) {
                int fd = open(line->redirect_output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("Error al abrir archivo de salida");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            if (i == num - 1 && line->redirect_error) {
                int fd = open(line->redirect_error, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("Error al abrir archivo de error");
                    exit(1);
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            // Pipes
            if (i > 0)
                dup2(pipes[i - 1][0], STDIN_FILENO);
            if (i < num - 1)
                dup2(pipes[i][1], STDOUT_FILENO);

            for (int j = 0; j < num - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(line->commands[i].filename, line->commands[i].argv);
            fprintf(stderr, "mandato: No se encuentra el mandato '%s'\n", line->commands[i].filename);
            exit(1);
        }
        else {
            if (i == num - 1 && line->background) {
                add_job(pid, line->commands[0].filename);
                printf("[%d] %d en background\n", job_count, pid);
            }
        }
    }

    for (int i = 0; i < num - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    if (!line->background) {
        for (int i = 0; i < num; i++)
            wait(NULL);
    }
}

// ====================== MAIN ======================
int main() {
    char buffer[1024];
    signal(SIGINT, sigint_handler);
    signal(SIGQUIT, SIG_IGN);

    while (1) {
        update_jobs();
        printf("msh> ");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin))
            break;

        if (strcmp(buffer, "\n") == 0)
            continue;

        tline *line = tokenize(buffer);
        ejecutar_linea(line);
    }
    return 0;
}
