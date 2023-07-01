#define UTILS_INCLUDED

#include <malloc.h>
#include <stdbool.h>
#include <string.h>

const int NONE = -1;

typedef struct reader_output {
    bool ended_with_EOF;
    char *line;
} reader_output;

reader_output read_stdin_line() {
    bool next_char_escaping = false;
    bool ended_with_EOF = false;
    const char NONE = 'a';
    char used_quote_symbol = NONE;

    int str_size = 1;
    int str_ptr = 0;
    char *str = malloc(sizeof(char) * (str_size + 1));
    str[str_ptr] = '\0';

    char ch;
    while ((ch = (char) getchar()) != '\n' || next_char_escaping || (used_quote_symbol != NONE)) {
        if (ch == EOF) {
            ended_with_EOF = true;
            break;
        }
        if (str_ptr == str_size) {
            str_size *= 10;
            char *new_str = malloc(sizeof(char) * (1 + str_size));
            strcpy(new_str, str);
            free(str);
            str = new_str;
        }

        switch (ch){
            case '\\':
                next_char_escaping = !next_char_escaping;

                break;
            case '\n':
                if (next_char_escaping){
                    str[--str_ptr] = '\0';
                    next_char_escaping = false;
                    continue;
                }
                break;
            case '\'':
            case '"':
                if (!next_char_escaping){
                    if (used_quote_symbol == ch){
                        used_quote_symbol = NONE;
                    }else if (used_quote_symbol == NONE){
                        used_quote_symbol = ch;
                    }
                }

                next_char_escaping = false;
                break;
            default:
                next_char_escaping = false;
                break;
        }


        str[str_ptr] = ch;
        str_ptr++;
        str[str_ptr] = '\0';

    }

    return (reader_output) {.line=str, .ended_with_EOF = ended_with_EOF};
}

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

bool strings_equal(char *str1, char *str2) {
    return strcmp(str1, str2) == 0;
}