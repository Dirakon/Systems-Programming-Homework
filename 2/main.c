#include <unistd.h>
#include "parser.c"


#ifndef UTILS_INCLUDED
#include "utils.c"
#endif


void execute_command(command *this_command) {
    char **arguments_with_filename = malloc(sizeof(char *) * (2 + (*this_command).argc));
    arguments_with_filename[0] = (*this_command).name;
    for (int i = 0; i < (*this_command).argc; ++i) {
        arguments_with_filename[i + 1] = (*this_command).argv[i];
    }
    arguments_with_filename[(*this_command).argc + 1] = NULL;
    execvp((*this_command).name, arguments_with_filename);
}


int get_command_count_before_redirection(command_array *commands) {
    int command_count_without_redirection;
    if ((*commands).command_count <= 1) {
        command_count_without_redirection = (*commands).command_count;
    } else {
        command potential_redirector = (*commands).commands[(*commands).command_count - 2];
        if (strings_equal(potential_redirector.name, ">") || strings_equal(potential_redirector.name, ">")) {
            command_count_without_redirection = (*commands).command_count - 2;
        } else {
            command_count_without_redirection = (*commands).command_count;
        }
    }
    return command_count_without_redirection;
}


int main() {
    bool last_line_ended_with_EOF = false;
    while (!last_line_ended_with_EOF) {
        printf(">> ");
        reader_output read_data = read_stdin_line();
        last_line_ended_with_EOF = read_data.ended_with_EOF;
        command_array commands = parse(read_data.line);
        free(read_data.line);


        if (commands.command_count == 1 && strings_equal(commands.commands[0].name, "exit")) {
            int exit_code = 0;
            if (commands.commands[0].argc != 0) {
                sscanf(commands.commands[0].argv[0], "%d", &exit_code);
            }
            dispose_of_commands(commands);
            return exit_code;
        } else if (commands.command_count == 1 && strings_equal(commands.commands[0].name, "cd")) {
            if (commands.commands[0].argc == 0) {
                chdir("~");
            } else {
                chdir(commands.commands[0].argv[0]);
            }
        } else {
            int command_array_ptr = 0;
            int last_pipe_end = NONE;

            // NOTE: currently only support one redirect and exactly at the end of command chain
            int command_count_without_redirection = get_command_count_before_redirection(&commands);

            while (true) {
                if (strings_equal(commands.commands[command_array_ptr].name, "|")) {
                    command_array_ptr++;
                    continue;
                }
                int fd[2];
                pipe(fd);
                const int FORK_CHILD = 0;
                if (fork() == FORK_CHILD) {
                    if (last_pipe_end != NONE) {
                        // Take the last cmd's output as STDIN
                        dup2(last_pipe_end, 0);
                        close(last_pipe_end);
                    }
                    // Output STDOUT to the next process' STDIN
                    dup2(fd[1], 1);
                    close(fd[0]);  // Close the unused read end of the pipe
                    command this_command = commands.commands[command_array_ptr];
                    execute_command(&this_command);
                    return 0; // Just to be sure
                } else // PARENT
                {
                    close(fd[1]);  // Close the unused write end of the pipe
                    if (command_array_ptr == commands.command_count - 1) {
                        int c;
                        ssize_t nbytes;
                        while ((nbytes = read(fd[0], &c, 1)) > 0) {
                            write(STDOUT_FILENO, &c, 1);
                        }
                        break;
                    } else if (command_array_ptr == command_count_without_redirection - 1) {
                        // The child was the last subcommand, so we just output to the file as the redirection says
                        bool overwrite_file = strings_equal(commands.commands[commands.command_count - 2].name, ">>");
                        char *file_to_write = commands.commands[commands.command_count - 1].name;
                        FILE *file = fopen(file_to_write, overwrite_file ? "w" : "a");
                        int file_descriptor = fileno(file);

                        int c;
                        ssize_t nbytes;
                        while ((nbytes = read(fd[0], &c, 1)) > 0) {
                            write(file_descriptor, &c, 1);
                        }
                        fclose(file);
                        break;
                    }

                }
                last_pipe_end = fd[0];
                command_array_ptr++;
            }
            // output_commands(commands);
        }
        dispose_of_commands(commands);

    }
    return 0;
}