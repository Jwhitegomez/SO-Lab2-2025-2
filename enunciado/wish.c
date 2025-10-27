#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdbool.h>

#define MAX_PATH 100
#define MAX_ARGS 100

char error_message[30] = "An error has occurred\n";
char *shell_path[MAX_PATH] = {"/bin", NULL};

// ----------------------------------------------------

void print_error() {
    write(STDERR_FILENO, error_message, strlen(error_message));
}

// ----------------------------------------------------

bool is_builtin(char *cmd) {
    return (strcmp(cmd, "exit") == 0 ||
            strcmp(cmd, "cd") == 0 ||
            strcmp(cmd, "path") == 0);
}

// ----------------------------------------------------

int builtin_cd(char **args) {
    if (args[1] == NULL || args[2] != NULL) {
        print_error();
        return 1;
    }
    if (chdir(args[1]) != 0) {
        print_error();
    }
    return 1;
}

int builtin_exit(char **args) {
    if (args[1] != NULL) {
        print_error();
        return 1;
    }
    exit(0);
}

int builtin_path(char **args) {
    for (int i = 0; i < MAX_PATH; i++)
        shell_path[i] = NULL;

    int i = 1;
    while (args[i] != NULL && i < MAX_PATH) {
        shell_path[i - 1] = args[i];
        i++;
    }
    return 1;
}

// ----------------------------------------------------

char *find_executable(char *cmd) {
    static char fullpath[256];

    for (int i = 0; shell_path[i] != NULL; i++) {
        snprintf(fullpath, sizeof(fullpath), "%s/%s",
                 shell_path[i], cmd);
        if (access(fullpath, X_OK) == 0)
            return fullpath;
    }
    return NULL;
}

// ----------------------------------------------------

int run_external(char **args, char *outfile) {
    pid_t pid = fork();
    if (pid == 0) {
        if (outfile != NULL) {
            int fd = open(outfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
            if (fd < 0) {
                print_error();
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        char *exec_path = find_executable(args[0]);
        if (!exec_path) {
            print_error();
            exit(1);
        }

        execv(exec_path, args);
        print_error();
        exit(1);

    } else if (pid < 0) {
        print_error();
    }
    return pid;
}

// ----------------------------------------------------

void execute_line(char *line) {
    char *commands[50];
    int num_cmds = 0;

    char *token = strtok(line, "&");
    while (token != NULL) {
        commands[num_cmds++] = token;
        token = strtok(NULL, "&");
    }

    pid_t pids[num_cmds];

    for (int i = 0; i < num_cmds; i++) {
        char *cmd = commands[i];

        char *args[MAX_ARGS];
        char *outfile = NULL;

        int j = 0;
        char *tok = strtok(cmd, " \t\n");
        while (tok != NULL) {
            if (strcmp(tok, ">") == 0) {
                tok = strtok(NULL, " \t\n");
                if (tok == NULL || strtok(NULL, " \t\n") != NULL) {
                    print_error();
                    return;
                }
                outfile = tok;
                break;
            }
            args[j++] = tok;
            tok = strtok(NULL, " \t\n");
        }
        args[j] = NULL;

        if (args[0] == NULL)
            return;

        if (is_builtin(args[0])) {
            if (strcmp(args[0], "cd") == 0) builtin_cd(args);
            else if (strcmp(args[0], "exit") == 0) builtin_exit(args);
            else if (strcmp(args[0], "path") == 0) builtin_path(args);
            pids[i] = -1;
        } else {
            if (shell_path[0] == NULL) {
                print_error();
                return;
            }
            pids[i] = run_external(args, outfile);
        }
    }

    for (int i = 0; i < num_cmds; i++)
        if (pids[i] > 0) waitpid(pids[i], NULL, 0);
}

// ----------------------------------------------------

int main(int argc, char *argv[]) {
    if (argc > 2) {
        print_error();
        exit(1);
    }

    FILE *input = stdin;
    bool batch = false;

    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (!input) {
            print_error();
            exit(1);
        }
        batch = true;
    }

    char *line = NULL;
    size_t len = 0;

    while (1) {
        if (!batch) printf("wish> ");
        if (getline(&line, &len, input) == -1)
            exit(0);

        execute_line(line);
    }
}
