
#include <malloc.h>
#include <string.h>
#include <stdbool.h>

const int NONE = -1;

char *substring(const char *string, int char_count) {
    char *answer = malloc(sizeof(char) * (char_count + 1));
    for (int i = 0; i < char_count; ++i) {
        answer[i] = string[i];
        if (string[i] == '\0') {
            return answer;
        }
    }
    answer[char_count] = '\0';
    return answer;
}


typedef struct command {
    const char *name;
    const char **argv;
    int argc;
} command;

typedef struct command_array {
    command *commands;
    int command_count;
} command_array;


typedef struct token_list {
    char *token;
    struct token_list *other_tokens;
} token_list;

token_list *get_tokens(const char *string);

int
get_closing_quote_index(const char *string, int quotation_inside_start_inclusive) {
    bool next_char_is_escaped = false;
    int quotation_inside_end_exclusive = quotation_inside_start_inclusive;
    for (;; ++quotation_inside_end_exclusive) {
        // Supposedly, real Bash auto-closes unclosed quotations, so we consider null-terminator quote-closer too
        if (string[quotation_inside_end_exclusive] == '\0')
            break;
        if (string[quotation_inside_end_exclusive] == '\\') {
            next_char_is_escaped = !next_char_is_escaped;
            continue;
        }
        if (!next_char_is_escaped && string[quotation_inside_end_exclusive] == '"')
            break;
        next_char_is_escaped = false;
    }
    return quotation_inside_end_exclusive;
}

char *
extract_quoted_token(const char *string, int quotation_inside_start_inclusive, int quotation_inside_end_exclusive) {
    int quotation_insides_max_size = quotation_inside_end_exclusive - quotation_inside_start_inclusive;
    char *quoted_token = malloc(sizeof(char) * (quotation_insides_max_size + 1));
    int quoted_token_ptr = 0;

    bool next_char_is_escaped = false;
    for (int j = quotation_inside_start_inclusive; j < quotation_inside_end_exclusive; ++j) {
        switch (string[j]) {
            case '\\':
                if (next_char_is_escaped) {
                    quoted_token[quoted_token_ptr++] = '\\';
                    next_char_is_escaped = false;
                } else {
                    next_char_is_escaped = true;
                }
                break;
            default: {
                char this_char = string[j];
                if (next_char_is_escaped) {
                    switch (this_char) {
                        case 'n':
                            this_char = '\n';
                            break;
                        case 't':
                            this_char = '\t';
                            break;
                        case '"':
                            break;
                        default:
                            quoted_token[quoted_token_ptr++] = '\\';
                    }
                    next_char_is_escaped = false;
                }
                quoted_token[quoted_token_ptr++] = this_char;


            }
        }
    }
    quoted_token[quoted_token_ptr++] = '\0';
    return quoted_token;
}

token_list *extract_tokens_starting_from_quoted_literal(const char *string, int opening_quote_index) {
    /*
     NOTE: for now, one situation is not handled properly:
     1: quoted literals not separated by space
       Real bash:
        >> echo a"b"c"d"
        >>> abcd
        (does this not seem wrong to you? there should just be an error at this point)
       Terminal powered by my parser:
        >> echo a"b"c"d"
        >>> a b c d
   */
    int first_char_index = opening_quote_index + 1;
    int closing_quote_index = get_closing_quote_index(string, first_char_index);
    //printf("quote detected: %d-%d(starting with '%c')\n",first_char_index,closing_quote_index, string[first_char_index]);
    char *quoted_token = extract_quoted_token(string, first_char_index, closing_quote_index);

    // Supposedly, real Bash auto-closes unclosed quotations, so we consider null-terminator quote-closer too
    token_list *others_tokens = string[closing_quote_index] == '\0' ? NULL : get_tokens(
            string + closing_quote_index + 1);

    token_list *tokens_starting_from_classified = malloc(sizeof(token_list));
    *tokens_starting_from_classified = (token_list) {.token = quoted_token, .other_tokens = others_tokens};
    return tokens_starting_from_classified;
}


token_list *
prepend_unclassified_token_if_needed(const char *string, int non_classified_token_start, token_list *following_tokens,
                                     int following_tokens_start) {
    if (non_classified_token_start == NONE) {
        return following_tokens;
    } else {
        token_list *non_classified_token = malloc(sizeof(token_list));
        *non_classified_token = (token_list) {.token = substring(string + non_classified_token_start,
                                                                 following_tokens_start - non_classified_token_start),
                .other_tokens = following_tokens};
        return non_classified_token;
    }
}

token_list *get_tokens(const char *string) {
    //TODO: handle non-classified token with escaped whitespaces
    int i = 0;
    int non_classified_token_start = NONE;
    while (string[i] != '\0') {
        switch (string[i]) {
            case '\t':
            case ' ':
            case '\n':
                if (non_classified_token_start != NONE) {
                    token_list *answer = malloc(sizeof(token_list));
                    *answer = (token_list) {.token = substring(string + non_classified_token_start,
                                                               i - non_classified_token_start),
                            .other_tokens = get_tokens(string + i + 1)};
                    return answer;
                }
                break;
            case '"': {
                token_list *quoted_token_and_following = extract_tokens_starting_from_quoted_literal(string, i);
                return prepend_unclassified_token_if_needed(string, non_classified_token_start,
                                                            quoted_token_and_following, i);
            }
            case '>':
            case '&':
            case '|': {
                int classified_token_size = string[i + 1] == string[i] ? 2 : 1;
                token_list *classified_token_and_following = malloc(sizeof(token_list));
                *classified_token_and_following = (token_list) {.token = substring(string + i,
                                                                                   classified_token_size),
                        .other_tokens = get_tokens(
                                string + i + classified_token_size)};

                return prepend_unclassified_token_if_needed(string, non_classified_token_start,
                                                            classified_token_and_following, i);

            }
            default:
                if (non_classified_token_start == NONE) {
                    non_classified_token_start = i;
                }
                break;
        }
        i++;
    }

    return prepend_unclassified_token_if_needed(string, non_classified_token_start, NULL, i);
}

void dispose_of_tokens(token_list *tokens) {
    if (tokens == NULL)
        return;
    free(tokens->token);
    dispose_of_tokens(tokens->other_tokens);
    free(tokens);
}


typedef struct command_list {
    command this_command;
    struct command_list *other_commands;
} command_list;

bool strings_equal(char *str1, char *str2) {
    return strcmp(str1, str2) == 0;
}

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
    const char **argv = malloc(sizeof(char *) * argument_count);

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


int main(int argc, char **argv) {
    if (argc == 1)
        return -1;
    command_array commands = parse(argv[1]);

    for (int i = 0; i < commands.command_count; ++i) {
        command cmd = commands.commands[i];
        printf("Command: '%s' (args: %d)\n", cmd.name, cmd.argc);
        for (int arg_ptr = 0; arg_ptr < cmd.argc; ++arg_ptr) {
            printf("\t- %s\n", cmd.argv[arg_ptr]);
        }
    }

    dispose_of_commands(commands);
    return 1;
}