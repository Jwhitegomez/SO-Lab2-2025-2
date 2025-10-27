#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 64
#define MAX_PATHS 64

char error_message[30] = "An error has occurred\n";
char *shell_path[MAX_PATHS];
int batch_mode = 0;

void print_error() {
    write(STDERR_FILENO, error_message, strlen(error_message));
}

/** Inserta espacios alrededor de '&' */
void normalize_ampersand(char *line) {
    char tmp[1024];
    int j = 0;
    for (int i = 0; line[i]; i++) {
        if (line[i] == '&') {
            tmp[j++] = ' ';
            tmp[j++] = '&';
            tmp[j++] = ' ';
        } else {
            tmp[j++] = line[i];
        }
    }
    tmp[j] = '\0';
    strcpy(line, tmp);
}

/** Comandos internos */
int builtin_cd(char **args) {
    if (args[1] == NULL || args[2] != NULL) return -1;
    if (chdir(args[1]) != 0) return -1;
    return 0;
}

int builtin_path(char **args) {
    for (int i = 0; i < MAX_PATHS; i++) {
        free(shell_path[i]);
        shell_path[i] = NULL;
    }
    int idx = 1;
    int j = 0;
    while (args[idx] != NULL && j < MAX_PATHS - 1) {
        shell_path[j++] = strdup(args[idx++]);
    }
    shell_path[j] = NULL;
    return 0;
}

/** Manejar redirección */
int setup_redirection(char **args) {
    int redirect_count = 0, idx = -1;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            redirect_count++;
            idx = i;
        }
    }
    if (redirect_count == 0) return 0;

    if (redirect_count > 1 || idx == 0 || args[idx+1] == NULL || args[idx+2] != NULL) {
        return -1;
    }

    int fd = open(args[idx+1], O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd < 0) return -1;

    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    args[idx] = NULL;
    return 0;
}

/** Ejecutar comando externo */
pid_t execute_external(char **args) {
    if (shell_path[0] == NULL) {
        print_error();
        return -1;
    }

    int red = setup_redirection(args);
    if (red == -1) {
        print_error();
        return -1;
    }

    pid_t pid = fork();
    if (pid == 0) {
        for (int i = 0; shell_path[i] != NULL; i++) {
            char full[512];
            snprintf(full, sizeof(full), "%s/%s", shell_path[i], args[0]);
            if (access(full, X_OK) == 0) {
                execv(full, args);
                break;
            }
        }
        print_error();
        exit(1);
    } else if (pid < 0) {
        print_error();
        return -1;
    }
    return pid;
}

/** Analizar línea con múltiples & */
void execute_line(char *line) {
    char *commands[MAX_ARGS];
    int num_cmds = 0;
    pid_t pids[MAX_ARGS];

    normalize_ampersand(line);

    char *cmd = strtok(line, "&");
    while (cmd != NULL && num_cmds < MAX_ARGS - 1) {
        commands[num_cmds++] = cmd;
        cmd = strtok(NULL, "&");
    }

    for (int i = 0; i < num_cmds; i++) {
        char *args[MAX_ARGS];
        int n = 0;

        char *tok = strtok(commands[i], " \t\n");
        while (tok != NULL && n < MAX_ARGS - 1) {
            args[n++] = tok;
            tok = strtok(NULL, " \t\n");
        }
        args[n] = NULL;

        if (args[0] == NULL) {
            pids[i] = -1;
            continue;
        }

        if (strcmp(args[0], "exit") == 0) {
            if (args[1] != NULL) {
                print_error();
            } else {
                exit(0);
            }
            pids[i] = -1;
        }
        else if (strcmp(args[0], "cd") == 0) {
            if (builtin_cd(args) == -1) print_error();
            pids[i] = -1;
        }
        else if (strcmp(args[0], "path") == 0) {
            if (builtin_path(args) == -1) print_error();
            pids[i] = -1;
        }
        else {
            pids[i] = execute_external(args);
        }
    }

    for (int i = 0; i < num_cmds; i++) {
        if (pids[i] > 0)
            waitpid(pids[i], NULL, 0);
    }
}

/** MAIN */
int main(int argc, char *argv[]) {
    FILE *input = stdin;
    shell_path[0] = strdup("/bin");
    shell_path[1] = NULL;

    if (argc > 2) {
        print_error();
        exit(1);
    }

    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (!input) {
            print_error();
            exit(1);
        }
        batch_mode = 1;
    }

    char *line = NULL;
    size_t len = 0;

    while (1) {
        if (!batch_mode) {
            printf("wish> ");
            fflush(stdout);
        }

        if (getline(&line, &len, input) == -1) {
            exit(0);
        }

        execute_line(line);
    }
}
