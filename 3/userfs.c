#include "userfs.h"
#include <stddef.h>
#include <malloc.h>
#include <stdbool.h>
#include <string.h>

enum {
    BLOCK_SIZE = 512,
    MAX_FILE_SIZE = 1024 * 1024 * 100,
};

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

bool strings_equal(char *str1, char *str2) {
    return strcmp(str1, str2) == 0;
}

bool specific_flag_is_present(int flags, int specific_flag) {
    return (flags & specific_flag) != 0;
}

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
    /** Block memory. */
    char *memory;
    /** How many bytes are occupied. */
    int occupied;
    /** Next block in the file. */
    struct block *next;
    /** Previous block in the file. */
    struct block *prev;

    /* PUT HERE OTHER MEMBERS */
    int index;
};

struct file {
    /** Double-linked list of file blocks. */
    struct block *block_list;
    /**
     * Last block in the list above for fast access to the end
     * of file.
     */
    struct block *last_block;
    /** How many file descriptors are opened on the file. */
    int refs;
    /** File name. */
    char *name;
    /** Files are stored in a double-linked list. */
    struct file *next;
    struct file *prev;

    /* PUT HERE OTHER MEMBERS */

    bool marked_for_deletion;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
    struct file *file;

    /* PUT HERE OTHER MEMBERS */
    int index;

    struct block *current_block;
    int block_offset;

    int flags;
};

struct file *try_get_file_by_filename(char *filename) {
    struct file *ptr = file_list;
    while (ptr != NULL) {
        if (strings_equal(ptr->name, filename))
            return ptr;
        ptr = ptr->next;
    }
    return NULL;
}

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

int try_get_empty_file_descriptor_slot() {
    for (int i = 0; i < file_descriptor_count; ++i) {
        if (file_descriptors[i] == NULL)
            return i;
    }
    return NONE;
}

int add_file_descriptor_at_the_end(struct filedesc *descriptor) {
    const int GROWTH_FACTOR = 2;
    if (file_descriptors == NULL) {
        file_descriptor_capacity = 1;
        file_descriptor_count = 0;
        file_descriptors = malloc(sizeof(struct filedesc *));
    }
    if (file_descriptor_capacity == file_descriptor_count) {
        file_descriptor_capacity *= GROWTH_FACTOR;
        file_descriptors = realloc(file_descriptors, sizeof(struct filedesc *) * file_descriptor_capacity);
    }

    int file_descriptor_index = file_descriptor_count;
    file_descriptors[file_descriptor_index] = descriptor;
    descriptor->index = file_descriptor_index;

    file_descriptor_count++;

    return file_descriptor_index;
}

int add_file_descriptor(struct filedesc *descriptor) {
    int file_descriptor_index = try_get_empty_file_descriptor_slot();
    if (file_descriptor_index != NONE) {
        descriptor->index = file_descriptor_index;
        file_descriptors[file_descriptor_index] = descriptor;
        return file_descriptor_index;
    }
    return add_file_descriptor_at_the_end(descriptor);
}

struct filedesc *try_get_file_descriptor(int index) {
    if (index < 0 || index >= file_descriptor_count)
        return NULL;
    return file_descriptors[index];
}

void delete_file(struct file *file);

void close_file_descriptor(struct filedesc *descriptor) {
    struct file *file = descriptor->file;
    file->refs--;
    if (file->marked_for_deletion && file->refs == 0)
        delete_file(file);
    file_descriptors[descriptor->index] = NULL;
    free(descriptor);
}

struct file *create_file(const char *filename) {
    struct block *first_block = malloc(sizeof(struct block));
    *first_block = (struct block) {
            .next = NULL,
            .prev = NULL,
            .index = 0,
            .memory = malloc(sizeof(char) * BLOCK_SIZE),
            .occupied = 0
    };

    struct file *new_file_list = malloc(sizeof(struct file));
    *new_file_list = (struct file) {
            .next = file_list,
            .name = strdup(filename),
            .prev = NULL,
            .block_list = first_block,
            .last_block = first_block,
            .refs = 0,
            .marked_for_deletion = false
    };

    if (file_list != NULL) {
        file_list->prev = new_file_list;
    }
    file_list = new_file_list;
    return new_file_list;
}

void disconnect_file_from_file_list(struct file *file) {
    if (file == file_list)
        file_list = file->next;

    if (file->prev != NULL)
        file->prev->next = file->next;
    if (file->next != NULL)
        file->next->prev = file->prev;

    file->prev = NULL;
    file->next = NULL;
}

void delete_file(struct file *file) {
    disconnect_file_from_file_list(file);
    free(file->name);
    struct block *ptr = file->block_list;
    while (ptr != NULL) {
        free(ptr->memory);
        struct block *next = ptr->next;
        free(ptr);
        ptr = next;
    }
    free(file);
}

enum ufs_error_code
ufs_errno() {
    return ufs_error_code;
}

int throw_error(int specific_error) {
    ufs_error_code = specific_error;
    return -1;
}

int
ufs_open(const char *filename, int flags) {
    struct file *referred_file = try_get_file_by_filename((char *) filename);
    if (referred_file == NULL) {
        if (!specific_flag_is_present(flags, UFS_CREATE))
            return throw_error(UFS_ERR_NO_FILE);
        referred_file = create_file(filename);
    }
    struct filedesc *file_descriptor = malloc(sizeof(struct filedesc));
    *file_descriptor = (struct filedesc) {
            .file = referred_file,
            .current_block = referred_file->block_list,
            .block_offset = 0,
            .flags = flags
    };

    referred_file->refs++;

    return add_file_descriptor(file_descriptor);
}

