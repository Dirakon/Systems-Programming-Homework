
#include <malloc.h>
#include <string.h>
#include <stdbool.h>
#include "tokenizer.c"

#ifndef UTILS_INCLUDED
#include "utils.c"
#endif


typedef struct command {
    char *name;
    char **argv;
    int argc;
} command;

typedef struct command_array {
    command *commands;
    int command_count;
} command_array;


typedef struct command_list {
    command this_command;
    struct command_list *other_commands;
} command_list;


bool is_operator(char *string) {
    // Cheap but working version:
    return string[0] == '&' || string[0] == '|' || string[0] == '>';
    // Actual implementation:
//    return strings_equal(string, "&") || strings_equal(string, "&&")
//           || strings_equal(string, "|") || strings_equal(string, "||")
//           || strings_equal(string, ">") || strings_equal(string, ">>");
}

// start_count parameter needed for tail recursion optimization
int count_tokens_until_operator_or_end(token_list *tokens, int start_count) {
    if (tokens == NULL || is_operator(tokens->token))
        return start_count;
    return count_tokens_until_operator_or_end(tokens->other_tokens, start_count + 1);
}

command_list *convert_tokens_without_disposal(token_list *all_tokens) {
    if (all_tokens == NULL)
        return NULL;

    command_list *current_command = NULL;
    command_list *previous_command = NULL;

    token_list *token_ptr = all_tokens;
    while (token_ptr != NULL) {
        char *command = strdup(token_ptr->token);
        bool has_arguments = !is_operator(command);
        int argument_count = has_arguments ? count_tokens_until_operator_or_end(token_ptr->other_tokens, 0) : 0;
        char **argv = malloc(sizeof(char *) * argument_count);

        token_list *next_tokens = token_ptr->other_tokens;
        for (int argument_ptr = 0;
             argument_ptr < argument_count; ++argument_ptr, next_tokens = next_tokens->other_tokens) {
            argv[argument_ptr] = strdup(next_tokens->token);
        }

        command_list *command_and_following = malloc(sizeof(command_list));
        *command_and_following = (command_list) {
                .other_commands = NULL,
                .this_command = {
                        .argc = argument_count,
                        .argv = argv,
                        .name = command
                }
        };

        if (current_command != NULL) {
            current_command->other_commands = command_and_following;
        } else {
            previous_command = command_and_following;
        }

        current_command = command_and_following;
        token_ptr = next_tokens;
    }

    return previous_command;
}


// start_count parameter needed for tail recursion optimization
int count_commands(command_list *commands, int start_count) {
    if (commands == NULL)
        return start_count;
    return count_commands(commands->other_commands, start_count + 1);
}


void dispose_of_command_list_structure(struct command_list *commands) {
    while (commands != NULL) {
        struct command_list *next_command = commands->other_commands;
        free(commands);
        commands = next_command;
    }
}


command_array parse(char *string) {
    token_list *tokens = get_tokens(string);
    command_list *commands = convert_tokens_without_disposal(tokens);
    dispose_of_tokens(tokens);
    int command_count = count_commands(commands, 0);
    command *commands_as_array = malloc(sizeof(command) * command_count);
    command_list *ptr = commands;
    for (int i = 0; i < command_count; ++i, ptr = ptr->other_commands) {
        commands_as_array[i] = ptr->this_command;
    }
    dispose_of_command_list_structure(commands);
    return (command_array) {.commands = commands_as_array, .command_count=command_count};
}

void dispose_of_commands(command_array commands) {
    for (int i = 0; i < commands.command_count; ++i) {
        free(commands.commands[i].name);
        for (int j = 0; j < commands.commands[i].argc; ++j)
            free(commands.commands[i].argv[j]);
        free(commands.commands[i].argv);
    }
    free(commands.commands);
}


void output_commands(command_array commands) {
    for (int i = 0; i < commands.command_count; ++i) {
        command cmd = commands.commands[i];
        printf("Command: '%s' (args: %d)\n", cmd.name, cmd.argc);
        for (int arg_ptr = 0; arg_ptr < cmd.argc; ++arg_ptr) {
            printf("\t- %s\n", cmd.argv[arg_ptr]);
        }
    }
}