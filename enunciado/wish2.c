#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 64
#define MAX_PATHS 64

// Mensaje de error estándar
char error_message[30] = "An error has occurred\n";

// Ruta de búsqueda inicial
char *path[MAX_PATHS];

// Variable global para modo batch
int batch_mode = 0;

// ---------------------------------------------------
// Función para imprimir el mensaje de error
void print_error() {
    write(STDERR_FILENO, error_message, strlen(error_message));
}

// ---------------------------------------------------
// Prototipos
int execute_builtin(char **args);
void execute_command(char **args);
int handle_redirection(char **args);
void handle_parallel_commands(char *input);
char *preprocess_input(char *input);

// ---------------------------------------------------
// Función principal
int main(int argc, char *argv[]) {
    FILE *input_stream = stdin;
    char *input = NULL;
    size_t len = 0;

    // Inicializar path con /bin
    path[0] = strdup("/bin");
    path[1] = NULL;

    // Verificar modo batch o interactivo
    if (argc == 2) {
        input_stream = fopen(argv[1], "r");
        if (!input_stream) {
            print_error();
            exit(1);
        }
        batch_mode = 1;
    } else if (argc > 2) {
        print_error();
        exit(1);
    }

    // Bucle principal
    while (1) {
        if (!batch_mode) {
            printf("wish> ");
            fflush(stdout);
        }

        ssize_t read = getline(&input, &len, input_stream);
        if (read == -1) {  // EOF o error
            break;
        }

        // Eliminar salto de línea y retorno de carro
        input[strcspn(input, "\n")] = '\0';
        size_t l = strlen(input);
        if (l > 0 && input[l - 1] == '\r') {
            input[l - 1] = '\0';
        }

        // Ignorar líneas vacías
        if (strlen(input) == 0) continue;

        // Manejar comandos (puede incluir '&')
        handle_parallel_commands(input);
    }

    free(input);

    if (input_stream != stdin) {
        fclose(input_stream);
    }

    // Liberar memoria de path
    for (int i = 0; path[i] != NULL; i++) {
        free(path[i]);
    }

    return 0;
}

// ---------------------------------------------------
// Inserta espacios alrededor de '&'
char *preprocess_input(char *input) {
    size_t len = strlen(input);
    char *modified_input = malloc(len * 3 + 1);
    if (modified_input == NULL) {
        print_error();
        exit(1);
    }
    int idx = 0;
    for (size_t i = 0; i < len; i++) {
        if (input[i] == '&') {
            modified_input[idx++] = ' ';
            modified_input[idx++] = '&';
            modified_input[idx++] = ' ';
        } else {
            modified_input[idx++] = input[i];
        }
    }
    modified_input[idx] = '\0';
    return modified_input;
}

// ---------------------------------------------------
// Maneja múltiples comandos separados por '&'
void handle_parallel_commands(char *input) {
    char *commands[MAX_ARGS];
    int num_commands = 0;

    char *modified_input = preprocess_input(input);

    // Dividir por '&'
    char *token = strtok(modified_input, "&");
    while (token != NULL && num_commands < MAX_ARGS - 1) {
        // Quitar espacios alrededor
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) *end-- = '\0';

        if (*token != '\0') {
            commands[num_commands++] = token;
        }
        token = strtok(NULL, "&");
    }
    commands[num_commands] = NULL;

    pid_t pids[MAX_ARGS];
    int pid_index = 0;

    // Ejecutar cada comando
    for (int i = 0; i < num_commands; i++) {
        char *args[MAX_ARGS];
        int j = 0;

        token = strtok(commands[i], " \t");
        while (token != NULL && j < MAX_ARGS - 1) {
            args[j++] = token;
            token = strtok(NULL, " \t");
        }
        args[j] = NULL;

        if (args[0] != NULL) {
            // Built-ins
            if (strcmp(args[0], "exit") == 0) {
                if (args[1] != NULL) {
                    print_error();
                    continue;
                }
                for (int k = 0; path[k] != NULL; k++) free(path[k]);
                free(modified_input);
                free(input);
                exit(0);
            } else if (strcmp(args[0], "cd") == 0 || strcmp(args[0], "path") == 0) {
                int result = execute_builtin(args);
                if (result == -1) print_error();
                continue;
            }

            // Externos
            pid_t pid = fork();
            if (pid < 0) {
                print_error();
            } else if (pid == 0) {
                execute_command(args);
                exit(0);
            } else {
                pids[pid_index++] = pid;
            }
        }
    }

    // Esperar todos los hijos
    for (int i = 0; i < pid_index; i++) {
        waitpid(pids[i], NULL, 0);
    }

    free(modified_input);
}

// ---------------------------------------------------
// Ejecutar built-ins (cd, path)
int execute_builtin(char **args) {
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL || args[2] != NULL) {
            return -1;
        }
        if (chdir(args[1]) != 0) {
            return -1;
        }
        return 0;
    } else if (strcmp(args[0], "path") == 0) {
        for (int i = 0; path[i] != NULL; i++) {
            free(path[i]);
            path[i] = NULL;
        }

        int index = 0;
        for (int i = 1; args[i] != NULL && index < MAX_PATHS - 1; i++) {
            path[index++] = strdup(args[i]);
        }
        path[index] = NULL;
        return 0;
    }
    return -1;
}

// ---------------------------------------------------
// Ejecutar comando externo
void execute_command(char **args) {
    if (handle_redirection(args) == -1) {
        print_error();
        exit(1);
    }

    if (path[0] == NULL) {
        print_error();
        exit(1);
    }

    for (int i = 0; path[i] != NULL; i++) {
        char command_path[1024];
        snprintf(command_path, sizeof(command_path), "%s/%s", path[i], args[0]);
        if (access(command_path, X_OK) == 0) {
            execv(command_path, args);
            print_error();
            exit(1);
        }
    }

    print_error();
    exit(1);
}

// ---------------------------------------------------
// Manejar redirección de salida
int handle_redirection(char **args) {
    int i = 0, redirect_index = -1, redirect_count = 0;

    while (args[i] != NULL) {
        if (strcmp(args[i], ">") == 0) {
            redirect_count++;
            if (redirect_count > 1 || args[i + 1] == NULL || args[i + 2] != NULL) {
                return -1;
            }
            redirect_index = i;
        }
        i++;
    }

    if (redirect_index != -1) {
        if (redirect_index == 0) return -1;

        int fd = open(args[redirect_index + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return -1;
        if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
        args[redirect_index] = NULL;
    }

    return 0;
}