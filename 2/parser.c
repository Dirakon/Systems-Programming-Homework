
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
    return strings_equal(string, "&") || strings_equal(string, "&&")
           || strings_equal(string, "|") || strings_equal(string, "||")
           || strings_equal(string, ">") || strings_equal(string, ">>");
}

int count_tokens_until_operator_or_end(token_list *tokens) {
    if (tokens == NULL || is_operator(tokens->token))
        return 0;
    return 1 + count_tokens_until_operator_or_end(tokens->other_tokens);
}

command_list *convert_tokens_without_disposal(token_list *all_tokens) {
    if (all_tokens == NULL)
        return NULL;

    char *command = strdup(all_tokens->token);
    bool has_arguments = !is_operator(command);
    int argument_count = has_arguments ? count_tokens_until_operator_or_end(all_tokens->other_tokens) : 0;
    char **argv = malloc(sizeof(char *) * argument_count);

    token_list *ptr = all_tokens->other_tokens;
    for (int argument_ptr = 0; argument_ptr < argument_count; ++argument_ptr, ptr = ptr->other_tokens) {
        argv[argument_ptr] = strdup(ptr->token);
    }

    command_list *command_and_following = malloc(sizeof(command_list));
    *command_and_following = (command_list) {.other_commands = convert_tokens_without_disposal(
            ptr), .this_command = {.argc = argument_count, .argv = argv, .name = command}};

    return command_and_following;
}


int count_commands(command_list *commands) {
    if (commands == NULL)
        return 0;
    return 1 + count_commands(commands->other_commands);
}


void dispose_of_command_list_structure(struct command_list *commands) {
    if (commands == NULL)
        return;
    dispose_of_command_list_structure(commands->other_commands);
    free(commands);
}


command_array parse(char *string) {
    token_list *tokens = get_tokens(string);
    command_list *commands = convert_tokens_without_disposal(tokens);
    dispose_of_tokens(tokens);
    int command_count = count_commands(commands);
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