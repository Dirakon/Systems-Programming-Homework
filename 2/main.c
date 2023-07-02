#include <unistd.h>
#include <wait.h>
#include <errno.h>
#include <stdlib.h>
#include "parser.c"


#ifndef UTILS_INCLUDED
#include "utils.c"
#endif


typedef struct pid_list {
    int pid;
    struct pid_list *other_pids;
} pid_list;

void dispose_of_pid_list(pid_list *list) {
    pid_list *ptr = list;
    while (ptr != NULL) {
        pid_list *next = ptr->other_pids;
        free(ptr);
        ptr = next;
    }
}

pid_list *add_pid(pid_list *list, int new_pid) {
    pid_list *answer = malloc(sizeof(pid_list));
    *answer = (pid_list) {.other_pids = list, .pid = new_pid};
    return answer;
}


int execute_command(command_array *commands, command *this_command) {
    char **arguments_with_filename = malloc(sizeof(char *) * (2 + (*this_command).argc));
    arguments_with_filename[0] = strdup((*this_command).name);
    for (int i = 0; i < (*this_command).argc; ++i) {
        arguments_with_filename[i + 1] = strdup((*this_command).argv[i]);
    }
    arguments_with_filename[(*this_command).argc + 1] = NULL;
    dispose_of_commands(*commands);
    execvp(arguments_with_filename[0], arguments_with_filename);

    char** ptr = arguments_with_filename;
    while ((*ptr) != NULL){
        free(*ptr);
        ptr++;
    }
    free(arguments_with_filename);

    exit(errno);
}

int execute_chdir_command(command command) {
    int last_child_exit_code;
    if (command.argc == 0) {
        last_child_exit_code = chdir("~");
    } else {
        last_child_exit_code = chdir(command.argv[0]);
    }
    return last_child_exit_code;
}


int get_command_count_before_redirection(command_array *commands) {
    int command_count_without_redirection;
    if ((*commands).command_count <= 1) {
        command_count_without_redirection = (*commands).command_count;
    } else {
        command potential_redirector = (*commands).commands[(*commands).command_count - 2];
        if (strings_equal(potential_redirector.name, ">>") || strings_equal(potential_redirector.name, ">")) {
            command_count_without_redirection = (*commands).command_count - 2;
        } else {
            command_count_without_redirection = (*commands).command_count;
        }
    }
    return command_count_without_redirection;
}


void execute_exit_command(command_array *commands, command first_command) {
    int exit_code = 0;
    if (first_command.argc != 0) {
        sscanf(first_command.argv[0], "%d", &exit_code);
    }
    dispose_of_commands((*commands));
    exit(exit_code);
}

