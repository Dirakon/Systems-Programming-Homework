
#include <malloc.h>
#include <string.h>
#include <stdbool.h>

#ifndef UTILS_INCLUDED

#include "utils.c"

#endif


typedef struct token_list {
    char *token;
    struct token_list *other_tokens;
} token_list;

token_list *get_tokens(const char *string);

int
get_closing_quote_index(const char *string, int quotation_inside_start_inclusive, char used_quote_symbol) {
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
        if (!next_char_is_escaped && string[quotation_inside_end_exclusive] == used_quote_symbol)
            break;
        next_char_is_escaped = false;
    }
    return quotation_inside_end_exclusive;
}

char *
extract_quoted_token(const char *string, int quotation_inside_start_inclusive, int quotation_inside_end_exclusive, char quote_symbol) {
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
                        case '\'':
                            if (this_char != quote_symbol){
                                quoted_token[quoted_token_ptr++] = '\\';
                            }
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

token_list *extract_tokens_starting_from_quoted_literal(const char *string, int opening_quote_index, char quote_symbol) {
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
    int closing_quote_index = get_closing_quote_index(string, first_char_index, quote_symbol);
    //printf("quote detected: %d-%d(starting with '%c')\n",first_char_index,closing_quote_index, string[first_char_index]);
    char *quoted_token = extract_quoted_token(string, first_char_index, closing_quote_index, quote_symbol);

    // Supposedly, real Bash auto-closes unclosed quotations, so we consider null-terminator quote-closer too
    token_list *others_tokens = string[closing_quote_index] == '\0' ? NULL : get_tokens(
            string + closing_quote_index + 1);

    token_list *tokens_starting_from_classified = malloc(sizeof(token_list));
    *tokens_starting_from_classified = (token_list) {.token = quoted_token, .other_tokens = others_tokens};
    return tokens_starting_from_classified;
}


token_list *
prepend_unclassified_token_if_needed(char *string, int non_classified_token_start, token_list *following_tokens,
                                     int following_tokens_start, bool free_string) {
    if (non_classified_token_start == NONE) {
        if (free_string)
            free(string);
        return following_tokens;
    } else {
        token_list *non_classified_token = malloc(sizeof(token_list));
        *non_classified_token = (token_list) {.token = substring(string + non_classified_token_start,
                                                                 following_tokens_start - non_classified_token_start),
                .other_tokens = following_tokens};
        if (free_string)
            free(string);
        return non_classified_token;
    }
}

token_list *get_tokens(const char *string) {
    char *new_string = malloc(sizeof(char)* (strlen(string)+1));
    memset(new_string, '\0', strlen(string)+1);
    //TODO: handle non-classified token with escaped whitespaces
    int old_string_ptr = 0;
    int new_string_ptr = 0;
    int non_classified_token_start = NONE;
    bool char_is_escaped = false;
    while (string[old_string_ptr] != '\0') {
        if (!char_is_escaped)
            switch (string[old_string_ptr]) {
                case '\t':
                case ' ':
                case '\n':
                    if (non_classified_token_start != NONE) {
                        token_list *answer = malloc(sizeof(token_list));
                        *answer = (token_list) {.token = substring(new_string + non_classified_token_start,
                                                                   new_string_ptr - non_classified_token_start),
                                .other_tokens = get_tokens(string + old_string_ptr + 1)};
                        free(new_string);
                        return answer;
                    }
                    break;
                case '\'':
                case '"': {
                    token_list *quoted_token_and_following = extract_tokens_starting_from_quoted_literal(string, old_string_ptr, string[old_string_ptr]);
                    free(new_string);
                    return prepend_unclassified_token_if_needed(string, non_classified_token_start,
                                                                quoted_token_and_following, old_string_ptr, false);
                }
                case '>':
                case '&':
                case '|': {
                    int classified_token_size = string[old_string_ptr + 1] == string[old_string_ptr] ? 2 : 1;
                    token_list *classified_token_and_following = malloc(sizeof(token_list));
                    *classified_token_and_following = (token_list) {.token = substring(string + old_string_ptr,
                                                                                       classified_token_size),
                            .other_tokens = get_tokens(
                                    string + old_string_ptr + classified_token_size)};
                    free(new_string);
                    return prepend_unclassified_token_if_needed(string, non_classified_token_start,
                                                                classified_token_and_following, old_string_ptr, false);

                }
                default:
                    if (non_classified_token_start == NONE) {
                        non_classified_token_start = old_string_ptr;
                    }
                    break;
            }
        if (string[old_string_ptr] == '\\'){
            if (char_is_escaped){
                // Save two '\\' as one (one escapes another)
                new_string[new_string_ptr++] = string[old_string_ptr];
                char_is_escaped = false;
            }else{
                char_is_escaped = true;
            }
        }else{
            char_is_escaped = false;
            new_string[new_string_ptr++] = string[old_string_ptr];
        }
        old_string_ptr++;
    }

    return prepend_unclassified_token_if_needed(new_string, non_classified_token_start, NULL, new_string_ptr, true);
}

void dispose_of_tokens(token_list *tokens) {
    if (tokens == NULL)
        return;
    free(tokens->token);
    dispose_of_tokens(tokens->other_tokens);
    free(tokens);
}