void write_single_byte(struct filedesc *descriptor, char byte) {
    descriptor->current_block->memory[descriptor->block_offset] = byte;
    if (descriptor->file->last_block == descriptor->current_block
        && descriptor->block_offset == (descriptor->file->last_block->occupied)) {
        descriptor->file->last_block->occupied++;
    }
}

char read_single_byte(struct filedesc *descriptor) {
    return descriptor->current_block->memory[descriptor->block_offset];
}

void advance_descriptor(struct filedesc *descriptor) {
    descriptor->block_offset++;
    if (descriptor->block_offset == BLOCK_SIZE) {
        struct block *next_block = descriptor->current_block->next;
        if (next_block == NULL) {
            // last in file, so we create new
            next_block = malloc(sizeof(struct block));
            *next_block = (struct block) {
                    .next = NULL,
                    .prev = descriptor->current_block,
                    .index = descriptor->current_block->index + 1,
                    .memory = malloc(sizeof(char) * BLOCK_SIZE),
                    .occupied = 0
            };
            descriptor->current_block->next = next_block;
            descriptor->file->last_block = next_block;
        }
        descriptor->current_block = next_block;
        descriptor->block_offset = 0;
    }
}

int write_via_descriptor(struct filedesc *descriptor, const char *buf, int size) {
    int current_size = descriptor->current_block->index * BLOCK_SIZE + descriptor->block_offset;
    int end_size = current_size + size;
    if (end_size > MAX_FILE_SIZE)
        return throw_error(UFS_ERR_NO_MEM);

    for (int i = 0; i < size; ++i) {
        write_single_byte(descriptor, buf[i]);
        advance_descriptor(descriptor);
    }

    return size;
}

int read_via_descriptor(struct filedesc *descriptor, char *buf, int size) {
    int current_size = descriptor->current_block->index * BLOCK_SIZE + descriptor->block_offset;
    int file_size_total = descriptor->file->last_block->index * BLOCK_SIZE + descriptor->file->last_block->occupied;
    int bytes_left_to_read = file_size_total - current_size;
    int read_count = (size > bytes_left_to_read ? bytes_left_to_read : size);

    for (int i = 0; i < read_count; ++i) {
        buf[i] = read_single_byte(descriptor);
        advance_descriptor(descriptor);
    }

    return read_count;
}

ssize_t
ufs_write(int fd, const char *buf, size_t size) {
    struct filedesc *descriptor = try_get_file_descriptor(fd);
    if (descriptor == NULL)
        return throw_error(UFS_ERR_NO_FILE);

    if (specific_flag_is_present(descriptor->flags, UFS_READ_ONLY))
        return throw_error(UFS_ERR_NO_PERMISSION);

    return write_via_descriptor(descriptor, buf, (int) size);
}

ssize_t
ufs_read(int fd, char *buf, size_t size) {
    struct filedesc *descriptor = try_get_file_descriptor(fd);
    if (descriptor == NULL)
        return throw_error(UFS_ERR_NO_FILE);
    if (specific_flag_is_present(descriptor->flags, UFS_WRITE_ONLY))
        return throw_error(UFS_ERR_NO_PERMISSION);
    return read_via_descriptor(descriptor, buf, (int) size);
}

int
ufs_close(int fd) {
    struct filedesc *descriptor = try_get_file_descriptor(fd);
    if (descriptor == NULL)
        return throw_error(UFS_ERR_NO_FILE);
    close_file_descriptor(descriptor);
    return UFS_ERR_NO_ERR;
}

int
ufs_delete(const char *filename) {
    struct file *referred_file = try_get_file_by_filename((char *) filename);
    if (referred_file == NULL)
        return throw_error(UFS_ERR_NO_FILE);
    disconnect_file_from_file_list(referred_file);
    if (referred_file->refs == 0) {
        delete_file(referred_file);
    } else {
        referred_file->marked_for_deletion = true;
    }
    return UFS_ERR_NO_ERR;
}

void
ufs_destroy(void) {
    for (int i = 0; i < file_descriptor_count; ++i) {
        if (file_descriptors[i] != NULL)
            close_file_descriptor(file_descriptors[i]);
    }
    free(file_descriptors);

    while (file_list != NULL)
        delete_file(file_list);
}

void reduce_file_size(struct file *file, int new_size) {
    struct block *new_last_block = file->last_block;
    while (new_last_block->index * BLOCK_SIZE > new_size) {
        new_last_block = new_last_block->prev;
    }

    int new_occupied = new_size - new_last_block->index * BLOCK_SIZE;
    new_last_block->occupied = new_occupied;
    new_last_block->next = NULL;

    for (int i = 0; i < file_descriptor_count; ++i) {
        if (file_descriptors[i] == NULL)
            continue;
        if (file_descriptors[i]->file != file)
            continue;
        if (file_descriptors[i]->current_block->index < new_last_block->index)
            continue;
        if (file_descriptors[i]->current_block->index == new_last_block->index &&
            file_descriptors[i]->block_offset <= new_occupied)
            continue;
        file_descriptors[i]->current_block = new_last_block;
        file_descriptors[i]->block_offset = new_occupied;
    }

    struct block *removal_ptr = file->last_block;
    while (removal_ptr != new_last_block) {
        struct block *next = removal_ptr->prev;
        free(removal_ptr->memory);
        free(removal_ptr);
        removal_ptr = next;
    }
    file->last_block = new_last_block;
}

int
ufs_resize(int fd, size_t new_size) {
    struct filedesc *descriptor = try_get_file_descriptor(fd);
    if (descriptor == NULL)
        return throw_error(UFS_ERR_NO_FILE);
    if (new_size >= MAX_FILE_SIZE)
        return throw_error(UFS_ERR_NO_MEM);
    reduce_file_size(descriptor->file, new_size);
    return 0;
}