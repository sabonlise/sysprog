#include "userfs.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

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
    bool pending_deletion;
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;

    int flags;
    int offset;
	/* PUT HERE OTHER MEMBERS */
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.
 */
static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;
static int file_descriptor_capacity = 0;

enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

struct file* find_file(const char *name) {
    struct file* curr = file_list;

    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            if (curr->pending_deletion) {
                return NULL;
            }
            break;
        }
        curr = curr->next;
    }

    return curr;
}

struct block* create_new_block(struct block* prev_block) {
    struct block* block = malloc(sizeof(struct block));

    block->memory = malloc(sizeof(char) * BLOCK_SIZE + 1);
    block->occupied = 0;
    block->next = NULL;
    block->prev = NULL;
    block->index = 0;

    if (prev_block != NULL) {
        block->prev = prev_block;
        block->index = prev_block->index + 1;
    }

    return block;
}

struct file* create_new_file(const char *name) {
    struct file* file = malloc(sizeof(struct file));
    struct block* block = create_new_block(NULL);

    file->block_list = block;
    file->last_block = block;
    file->refs = 0;
    file->name = strdup(name);
    file->next = file_list;
    file->prev = NULL;
    file->pending_deletion = false;

    if (file_list != NULL) {
        file_list->prev = file;
    }
    file_list = file;

    return file;
}

int get_next_fd() {
    int i = 0;

    for (; i < file_descriptor_capacity; i++) {
        if (file_descriptors[i] == NULL) {
            break;
        }
    }

    return i;
}

int check_fd(int fd) {
    if (fd < 0 || fd > file_descriptor_capacity || file_descriptors == NULL ||
        file_descriptors[fd] == NULL || file_descriptor_count == 0) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    return fd;
}

int create_new_fd(struct file* file, int flags) {
    if (file_descriptor_capacity == file_descriptor_count) {
        file_descriptor_capacity = (int) ((file_descriptor_capacity + 1) * 1.5);

        if (file_descriptor_count == 0) {
            free(file_descriptors);
            file_descriptors = malloc(file_descriptor_capacity * sizeof(struct filedesc));
        } else {
            struct filedesc **tmp;
            tmp = realloc(file_descriptors, file_descriptor_capacity * sizeof(struct filedesc));

            if (tmp == NULL) {
                free(file_descriptors);
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            } else {
                file_descriptors = tmp;
            }
        }

        memset(file_descriptors + file_descriptor_count,
               0,
               (file_descriptor_capacity - file_descriptor_count) * sizeof(struct filedesc));
    }

    int fd = get_next_fd();

    file_descriptors[fd] = malloc(sizeof(struct filedesc));

    file_descriptors[fd]->file = file;
    file_descriptors[fd]->flags = flags;
    file_descriptors[fd]->offset = 0;

    file->refs++;
    file_descriptor_count++;

    return fd;
}


int ufs_open(const char *filename, int flags)
{
    struct file* file = find_file(filename);

    if (file == NULL) {
        if (!(flags & UFS_CREATE)) {
            ufs_error_code = UFS_ERR_NO_FILE;
            return -1;
        }
        file = create_new_file(filename);
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return create_new_fd(file, flags);
}

ssize_t
ufs_write(int fd, const char *buf, size_t size)
{
    ssize_t bytes_written = 0;

    if (check_fd(fd) == -1) {
        return -1;
    }

    struct filedesc* file_descriptor = file_descriptors[fd];

    if (file_descriptor->flags & UFS_READ_ONLY) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    struct file* file = file_descriptor->file;

    ssize_t write_size;
    while (size > 0) {
        if (file_descriptor->offset == BLOCK_SIZE) {
            if (file->last_block->index + 1 >= (MAX_FILE_SIZE / BLOCK_SIZE)) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }

            file->last_block->next = create_new_block(file->last_block);
            file->last_block = file->last_block->next;

            file_descriptor->offset = 0;
        }

        write_size = size < (size_t) (BLOCK_SIZE - file_descriptor->offset) ?
                (ssize_t) size :
                (ssize_t) (BLOCK_SIZE - file_descriptor->offset);

        memcpy(file->last_block->memory + file_descriptor->offset,
               buf + bytes_written,
               write_size);

        size -= write_size;
        bytes_written += write_size;
        file_descriptor->offset += (int) write_size;

        if (file_descriptor->offset > file->last_block->occupied) {
            file->last_block->occupied = file_descriptor->offset;
        }
    }

    ufs_error_code = UFS_ERR_NO_ERR;
	return bytes_written;
}

ssize_t ufs_read(int fd, char *buf, size_t size)
{
    ssize_t bytes_read = 0;

    if (check_fd(fd) == -1) {
        return -1;
    }

    struct filedesc* file_descriptor = file_descriptors[fd];

    if (file_descriptor->flags & UFS_WRITE_ONLY) {
        ufs_error_code = UFS_ERR_NO_PERMISSION;
        return -1;
    }

    struct file* file = file_descriptor->file;

    ssize_t read_size;
    while (size > 0) {
        if (file_descriptor->offset == BLOCK_SIZE) {
            if (file->block_list->next == NULL) {
                break;
            }
            file->block_list = file->block_list->next;
            file_descriptor->offset = 0;
        }

        read_size = size < (size_t) (file->block_list->occupied - file_descriptor->offset) ?
                    (ssize_t) size :
                    (ssize_t) (file->block_list->occupied - file_descriptor->offset);

        if (read_size == 0) break;

        memcpy(buf + bytes_read, file->block_list->memory + file_descriptor->offset, read_size);
        size -= read_size;
        bytes_read += read_size;
        file_descriptor->offset += (int) read_size;

    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return bytes_read;
}

void clear_file(struct file* file) {
    struct block* current_block = file->last_block;
    while (current_block != NULL) {
        free(current_block->memory);
        struct block* prev_block = current_block->prev;
        free(current_block);
        current_block = prev_block;
    }
    free(file->name);
    free(file);
}

int ufs_close(int fd) {
    if (check_fd(fd) == -1) {
        return -1;
    }

    struct filedesc* file_descriptor = file_descriptors[fd];

    struct file* file = file_descriptor->file;

    if (--file->refs == 0 && file->pending_deletion == true) {
        clear_file(file);
    }

    free(file_descriptors[fd]);
    file_descriptors[fd] = NULL;
    file_descriptor_count--;

    ufs_error_code = UFS_ERR_NO_ERR;
    return 0;
}

int ufs_delete(const char *filename) {
	struct file* file = find_file(filename);

    if (file == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    if (file->next != NULL) {
        file->next->prev = file->prev;
    }

    if (file->prev != NULL) {
        file->prev->next = file->next;
    } else {
        file_list = file->next;
    }

    file->prev = NULL;
    file->next = NULL;

    if (file->refs == 0) {
        clear_file(file);
    } else if (file->refs > 0) {
        file->pending_deletion = true;
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return 0;
}

void ufs_destroy(void) {
    struct file* current_file = file_list;
    while (current_file != NULL) {
        struct file* next_file = current_file->next;
        clear_file(current_file);
        current_file = next_file;
    }

    for (int i = 0; i < file_descriptor_capacity; i++) {
        if (file_descriptors[i] != NULL) {
            if (file_descriptors[i]->file != NULL) {
                clear_file(file_descriptors[i]->file);
            }
            free(file_descriptors[i]);
        }
    }

    free(file_descriptors);
}
