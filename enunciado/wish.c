//Bibliotecas necesarias
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

//Límites de los argumentos y de las rutas
#define MAX_ARGS 64
#define MAX_PATHS 64

char error_message[30] = "An error has occurred\n"; //Mensaje de error que se usará en todo el programa
char *shell_path[MAX_PATHS]; //Array que almacena las rutas donde se encuentran los ejecutables
int batch_mode = 0; //Bandera que inidica si el shell entra a modo batch

//Declaración de funciones
void print_error();
int cd(char **args);
int path(char **args);
int redirection(char **args);
pid_t execute_external(char **args);
void execute_line(char *line);

//Función principal
int main(int argc, char *argv[]) {
    FILE *input = stdin;
    //Rutas por defecto
    shell_path[0] = strdup("./");
    shell_path[1] = strdup("/usr/bin/");
    shell_path[2] = strdup("/bin/");
    shell_path[3] = NULL;

    if (argc > 2) {
        print_error();
        exit(1);
    }

    //Si hay dos argumentos, estos serán el nombre del programa y un archivo para el modo batch
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

    while (1) { //Bucle infinito para el shell
        if (!batch_mode) { //Modo interactivo
            printf("wish> ");
        }

        if (getline(&line, &len, input) == -1) { //Si se llega al final del archivo en modo batch o hay un error se sale del shell
            for (int i = 0; shell_path[i] != NULL; i++) {
                free(shell_path[i]);
            }

            free(line);
            exit(0);
        }

        //Se ejecuta la línea leída
        execute_line(line);
    }
}

//Mensaje de error estándar
void print_error() {
    write(STDERR_FILENO, error_message, strlen(error_message));
}

//Función para cambiar de directorio
int cd(char **args) {
    if (args[1] == NULL || args[2] != NULL) { //Si no hay argumento o hay más de uno, error
        return -1;
    }

    if (chdir(args[1]) != 0) { //Error si falla el cambio de directorio
        return -1;
    }

    return 0;
}

int path(char **args) {
    //Se limpia el path anterior
    for (int i = 0; i < MAX_PATHS; i++) {
        if (shell_path[i] != NULL) {
            free(shell_path[i]);
        }

        shell_path[i] = NULL;
    }

    //Se añaden las nuevas rutas
    int idx = 1;
    int j = 0;

    while (args[idx] != NULL && j < MAX_PATHS - 1) {
        shell_path[j++] = strdup(args[idx++]);
    }

    shell_path[j] = NULL;
    return 0;
}

//Función para manejar la redirección de salida
int redirection(char **args) {
    int redirect_count = 0, idx = -1;
    
    //Cuenta cuantos signos de redirección hay
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], ">") == 0) {
            redirect_count++;
            idx = i;
        }
    }

    if (redirect_count == 0) {
        return 0;
    }

    //Si hay más de un signo de redirección o la sintaxis es incorrecta, error
    if (redirect_count > 1 || idx == 0 || args[idx+1] == NULL || args[idx+2] != NULL) {
        return -1;
    }

    //0666 (numero octal para permisos) = primer 6 (usuario), segundo 6 (grupo), tercer 6 (otros)
    int fd = open(args[idx+1], O_CREAT | O_WRONLY | O_TRUNC, 0666); 

    //Si no se puede abrir el archivo, error
    if (fd < 0) {
        return -1;
    }

    //Redirige la salida estándar y de error al archivo
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        close(fd);
        return -1;
    }

    close(fd);

    args[idx] = NULL;
    args[idx+1] = NULL;

    return 0;
}

//Función para ejecutar comandos externos
pid_t execute_external(char **args) {
    //Si no hay rutas en el path, error
    if (shell_path[0] == NULL) {
        print_error();
        return -1;
    }

    pid_t pid = fork(); //Crea un proceso hijo

    if (pid == 0) { //Si es el proceso hijo
        //Maneja la redirección si es necesaria
        if (redirection(args) == -1) {
            print_error();
            _exit(1);
        }

        //Busca el ejecutable en las rutas del path
        for (int i = 0; shell_path[i] != NULL; i++) {
            char full[512];
            //Construye la ruta completa del ejecutable
            snprintf(full, sizeof(full), "%s/%s", shell_path[i], args[0]);

            //Si el archivo es ejecutable, lo ejecuta
            if (access(full, X_OK) == 0) {
                execv(full, args);
                print_error();
                _exit(1);
            }
        }

        print_error();
        _exit(1);
    } else if (pid < 0) { //Error al crear el proceso hijo
        print_error();
        return -1;
    }

    return pid;
}

//Función para ejecutar una línea de comandos
void execute_line(char *line) {
    char *commands[MAX_ARGS]; //Array para almacenar los comandos separados por '&'
    int num_cmds = 0; //Número de comandos a ejecutar
    pid_t pids[MAX_ARGS]; //Array para almacenar los PIDs de los procesos hijos

    char *cmd = strtok(line, "&"); //Separa la línea por '&'

    //Procesa cada comando separado
    while (cmd != NULL && num_cmds < MAX_ARGS - 1) {
        while (*cmd == ' ' || *cmd == '\t') { //Elimina espacios al inicio
            cmd++;
        }

        char *end = cmd + strlen(cmd) - 1; //Elimina espacios al final

        while (end > cmd && (*end == ' ' || *end == '\t' || *end == '\n')) { //Elimina espacios, tabulaciones y saltos de línea al final
            *end = '\0'; end--; 
        }

        commands[num_cmds++] = cmd; //Almacena el comando limpio
        cmd = strtok(NULL, "&"); //Se tokeniza el siguiente comando
    }

    //Ejecuta cada comando
    for (int i = 0; i < num_cmds; i++) {
        char *args[MAX_ARGS];
        int n = 0;

        char *tok = strtok(commands[i], " \t\n");

        //Separa el comando en argumentos
        while (tok != NULL && n < MAX_ARGS - 1) {
            args[n++] = tok;
            tok = strtok(NULL, " \t\n"); //Tokeniza el siguiente argumento
        }

        args[n] = NULL;

        //Si no hay comando, continúa al siguiente
        if (args[0] == NULL) {
            pids[i] = -1;
            continue;
        }

        //Manejo de comandos internos
        if (strcmp(args[0], "exit") == 0) {
            if (args[1] != NULL) {
                print_error();
            } else {
                for (int k = 0; shell_path[k] != NULL; k++) {
                    free(shell_path[k]);
                }
                exit(0);
            }

            pids[i] = -1;
        }
        else if (strcmp(args[0], "cd") == 0) {
            if (cd(args) == -1) {
                print_error();
            }

            pids[i] = -1;
        }
        else if (strcmp(args[0], "path") == 0) {
            if (path(args) == -1) {
                print_error();
            }

            pids[i] = -1;
        }
        else {
            pids[i] = execute_external(args); //Ejecuta comandos externos
        }
    }

    //Espera a que terminen todos los procesos hijos
    for (int i = 0; i < num_cmds; i++) {
        if (pids[i] > 0) {
            waitpid(pids[i], NULL, 0);
        }
    }
}