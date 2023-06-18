
#include "parser.c"

#ifndef INC_UTILS
#define INC_UTILS
#include "utils.c"
#endif


int main() {
    while (true) {
        printf(">> ");
        char *line = read_stdin_line();
        command_array commands = parse(line);
        free(line);


        if (commands.command_count == 1 && strings_equal(commands.commands[0].name, "exit")) {
            int exit_code = 0;
            if (commands.commands[0].argc != 0) {
                sscanf(commands.commands[0].argv[0], "%d", &exit_code);
            }
            dispose_of_commands(commands);
            return exit_code;
        } else {
            output_commands(commands);
            dispose_of_commands(commands);
        }

    }
}