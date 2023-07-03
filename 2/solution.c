#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define READ 0
#define WRITE 1

typedef struct {
    char *name;
    char **args;
    int arg_count;
} cmd;

struct redirect_info {
    char *to_file;
    bool is_appending;
    bool has_redirect;
};


int exec_commands(cmd* commands, int size, struct redirect_info *redirect) {
    int cmds[size];
    int fd[2];
    int last_fd = 0;

    int status = 0;

    if (redirect->has_redirect && !redirect->to_file) {
        printf("sh: syntax error near unexpected token `newline'");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < size; i++) {
        if (strcmp(commands[i].name, "cd") == 0) {
            if (chdir(commands[i].args[1]) == -1) {
                printf("cd: %s: No such file or directory\n", commands[i].args[1]);
            }
            continue;
        }

        if (pipe(fd) != 0) {
            printf("%s\n", strerror(errno));
            return errno;
        }

        cmds[i] = fork();

        if (cmds[i] == 0) {
            if (i == (size - 1) && redirect->has_redirect) {
                int redirect_ds;

                mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | S_IWOTH;

                if (!redirect->is_appending) {
                    redirect_ds = open(redirect->to_file, O_WRONLY | O_CREAT | O_TRUNC, mode);
                } else {
                    redirect_ds = open(redirect->to_file, O_WRONLY | O_CREAT | O_APPEND, mode);
                }

                if (redirect_ds == -1) {
                    printf("Redirect failed\n");
                    printf("%s\n", strerror(errno));
                    return errno;
                }

                dup2(redirect_ds, STDOUT_FILENO);
                close(redirect_ds);
            }

            dup2(last_fd, STDIN_FILENO);

            if (i != (size - 1)) {
                dup2(fd[WRITE], STDOUT_FILENO);
            }
            close(fd[READ]);

            if (execvp(commands[i].name, commands[i].args) == -1) {
                printf("Error occurred while executing command %s\n", commands[i].name);
                printf("%s\n", strerror(errno));
                _exit(errno);
            }
            _exit(EXIT_FAILURE);
        } else {
            close(fd[WRITE]);
            last_fd = fd[READ];
        }
    }

    for (int i = 0; i < size; i++) {
        if (strcmp(commands[i].name, "cd") == 0) continue;
        if (waitpid(cmds[i], &status, 0) != cmds[i]) {
            break;
        }
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
}

void test_commands(cmd* commands, int size, struct redirect_info *redirect) {
    for (int i = 0; i < size; i++) {
        printf("Command #%d: %s\n", i + 1, commands[i].name);
        for (int j = 0; j < commands[i].arg_count; j++) {
            printf("Argument #%d: %s\n", j + 1, commands[i].args[j]);
        }
    }

    if (redirect->has_redirect) {
        printf("Redirecting to file: %s. Is appending: %d\n", redirect->to_file, redirect->is_appending);
    }
}

void write_command(cmd* commands, char *buf, int cmd_count, int *curr_word) {
    if (*curr_word == 0) {
        commands[cmd_count - 1].name = strdup(buf);
        commands[cmd_count - 1].args = (char**) malloc(sizeof(char*));
    } else if (*curr_word > 0) {
        char **tmp;
        tmp = realloc(commands[cmd_count - 1].args, (*curr_word + 2) * sizeof(char*));

        if (tmp == NULL) {
            free(commands[cmd_count - 1].args);
            exit(EXIT_FAILURE);
        } else {
            commands[cmd_count - 1].args = tmp;
        }
    }

    commands[cmd_count - 1].args[*curr_word] = strdup(buf);

    *curr_word = *curr_word + 1;
    commands[cmd_count - 1].arg_count = *curr_word;

    memset(buf, '\0', strlen(buf));
}

void free_commands(cmd* commands, int size) {
    for (int i = 0; i < size; i++) {
        free(commands[i].name);
        for (int j = 0; j < commands[i].arg_count; j++) {
            free(commands[i].args[j]);
        }
        free(commands[i].args);
    }
    free(commands);
}


int main() {
    int EXIT_CODE = 0;

    while(true) {
        printf("> ");
        cmd *commands = (cmd*) malloc(sizeof(cmd));
        struct redirect_info *redirect = (struct redirect_info*) malloc(sizeof(struct redirect_info));
        redirect->is_appending = false;
        redirect->has_redirect = false;

        char *buf = (char*) calloc(1, sizeof(char));

        int cmd_count = 1;
        int curr_word = 0;

        size_t size = 0;
        char *line = NULL;
        char *lines = calloc(1, sizeof(char));
        bool eol = false;

        while (getline(&line, &size, stdin) != -1) {
            size_t line_len = line ? strlen(line) : 0;

            if (line_len >= 2 && line[line_len - 2] == '\\' && line[line_len - 1] == '\n') {
                line[strlen(line) - 2] = '\0';
            } else if (line_len == 1 && line[line_len - 1] == '\n') {
                eol = true;
            } else {
                line[strlen(line) - 1] = '\0';
                eol = true;
            }
            char *tmp;

            tmp = realloc(lines, line_len + strlen(lines) + 1);
            if (tmp == NULL) {
                free(lines);
                exit(EXIT_FAILURE);
            } else {
                lines = tmp;
            }

            strcat(lines, line);
            if (eol) {
                lines = realloc(lines, strlen(lines) + 3);
                lines[strlen(lines)] = ' ';
                lines[strlen(lines) + 1] = ' ';
                lines[strlen(lines) + 2] = '\0';
                break;
            }
        }

        if  (lines[0] == '#' ||
            (strlen(lines) == 1
            && ((lines[strlen(lines) - 1] == '\n')
            || (lines[strlen(lines) - 1] == '#')))) {

            free(line);
            free(lines);
            free(buf);
            free(commands);
            eol = false;
            continue;
        }

        char current_quote = 'a';
        bool inside_quotation = false;
        bool escaping_outside_quotes = false;

        for (size_t c = 0; c < (strlen(lines) - 1); c++) {
            size_t len = buf ? strlen(buf) : 0;

            if (!inside_quotation && lines[c] == '#') {
                break;
            }

            if (!inside_quotation && lines[c] == '\\' && lines[c + 1] == ' ') {
                // printf("Escape\n");
                escaping_outside_quotes = true;
            }

            if (escaping_outside_quotes && lines[c] != '\\' && !isspace(lines[c])) {
                escaping_outside_quotes = false;
            }

            if (lines[c] == '"' || lines[c] == '\'') {
                if (inside_quotation) {
                    if (current_quote == lines[c]) {
                        inside_quotation = false;
                    }
                } else {
                    current_quote = lines[c];
                    inside_quotation = true;
                    continue;
                }
            }

            if ((!isspace(lines[c]) && lines[c] != '|' && lines[c] != '>' && !inside_quotation) || inside_quotation || escaping_outside_quotes) {
                if (lines[c] == '\\') continue;

                if (!inside_quotation && (lines[c] == '"' || lines[c] == '\'')) {
                    // continue;
                } else {
                    char *tmp;
                    if (c == (strlen(lines) - 2) && !isspace(lines[c + 1])) {
                        tmp = realloc(buf, len + 3);
                    } else {
                        tmp = realloc(buf, len + 2);
                    }

                    if (tmp == NULL) {
                        free(buf);
                        exit(EXIT_FAILURE);
                    } else {
                        buf = tmp;
                    }

                    buf[len] = lines[c];

                    if (c == (strlen(lines) - 2) && (!isspace(lines[c + 1])) && (lines[c + 1] != '\\')) {
                        buf[len + 1] = lines[c + 1];
                        buf[len + 2] = '\0';
                    } else {
                        buf[len + 1] = '\0';
                    }
                    // printf("Command: %s\n", buf);
                    len = strlen(buf);
                }
            }

            if (!inside_quotation && !isspace(lines[c]) &&
                (lines[c + 1] == '|' || lines[c + 1] == '>') && lines[c] != '>') {
                write_command(commands, buf, cmd_count, &curr_word);
                continue;
            }

            if (!inside_quotation && lines[c] == '>') {
                redirect->has_redirect = true;
                if (lines[c + 1] == '>') {
                    redirect->is_appending = true;
                }
                commands[cmd_count - 1].args[curr_word] = NULL;
                continue;
            }

            if (!inside_quotation && lines[c] == '|') {
                commands[cmd_count - 1].args[curr_word] = NULL;
                cmd *tmp;
                tmp = realloc(commands, sizeof(cmd) * (++cmd_count));

                if (tmp == NULL) {
                    free(commands);
                    exit(EXIT_FAILURE);
                } else {
                    commands = tmp;
                }

                curr_word = 0;
                continue;
            }

            if (!inside_quotation && (isspace(lines[c]) || (c == (strlen(lines) - 2)))) {
                if (len == 0) continue;

                if (escaping_outside_quotes) continue;

                // printf("Current command %s\n", buf);

                if (c == (strlen(lines) - 2)) {
                    if (redirect->has_redirect) {
                        redirect->to_file = strdup(buf);
                    } else {
                        write_command(commands, buf, cmd_count, &curr_word);
                        commands[cmd_count - 1].args[curr_word] = NULL;
                    }
                } else {
                    write_command(commands, buf, cmd_count, &curr_word);
                }
            }
        }
        free(line);
        free(lines);
        free(buf);

        if (cmd_count <= 1 && curr_word == 0) {
            if (redirect->has_redirect) {
                free(redirect->to_file);
            }
            free(redirect);
            free(commands);
            continue;
        }

        if (commands == NULL || (commands[0].name == NULL)) {
            continue;
        }

        // test_commands(commands, cmd_count, redirect);

        if ((cmd_count == 1) && strcmp(commands[0].name, "exit") == 0) {
            if (curr_word == 2) {
                EXIT_CODE = atoi(commands[0].args[1]);
            }

            if (redirect->has_redirect) {
                free(redirect->to_file);
            }

            free(redirect);
            free_commands(commands, cmd_count);
            break;
        }

        EXIT_CODE = exec_commands(commands, cmd_count, redirect);
        free_commands(commands, cmd_count);
        if (redirect->has_redirect) {
            free(redirect->to_file);
        }
        free(redirect);
    }

    // printf("Exiting with exit code: %d\n", EXIT_CODE);
    return EXIT_CODE;
}
