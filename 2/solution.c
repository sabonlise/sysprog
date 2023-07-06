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
    int old_fd[2];
    int new_fd[2];

    int status = 0;

    int last_exit_code = -1;

    if (redirect->has_redirect && !redirect->to_file) {
        printf("sh: syntax error near unexpected token `newline'");
        return EXIT_FAILURE;
    }

    if (pipe(old_fd) != 0) {
        return errno;
    }

    for (int i = 0; i < size; i++) {
        if (strcmp(commands[i].name, "cd") == 0) {
            if (commands[i].arg_count == 1) continue;
            if (chdir(commands[i].args[1]) == -1) {
                printf("cd: %s: No such file or directory\n", commands[i].args[1]);
            }
            continue;
        }

        if (strcmp(commands[i].name, "exit") == 0) {
            if (commands[i].arg_count == 2) {
                status = atoi(commands[i].args[1]);
            } else if (commands[i].arg_count == 1) {
                status = 0;
            } else {
                printf("exit: too many arguments\n");
                status = EXIT_FAILURE;
            }

            if (i == (size - 1)) {
                last_exit_code = status;
                break;
            }
        }

        if (i != (size - 1)) {
            if (pipe(new_fd) != 0) {
                return errno;
            }
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
                    return errno;
                }

                dup2(redirect_ds, STDOUT_FILENO);
                close(redirect_ds);
            }

            if (i > 0) {
                dup2(old_fd[READ], STDIN_FILENO);
                close(old_fd[READ]);
                close(old_fd[WRITE]);
            }

            if (i != (size - 1)) {
                close(new_fd[READ]);
                dup2(new_fd[WRITE], STDOUT_FILENO);
                close(new_fd[WRITE]);
            }

            if (execvp(commands[i].name, commands[i].args) == -1) {
                _exit(errno);
            }
            _exit(EXIT_FAILURE);
        } else {
            if (i > 0) {
                close(old_fd[READ]);
                close(old_fd[WRITE]);
            }

            if (i != (size - 1)) {
                for (int j = 0; j < 2; j++) {
                    old_fd[j] = new_fd[j];
                }
            }
        }
    }

    close(old_fd[READ]);
    close(old_fd[WRITE]);

    int pid = 0;

    for (int i = 0; i < size; i++) {
        if (strcmp(commands[i].name, "cd") == 0) continue;
        pid = waitpid(cmds[i], &status, 0);
        if (pid != cmds[i]) {
            break;
        }
    }

    if (last_exit_code != -1) {
        status = last_exit_code;
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
}

//void test_commands(cmd* commands, int size, struct redirect_info *redirect) {
//    for (int i = 0; i < size; i++) {
//        fprintf(stderr, "Command #%d: %s\n", i + 1, commands[i].name);
//        for (int j = 0; j < commands[i].arg_count; j++) {
//            fprintf(stderr, "Argument #%d: %s\n", j + 1, commands[i].args[j]);
//        }
//    }
//
//    if (redirect->has_redirect) {
//        fprintf(stderr, "Redirecting to file: %s. Is appending: %d\n", redirect->to_file, redirect->is_appending);
//    }
//}

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
        cmd *commands = (cmd*) calloc(1, sizeof(cmd));
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
        bool inside_quotation = false;
        char current_quote = '~';

        while (true) {
            if (getline(&line, &size, stdin) == -1) {
                // EOF
                free(redirect);
                free(commands);
                free(buf);
                free(line);
                free(lines);
                exit(EXIT_CODE);
            }

            size_t line_len = line ? strlen(line) : 0;

            for (size_t offset = 0; offset < line_len; offset++) {
                if (line[offset] == '"' || line[offset] == '\'') {
                    if (inside_quotation) {
                        if (current_quote == line[offset]) {
                            inside_quotation = false;
                        }
                    } else {
                        current_quote = line[offset];
                        inside_quotation = true;
                    }
                }
            }

            if (line_len >= 2 && ((line[line_len - 2] == '\\' && !inside_quotation) || inside_quotation) && line[line_len - 1] == '\n') {
                if (!inside_quotation) {
                    line[line_len - 2] = '\0';
                }
            } else if (line_len == 1 && line[line_len - 1] == '\n') {
                eol = true;
            } else if (line_len > 1 && line[line_len - 1] == '\n') {
                line[line_len - 1] = '\0';
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

            strncat(lines, line, line_len);
            if (eol) {
                size_t new_len = lines ? strlen(lines) : 0;

                char *temp;
                temp = realloc(lines, new_len + 3);

                if (temp == NULL) {
                    free(lines);
                    exit(EXIT_FAILURE);
                } else {
                    lines = temp;
                }
                // Adding 2 extra spaces for correct parsing (they won't be included in commands anyway)
                char add_space = ' ';
                strncat(lines, &add_space, 1);
                // strncat(lines, &add_space, 1);
                break;
            }
        }

        if  (lines[0] == '#' ||
            (strlen(lines) == 1
            && ((lines[strlen(lines) - 1] == '\n')
            || (lines[strlen(lines) - 1] == '#')))) {

            free(redirect);
            free(line);
            free(lines);
            free(buf);
            free(commands);
            continue;
        }

        current_quote = '~';
        inside_quotation = false;
        bool escaping_next_char = false;

        for (size_t c = 0; c < (strlen(lines) - 1); c++) {
            size_t len = buf ? strlen(buf) : 0;

            if (!inside_quotation && lines[c] == '#') {
                break;
            }

            if (lines[c] == '\\') {
                if (escaping_next_char) {
                    escaping_next_char = false;
                } else {
                    escaping_next_char = true;
                }
            }

            if (escaping_next_char && lines[c] != '\\' && !isspace(lines[c])) {
                escaping_next_char = false;
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

            if ((!isspace(lines[c]) && lines[c] != '|' && lines[c] != '>' && !inside_quotation) || inside_quotation || escaping_next_char) {
                if (inside_quotation && escaping_next_char && lines[c] == '\\' && lines[c + 1] == '\\') continue;
                if (!inside_quotation && lines[c] == '\\' && escaping_next_char) continue;

                if (!inside_quotation && (lines[c] == '"' || lines[c] == '\'') && !escaping_next_char) {
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

                if (escaping_next_char && lines[c] != '\\') continue;

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
        } else {
            EXIT_CODE = exec_commands(commands, cmd_count, redirect);
        }

        free_commands(commands, cmd_count);
        if (redirect->has_redirect) {
            free(redirect->to_file);
        }
        free(redirect);
    }

    return EXIT_CODE;
}
