#import <stdio.h>
#import <stdlib.h>

int cd(char **args);
int path(char **args);
int exit(char **args);

char *builtin_str[] = {
  "cd",
  "path",
  "exit"
};

int (*builtin_func[]) (char **) = {
  &cd,
  &path,
  &exit
};

int num_builtins() {
  return sizeof(builtin_str) / sizeof(char *);
}

int cd(char **args)
{
  if (args[1] == NULL) {
    fprintf(stderr, "wish: expected argument to \"cd\"\n");
  } else {
    if (chdir(args[1]) != 0) {
      perror("wish");
    }
  }
  return 1;
}

int path(char **args)
{
  return 1;
}

int exit(char **args)
{
  return 0;
}

int READ_LINE_BUFFER_SIZE = 1024;
int TOKEN_BUFFER_SIZE = 64;
char *TOKEN_DELIMITERS = " \t\r\n\a";

int main(int argc, char *argv[]) {
  if (argc == 1) {
    //Interactive mode
    pepinowao_loop();
    return EXIT_SUCCESS;
  }

  if (argc == 2) {
    //Batch mode
    FILE *file = fopen(argv[1], "r");
    if (!file) {
      perror("wish");
      exit(EXIT_FAILURE);
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, file)) != -1) {
      char **args = split_lines(line);
      execute_command(args);
      free(args);
    }

    free(line);
    fclose(file);
    exit(EXIT_SUCCESS);
  }

  return EXIT_SUCCESS;
}

void pepinowao_loop(void) {
    char *line;
    char **args;
    int status;

    do {
        printf("wish> ");
        line = read_lines();
        args = split_lines(line);
        status = execute_command(args);

        free(line);
        free(args);
    } while (status);
}

char *read_lines(void) {
    char *line = NULL;
    ssize_t bufsize = 0;

    if (getline(&line, &bufsize, stdin) == -1) {
        if (feof(stdin)) {
            exit(EXIT_SUCCESS);
        } else {
            perror("getline");
            exit(EXIT_FAILURE);
        }
    }

    return line;
}

char **split_lines(char *line) {
    int bufsize = TOKEN_BUFFER_SIZE, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        fprintf(stderr, "wish: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, TOKEN_DELIMITERS);
    while (token != NULL) {
        tokens[position++] = token;

        if (position >= bufsize) {
            bufsize += TOKEN_BUFFER_SIZE;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "wish: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, TOKEN_DELIMITERS);
    }
    tokens[position] = NULL;
    return tokens;
}

int launch(char **args) {
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0) {
        if (execvp(args[0], args) == -1) {
            perror("wish");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("wish");
    } else {
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

int execute_command(char **args)
{
  int i;

  if (args[0] == NULL) {
    return 1;
  }

  for (i = 0; i < num_builtins(); i++) {
    if (strcmp(args[0], builtin_str[i]) == 0) {
      return (*builtin_func[i])(args);
    }
  }

  return launch(args);
}