int main() {
    bool last_line_ended_with_EOF = false;
    int last_child_exit_code = 0;
    while (!last_line_ended_with_EOF) {
        reader_output read_data = read_stdin_line();
        last_line_ended_with_EOF = read_data.ended_with_EOF;
        command_array commands = parse(read_data.line);
        free(read_data.line);

        int actual_command_count = commands.command_count;
        if (actual_command_count == 0) {
            dispose_of_commands(commands);
            continue;
        }

        int last_child_pid = NONE;
        pid_list *children_to_wait_for = NULL;

        command first_command = commands.commands[0];
        if (actual_command_count == 1 && strings_equal(first_command.name, "exit")) {
            execute_exit_command(&commands, first_command);
        } else if (actual_command_count == 1 && strings_equal(first_command.name, "cd")) {
            last_child_exit_code = execute_chdir_command(first_command);
        } else {
            if (strings_equal("&", commands.commands[actual_command_count - 1].name)) {
                // Subshell will be executing this command set
                actual_command_count -= 1;
                const int FORK_CHILD = 0;
                if ((last_child_pid = fork()) == FORK_CHILD) {
                    // child will finish this command set
                    fclose(stdin);

                } else {
                    // we will proceed
                    dispose_of_commands(commands);
                    continue;
                }
            }


            int command_array_ptr = 0;
            int last_pipe_end = NONE;

            // NOTE: currently only support one redirect and exactly at the end of command chain
            int command_count_without_redirection = get_command_count_before_redirection(&commands);
            char *last_operator = NULL;
            while (true) {
                if (command_array_ptr == actual_command_count)
                    break;
                if (is_operator(commands.commands[command_array_ptr].name)) {
                    last_operator = commands.commands[command_array_ptr].name;
                    command_array_ptr++;
                    continue;
                }

                if (last_operator != NULL && !strings_equal(last_operator, "|")) {
                    // OR or AND
                    if (last_pipe_end != NONE) {
                        int status;
                        int single_byte;
                        while (read(last_pipe_end, &single_byte, 1) > 0) {
                            write(STDOUT_FILENO, &single_byte, 1);
                        }
                        close(last_pipe_end);  // Close the read end of the pipe
                        last_pipe_end = NONE;

                        waitpid(last_child_pid, &status, 0);
                        if (WIFEXITED(status)) {
                            int es = WEXITSTATUS(status);
                            last_child_exit_code = es;
                        } else {
                            last_child_exit_code = 0;
                        }
                    }
                    if (strings_equal(last_operator, "||")) {
                        if (!last_child_exit_code) {
                            command_array_ptr++;
                            continue;
                        }
                    } else {//&&
                        if (last_child_exit_code) {
                            command_array_ptr++;
                            continue;
                        }
                    }
                    last_operator = NULL;

                }

                if (command_array_ptr != 0 && last_pipe_end == NONE && last_operator != NULL &&
                    strings_equal("|", last_operator)) {
                    command_array_ptr++;
                    last_operator = NULL;
                    continue;
                }

                int fd[2];
                pipe(fd);
                const int FORK_CHILD = 0;

                if ((last_child_pid = fork()) == FORK_CHILD) {
                    if (last_pipe_end != NONE) {
                        // Take the last cmd's output as STDIN
                        dup2(last_pipe_end, 0);
                        close(last_pipe_end);
                        last_pipe_end = NONE;
                    }
                    // Output STDOUT to the next process' STDIN
                    dup2(fd[1], 1);
                    close(fd[1]);  // Close the duplicated write end of the pipe
                    close(fd[0]);  // Close the unused read end of the pipe
                    command this_command = commands.commands[command_array_ptr];
                    dispose_of_pid_list(children_to_wait_for);
                    children_to_wait_for = NULL;
                    if (strings_equal(this_command.name, "exit")) {
                        execute_exit_command(&commands, this_command);
                    }
                    return execute_command(&commands,&this_command); // Leads to execvp
                } else // PARENT
                {
                    children_to_wait_for = add_pid(children_to_wait_for, last_child_pid);

                    close(fd[1]);  // Close the unused write end of the pipe
                    if (command_array_ptr == actual_command_count - 1) {
                        // Last command, so we manually output to STDOUT
                        int single_byte;
                        while (read(fd[0], &single_byte, 1) > 0) {
                            write(STDOUT_FILENO, &single_byte, 1);
                        }
                        close(fd[0]);  // Close the read end of the pipe
                        break;
                    } else if (command_array_ptr == command_count_without_redirection - 1) {
                        // The child was the last subcommand, so we just output to the file as the redirection says
                        bool overwrite_file = strings_equal(commands.commands[actual_command_count - 2].name, ">");
                        char *file_to_write = commands.commands[actual_command_count - 1].name;
                        FILE *file = fopen(file_to_write, overwrite_file ? "w" : "a");
                        int file_descriptor = fileno(file);

                        int single_byte;
                        while (read(fd[0], &single_byte, 1) > 0) {
                            write(file_descriptor, &single_byte, 1);
                        }
                        fclose(file);
                        close(fd[0]);  // Close the read end of the pipe
                        break;
                    }

                }
                if (last_pipe_end != NONE) {
                    close(last_pipe_end);
                }
                last_operator = NULL;
                last_pipe_end = fd[0];
                command_array_ptr++;
            }
            if (last_pipe_end != NONE) {
                close(last_pipe_end);
            }
        }

        // Wait for children to terminate
        int status = 0;
        pid_list *ptr = children_to_wait_for;
        while (ptr != NULL) {
            int pid_to_wait_this_turn = ptr->pid;

            waitpid(pid_to_wait_this_turn, &status, 0);
            if (pid_to_wait_this_turn == last_child_pid) {
                if (WIFEXITED(status)) {
                    int es = WEXITSTATUS(status);
                    last_child_exit_code = es;
                } else {
                    last_child_exit_code = 0;
                }
            }
            ptr = ptr->other_pids;
        }
        dispose_of_pid_list(children_to_wait_for);

        dispose_of_commands(commands);

    }
    return last_child_exit_code;
}
