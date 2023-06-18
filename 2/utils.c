
#include <malloc.h>
#include <stdbool.h>
#include <string.h>

const int NONE = -1;


char *read_stdin_line() {
    int str_size = 1;
    int str_ptr = 0;
    char *str = malloc(sizeof(char) * (str_size + 1));
    str[0] = '\0';
    // TODO: allow escaped newlines for multi-line input
    // bool next_char_escaping = false;
    char ch;
    while ((ch = (char) getchar()) != EOF && (ch != '\n')) {
        if (str_ptr == str_size) {
            str_size *= 2;
            char *new_str = malloc(sizeof(char) * (1 + str_size));
            strcpy(new_str, str);
            free(str);
            str = new_str;
        }

        str[str_ptr] = ch;
        str_ptr++;
        str[str_ptr] = '\0';

    }

    return str;
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