#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <udfread/udfread.h>

#ifndef UDFREAD_CLI_VERSION
#define UDFREAD_CLI_VERSION "0.1.0"
#endif

#ifndef UDF_BLOCK_SIZE
#define UDF_BLOCK_SIZE 2048
#endif

#define COPY_BUFFER_SIZE (1024U * 1024U)
#define MAX_RECURSION_DEPTH 128U

#define EXIT_OK 0
#define EXIT_FAILURE_RUNTIME 1
#define EXIT_FAILURE_USAGE 2

/* This symbol is part of libudfread's public ABI and is available in 1.1.x+. */
extern void udfread_get_version(int *major, int *minor, int *micro);

struct image_handle {
    udfread *volume;
    const char *path;
};

struct directory_entry {
    char *name;
    unsigned int type;
};

struct directory_entries {
    struct directory_entry *items;
    size_t count;
    size_t capacity;
};

static void print_usage(FILE *stream)
{
    fprintf(stream,
        "Usage:\n"
        "  udfread --help\n"
        "  udfread --version\n"
        "  udfread info IMAGE\n"
        "  udfread ls [-l] [-R] IMAGE [PATH]\n"
        "  udfread stat IMAGE PATH\n"
        "  udfread cat IMAGE PATH\n"
        "  udfread extract IMAGE PATH DESTINATION\n"
        "  udfread range [-o DESTINATION] IMAGE PATH OFFSET [LENGTH]\n"
        "  udfread map IMAGE PATH\n"
        "  udfread blocks IMAGE PATH FILE_BLOCK [COUNT]\n"
        "\n"
        "Commands:\n"
        "  info      Show UDF volume identifiers.\n"
        "  ls        List a directory; -l adds sizes and -R recurses.\n"
        "  stat      Show the type, size, and first physical block of a path.\n"
        "  cat       Stream a complete file to standard output.\n"
        "  extract   Copy a complete file to a local destination.\n"
        "  range     Copy a byte range, or from OFFSET to EOF if LENGTH is omitted.\n"
        "  map       Show contiguous physical LBA runs backing a file.\n"
        "  blocks    Stream logical 2048-byte file blocks to standard output.\n"
        "\n"
        "Numbers accept decimal or a 0x hexadecimal prefix. Use '-' as a\n"
        "destination to write to standard output.\n");
}

static void print_version(void)
{
    int major = 0;
    int minor = 0;
    int micro = 0;

    udfread_get_version(&major, &minor, &micro);
    printf("udfread-cli %s\n", UDFREAD_CLI_VERSION);
    printf("libudfread %d.%d.%d\n", major, minor, micro);
}

static int usage_error(const char *message)
{
    if (message != NULL)
        fprintf(stderr, "udfread: %s\n\n", message);

    print_usage(stderr);
    return EXIT_FAILURE_USAGE;
}

static int runtime_error(const char *message)
{
    fprintf(stderr, "udfread: %s\n", message);
    return EXIT_FAILURE_RUNTIME;
}

static int runtime_error_path(const char *message, const char *path)
{
    fprintf(stderr, "udfread: %s: %s\n", message, path);
    return EXIT_FAILURE_RUNTIME;
}

static bool parse_u64(const char *text, uint64_t *value)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text == NULL || text[0] == '\0' || text[0] == '-')
        return false;

    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0')
        return false;

    *value = (uint64_t)parsed;
    return true;
}

static int open_image(struct image_handle *image, const char *path)
{
    image->volume = udfread_init();
    image->path = path;

    if (image->volume == NULL)
        return runtime_error("failed to initialize libudfread");

    if (udfread_open(image->volume, path) < 0) {
        udfread_close(image->volume);
        image->volume = NULL;
        return runtime_error_path("failed to open UDF image", path);
    }

    return EXIT_OK;
}

static void close_image(struct image_handle *image)
{
    if (image->volume != NULL)
        udfread_close(image->volume);

    image->volume = NULL;
}

static char *duplicate_string(const char *text)
{
    char *copy = strdup(text);

    if (copy == NULL)
        runtime_error("out of memory");

    return copy;
}

static char *join_display_path(const char *parent, const char *name)
{
    size_t parent_length = strlen(parent);
    size_t name_length = strlen(name);
    bool root = parent_length == 1 && parent[0] == '/';
    size_t total = parent_length + (root ? 0U : 1U) + name_length + 1U;
    char *result = malloc(total);

    if (result == NULL) {
        runtime_error("out of memory");
        return NULL;
    }

    if (root)
        snprintf(result, total, "/%s", name);
    else
        snprintf(result, total, "%s/%s", parent, name);

    return result;
}

static char *normalize_display_path(const char *path)
{
    size_t length;
    char *result;

    if (path == NULL || path[0] == '\0' || strcmp(path, "/") == 0)
        return duplicate_string("/");

    length = strlen(path);
    while (length > 1 && (path[length - 1] == '/' || path[length - 1] == '\\'))
        length--;

    if (path[0] == '/' || path[0] == '\\') {
        result = malloc(length + 1U);
        if (result == NULL) {
            runtime_error("out of memory");
            return NULL;
        }
        memcpy(result, path, length);
        result[length] = '\0';
        result[0] = '/';
        return result;
    }

    result = malloc(length + 2U);
    if (result == NULL) {
        runtime_error("out of memory");
        return NULL;
    }

    result[0] = '/';
    memcpy(result + 1, path, length);
    result[length + 1] = '\0';
    return result;
}

static void free_directory_entries(struct directory_entries *entries)
{
    size_t index;

    for (index = 0; index < entries->count; index++)
        free(entries->items[index].name);

    free(entries->items);
    entries->items = NULL;
    entries->count = 0;
    entries->capacity = 0;
}

static bool append_directory_entry(
    struct directory_entries *entries,
    const struct udfread_dirent *entry)
{
    struct directory_entry *resized;
    size_t new_capacity;

    if (entries->count == entries->capacity) {
        new_capacity = entries->capacity == 0 ? 32U : entries->capacity * 2U;
        if (new_capacity < entries->capacity ||
            new_capacity > SIZE_MAX / sizeof(*entries->items)) {
            runtime_error("directory contains too many entries");
            return false;
        }

        resized = realloc(entries->items, new_capacity * sizeof(*entries->items));
        if (resized == NULL) {
            runtime_error("out of memory");
            return false;
        }

        entries->items = resized;
        entries->capacity = new_capacity;
    }

    entries->items[entries->count].name = duplicate_string(entry->d_name);
    if (entries->items[entries->count].name == NULL)
        return false;

    entries->items[entries->count].type = entry->d_type;
    entries->count++;
    return true;
}

static int compare_directory_entries(const void *left, const void *right)
{
    const struct directory_entry *left_entry = left;
    const struct directory_entry *right_entry = right;

    return strcmp(left_entry->name, right_entry->name);
}

static bool read_directory_entries(UDFDIR *directory, struct directory_entries *entries)
{
    struct udfread_dirent storage;
    struct udfread_dirent *entry;

    memset(entries, 0, sizeof(*entries));

    while ((entry = udfread_readdir(directory, &storage)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        if (!append_directory_entry(entries, entry)) {
            free_directory_entries(entries);
            return false;
        }
    }

    if (entries->count > 1)
        qsort(entries->items, entries->count, sizeof(*entries->items),
            compare_directory_entries);
    return true;
}

static char entry_type_character(unsigned int type)
{
    if (type == UDF_DT_DIR)
        return 'd';
    if (type == UDF_DT_REG)
        return '-';
    return '?';
}

static void print_long_entry(UDFDIR *parent, const struct directory_entry *entry,
    const char *display_path)
{
    int64_t size = -1;

    if (entry->type == UDF_DT_REG) {
        UDFFILE *file = udfread_file_openat(parent, entry->name);

        if (file != NULL) {
            size = udfread_file_size(file);
            udfread_file_close(file);
        }
    }

    if (size >= 0)
        printf("%c %12" PRId64 " %s\n", entry_type_character(entry->type),
            size, display_path);
    else
        printf("%c %12s %s\n", entry_type_character(entry->type), "-",
            display_path);
}

static int list_directory(UDFDIR *directory, const char *display_path,
    bool recursive, bool long_format, unsigned int depth)
{
    struct directory_entries entries;
    size_t index;
    int result = EXIT_OK;

    if (depth > MAX_RECURSION_DEPTH)
        return runtime_error("maximum directory recursion depth exceeded");

    if (!read_directory_entries(directory, &entries))
        return EXIT_FAILURE_RUNTIME;

    for (index = 0; index < entries.count; index++) {
        char *child_path = join_display_path(display_path, entries.items[index].name);

        if (child_path == NULL) {
            result = EXIT_FAILURE_RUNTIME;
            break;
        }

        if (long_format)
            print_long_entry(directory, &entries.items[index], child_path);
        else
            printf("%s\n", child_path);

        free(child_path);
    }

    if (result == EXIT_OK && recursive) {
        for (index = 0; index < entries.count; index++) {
            UDFDIR *child_directory;
            char *child_path;

            if (entries.items[index].type != UDF_DT_DIR)
                continue;

            child_directory = udfread_opendir_at(directory, entries.items[index].name);
            if (child_directory == NULL) {
                fprintf(stderr, "udfread: failed to open directory entry: %s\n",
                    entries.items[index].name);
                result = EXIT_FAILURE_RUNTIME;
                continue;
            }

            child_path = join_display_path(display_path, entries.items[index].name);
            if (child_path == NULL) {
                udfread_closedir(child_directory);
                result = EXIT_FAILURE_RUNTIME;
                break;
            }

            printf("\n%s:\n", child_path);
            if (list_directory(child_directory, child_path, true, long_format,
                    depth + 1U) != EXIT_OK)
                result = EXIT_FAILURE_RUNTIME;

            free(child_path);
            udfread_closedir(child_directory);
        }
    }

    free_directory_entries(&entries);
    return result;
}

static void print_escaped_bytes(const unsigned char *data, size_t size)
{
    size_t index;

    putchar('"');
    for (index = 0; index < size; index++) {
        unsigned char value = data[index];

        if (value == '\\' || value == '"')
            printf("\\%c", value);
        else if (value >= 0x20U && value <= 0x7eU)
            putchar((int)value);
        else
            printf("\\x%02x", value);
    }
    putchar('"');
}

static int command_info(const char *image_path)
{
    struct image_handle image;
    const char *volume_id;
    unsigned char volume_set_id[1024];
    size_t volume_set_id_size;
    int result;

    result = open_image(&image, image_path);
    if (result != EXIT_OK)
        return result;

    volume_id = udfread_get_volume_id(image.volume);
    volume_set_id_size = udfread_get_volume_set_id(image.volume,
        volume_set_id, sizeof(volume_set_id));

    printf("Image: %s\n", image_path);
    printf("Volume ID: %s\n", volume_id != NULL ? volume_id : "<unavailable>");
    printf("Volume set ID: ");
    if (volume_set_id_size == 0)
        printf("<unavailable>");
    else if (volume_set_id_size > sizeof(volume_set_id))
        printf("<too large: %zu bytes>", volume_set_id_size);
    else
        print_escaped_bytes(volume_set_id, volume_set_id_size);
    putchar('\n');

    close_image(&image);
    return EXIT_OK;
}

static int command_ls(int argc, char **argv)
{
    bool recursive = false;
    bool long_format = false;
    int index = 2;
    int remaining;
    const char *image_path;
    const char *udf_path;
    char *display_path;
    struct image_handle image;
    UDFDIR *directory;
    int result;

    while (index < argc && argv[index][0] == '-') {
        if (strcmp(argv[index], "--") == 0) {
            index++;
            break;
        }
        if (strcmp(argv[index], "-R") == 0 ||
            strcmp(argv[index], "--recursive") == 0) {
            recursive = true;
            index++;
            continue;
        }
        if (strcmp(argv[index], "-l") == 0 ||
            strcmp(argv[index], "--long") == 0) {
            long_format = true;
            index++;
            continue;
        }
        return usage_error("unknown ls option");
    }

    remaining = argc - index;
    if (remaining < 1 || remaining > 2)
        return usage_error("ls expects IMAGE and an optional PATH");

    image_path = argv[index];
    udf_path = remaining == 2 ? argv[index + 1] : "/";
    display_path = normalize_display_path(udf_path);
    if (display_path == NULL)
        return EXIT_FAILURE_RUNTIME;

    result = open_image(&image, image_path);
    if (result != EXIT_OK) {
        free(display_path);
        return result;
    }

    directory = udfread_opendir(image.volume, udf_path);
    if (directory == NULL) {
        close_image(&image);
        free(display_path);
        return runtime_error_path("failed to open UDF directory", udf_path);
    }

    if (recursive)
        printf("%s:\n", display_path);

    result = list_directory(directory, display_path, recursive, long_format, 0);

    udfread_rewinddir(directory);
    udfread_closedir(directory);
    close_image(&image);
    free(display_path);
    return result;
}

static int command_stat(const char *image_path, const char *udf_path)
{
    struct image_handle image;
    UDFDIR *directory;
    UDFFILE *file;
    int64_t size;
    uint64_t blocks;
    uint32_t first_lba;
    int result;

    result = open_image(&image, image_path);
    if (result != EXIT_OK)
        return result;

    directory = udfread_opendir(image.volume, udf_path);
    if (directory != NULL) {
        printf("Path: %s\n", udf_path);
        printf("Type: directory\n");
        udfread_closedir(directory);
        close_image(&image);
        return EXIT_OK;
    }

    file = udfread_file_open(image.volume, udf_path);
    if (file == NULL) {
        close_image(&image);
        return runtime_error_path("path not found", udf_path);
    }

    size = udfread_file_size(file);
    if (size < 0) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error_path("failed to get file size", udf_path);
    }

    blocks = ((uint64_t)size + UDF_BLOCK_SIZE - 1U) / UDF_BLOCK_SIZE;
    first_lba = blocks > 0 ? udfread_file_lba(file, 0) : 0;

    printf("Path: %s\n", udf_path);
    printf("Type: regular file\n");
    printf("Size: %" PRId64 " bytes\n", size);
    printf("Logical blocks: %" PRIu64 "\n", blocks);
    if (blocks == 0)
        printf("First LBA: <empty file>\n");
    else if (first_lba == 0)
        printf("First LBA: <inline or unavailable>\n");
    else {
        printf("First LBA: %" PRIu32 "\n", first_lba);
        printf("First physical offset: %" PRIu64 " bytes\n",
            (uint64_t)first_lba * UDF_BLOCK_SIZE);
    }

    udfread_file_close(file);
    close_image(&image);
    return EXIT_OK;
}

static FILE *open_output(const char *path, bool *must_close)
{
    FILE *output;

    if (strcmp(path, "-") == 0) {
        *must_close = false;
        return stdout;
    }

    output = fopen(path, "wb");
    if (output == NULL) {
        fprintf(stderr, "udfread: failed to open output '%s': %s\n",
            path, strerror(errno));
        return NULL;
    }

    *must_close = true;
    return output;
}

static int close_output(FILE *output, bool must_close, const char *path)
{
    if (fflush(output) != 0) {
        fprintf(stderr, "udfread: failed to flush output '%s': %s\n",
            path, strerror(errno));
        if (must_close)
            fclose(output);
        return EXIT_FAILURE_RUNTIME;
    }

    if (must_close && fclose(output) != 0) {
        fprintf(stderr, "udfread: failed to close output '%s': %s\n",
            path, strerror(errno));
        return EXIT_FAILURE_RUNTIME;
    }

    return EXIT_OK;
}

static int copy_file_bytes(UDFFILE *file, FILE *output, uint64_t bytes)
{
    unsigned char *buffer = malloc(COPY_BUFFER_SIZE);
    uint64_t remaining = bytes;
    int result = EXIT_OK;

    if (buffer == NULL)
        return runtime_error("out of memory");

    while (remaining > 0) {
        size_t request = remaining > COPY_BUFFER_SIZE
            ? COPY_BUFFER_SIZE
            : (size_t)remaining;
        ssize_t received = udfread_file_read(file, buffer, request);

        if (received < 0) {
            result = runtime_error("failed while reading the UDF file");
            break;
        }
        if (received == 0) {
            result = runtime_error("unexpected end of UDF file");
            break;
        }
        if ((size_t)received > request) {
            result = runtime_error("libudfread returned an invalid read length");
            break;
        }

        if (fwrite(buffer, 1, (size_t)received, output) != (size_t)received) {
            fprintf(stderr, "udfread: failed while writing output: %s\n",
                strerror(errno));
            result = EXIT_FAILURE_RUNTIME;
            break;
        }

        remaining -= (uint64_t)received;
    }

    free(buffer);
    return result;
}

static int copy_range(const char *image_path, const char *udf_path,
    uint64_t offset, bool length_given, uint64_t requested_length,
    const char *output_path)
{
    struct image_handle image;
    UDFFILE *file;
    int64_t signed_size;
    uint64_t size;
    uint64_t length;
    FILE *output;
    bool must_close;
    int result;

    result = open_image(&image, image_path);
    if (result != EXIT_OK)
        return result;

    file = udfread_file_open(image.volume, udf_path);
    if (file == NULL) {
        close_image(&image);
        return runtime_error_path("failed to open UDF file", udf_path);
    }

    signed_size = udfread_file_size(file);
    if (signed_size < 0) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error_path("failed to get file size", udf_path);
    }
    size = (uint64_t)signed_size;

    if (offset > size || offset > INT64_MAX) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error("range offset exceeds the file size");
    }

    length = length_given ? requested_length : size - offset;
    if (length > size - offset) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error("requested range exceeds the file size");
    }

    if (udfread_file_seek(file, (int64_t)offset, UDF_SEEK_SET) != (int64_t)offset ||
        udfread_file_tell(file) != (int64_t)offset) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error("failed to seek within the UDF file");
    }

    output = open_output(output_path, &must_close);
    if (output == NULL) {
        udfread_file_close(file);
        close_image(&image);
        return EXIT_FAILURE_RUNTIME;
    }

    result = copy_file_bytes(file, output, length);
    if (close_output(output, must_close, output_path) != EXIT_OK)
        result = EXIT_FAILURE_RUNTIME;

    udfread_file_close(file);
    close_image(&image);
    return result;
}

static int command_range(int argc, char **argv)
{
    const char *output_path = "-";
    int index = 2;
    int remaining;
    uint64_t offset;
    uint64_t length = 0;
    bool length_given;

    if (index < argc && (strcmp(argv[index], "-o") == 0 ||
            strcmp(argv[index], "--output") == 0)) {
        if (index + 1 >= argc)
            return usage_error("range output option requires a path");
        output_path = argv[index + 1];
        index += 2;
    }

    if (index < argc && strcmp(argv[index], "--") == 0)
        index++;

    remaining = argc - index;
    if (remaining < 3 || remaining > 4)
        return usage_error("range expects IMAGE PATH OFFSET and an optional LENGTH");

    if (!parse_u64(argv[index + 2], &offset))
        return usage_error("invalid range offset");

    length_given = remaining == 4;
    if (length_given && !parse_u64(argv[index + 3], &length))
        return usage_error("invalid range length");

    return copy_range(argv[index], argv[index + 1], offset,
        length_given, length, output_path);
}

static void print_map_run(uint32_t file_block, uint32_t lba,
    uint64_t run_blocks, uint64_t file_size)
{
    uint64_t logical_offset = (uint64_t)file_block * UDF_BLOCK_SIZE;
    uint64_t run_bytes = run_blocks * UDF_BLOCK_SIZE;
    uint64_t remaining = file_size - logical_offset;

    if (run_bytes > remaining)
        run_bytes = remaining;

    printf("%" PRIu32 "\t%" PRIu64 "\t", file_block, logical_offset);
    if (lba == 0)
        printf("-\t-\t");
    else
        printf("%" PRIu32 "\t%" PRIu64 "\t", lba,
            (uint64_t)lba * UDF_BLOCK_SIZE);
    printf("%" PRIu64 "\t%" PRIu64 "\n", run_blocks, run_bytes);
}

static int command_map(const char *image_path, const char *udf_path)
{
    struct image_handle image;
    UDFFILE *file;
    int64_t signed_size;
    uint64_t size;
    uint64_t block_count;
    uint64_t block_index;
    uint32_t run_file_block;
    uint32_t run_lba;
    uint32_t previous_lba;
    uint64_t run_blocks;
    int result;

    result = open_image(&image, image_path);
    if (result != EXIT_OK)
        return result;

    file = udfread_file_open(image.volume, udf_path);
    if (file == NULL) {
        close_image(&image);
        return runtime_error_path("failed to open UDF file", udf_path);
    }

    signed_size = udfread_file_size(file);
    if (signed_size < 0) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error_path("failed to get file size", udf_path);
    }

    size = (uint64_t)signed_size;
    block_count = (size + UDF_BLOCK_SIZE - 1U) / UDF_BLOCK_SIZE;
    if (block_count > UINT32_MAX) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error("file is too large for libudfread's 32-bit block mapping API");
    }

    printf("FILE_BLOCK\tFILE_OFFSET\tLBA\tIMAGE_OFFSET\tBLOCKS\tBYTES\n");
    if (block_count == 0) {
        udfread_file_close(file);
        close_image(&image);
        return EXIT_OK;
    }

    run_file_block = 0;
    run_lba = udfread_file_lba(file, 0);
    previous_lba = run_lba;
    run_blocks = 1;

    for (block_index = 1; block_index < block_count; block_index++) {
        uint32_t lba = udfread_file_lba(file, (uint32_t)block_index);
        bool contiguous;

        if (run_lba == 0)
            contiguous = lba == 0;
        else
            contiguous = lba != 0 && previous_lba != UINT32_MAX &&
                lba == previous_lba + 1U;

        if (contiguous) {
            run_blocks++;
            previous_lba = lba;
            continue;
        }

        print_map_run(run_file_block, run_lba, run_blocks, size);
        run_file_block = (uint32_t)block_index;
        run_lba = lba;
        previous_lba = lba;
        run_blocks = 1;
    }

    print_map_run(run_file_block, run_lba, run_blocks, size);

    udfread_file_close(file);
    close_image(&image);
    return EXIT_OK;
}

static int command_blocks(int argc, char **argv)
{
    uint64_t first_block_value;
    uint64_t count_value = 1;
    uint32_t first_block;
    uint32_t count;
    struct image_handle image;
    UDFFILE *file;
    int64_t signed_size;
    uint64_t available_blocks;
    unsigned char *buffer;
    uint32_t current_block;
    uint32_t remaining;
    int result;

    if (argc < 5 || argc > 6)
        return usage_error("blocks expects IMAGE PATH FILE_BLOCK and an optional COUNT");

    if (!parse_u64(argv[4], &first_block_value) || first_block_value > UINT32_MAX)
        return usage_error("invalid first file block");
    if (argc == 6 && (!parse_u64(argv[5], &count_value) || count_value > UINT32_MAX))
        return usage_error("invalid block count");

    first_block = (uint32_t)first_block_value;
    count = (uint32_t)count_value;

    result = open_image(&image, argv[2]);
    if (result != EXIT_OK)
        return result;

    file = udfread_file_open(image.volume, argv[3]);
    if (file == NULL) {
        close_image(&image);
        return runtime_error_path("failed to open UDF file", argv[3]);
    }

    signed_size = udfread_file_size(file);
    if (signed_size < 0) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error_path("failed to get file size", argv[3]);
    }

    available_blocks = ((uint64_t)signed_size + UDF_BLOCK_SIZE - 1U) /
        UDF_BLOCK_SIZE;
    if ((uint64_t)first_block > available_blocks ||
        (uint64_t)count > available_blocks - (uint64_t)first_block) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error("requested logical blocks exceed the file");
    }

    buffer = malloc(512U * UDF_BLOCK_SIZE);
    if (buffer == NULL) {
        udfread_file_close(file);
        close_image(&image);
        return runtime_error("out of memory");
    }

    current_block = first_block;
    remaining = count;
    result = EXIT_OK;

    while (remaining > 0) {
        uint32_t request = remaining > 512U ? 512U : remaining;
        uint32_t received = udfread_read_blocks(file, buffer, current_block,
            request, 0);

        if (received == 0 || received > request) {
            result = runtime_error("failed while reading UDF file blocks");
            break;
        }

        if (fwrite(buffer, UDF_BLOCK_SIZE, received, stdout) != received) {
            fprintf(stderr, "udfread: failed while writing output: %s\n",
                strerror(errno));
            result = EXIT_FAILURE_RUNTIME;
            break;
        }

        current_block += received;
        remaining -= received;
    }

    if (fflush(stdout) != 0) {
        fprintf(stderr, "udfread: failed to flush standard output: %s\n",
            strerror(errno));
        result = EXIT_FAILURE_RUNTIME;
    }

    free(buffer);
    udfread_file_close(file);
    close_image(&image);
    return result;
}

int main(int argc, char **argv)
{
    const char *command;

    if (argc < 2)
        return usage_error(NULL);

    command = argv[1];

    if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0 ||
        strcmp(command, "help") == 0) {
        print_usage(stdout);
        return EXIT_OK;
    }

    if (strcmp(command, "-V") == 0 || strcmp(command, "--version") == 0 ||
        strcmp(command, "version") == 0) {
        print_version();
        return EXIT_OK;
    }

    if (strcmp(command, "info") == 0) {
        if (argc != 3)
            return usage_error("info expects exactly one IMAGE");
        return command_info(argv[2]);
    }

    if (strcmp(command, "ls") == 0)
        return command_ls(argc, argv);

    if (strcmp(command, "stat") == 0) {
        if (argc != 4)
            return usage_error("stat expects IMAGE and PATH");
        return command_stat(argv[2], argv[3]);
    }

    if (strcmp(command, "cat") == 0) {
        if (argc != 4)
            return usage_error("cat expects IMAGE and PATH");
        return copy_range(argv[2], argv[3], 0, false, 0, "-");
    }

    if (strcmp(command, "extract") == 0) {
        if (argc != 5)
            return usage_error("extract expects IMAGE PATH and DESTINATION");
        return copy_range(argv[2], argv[3], 0, false, 0, argv[4]);
    }

    if (strcmp(command, "range") == 0)
        return command_range(argc, argv);

    if (strcmp(command, "map") == 0) {
        if (argc != 4)
            return usage_error("map expects IMAGE and PATH");
        return command_map(argv[2], argv[3]);
    }

    if (strcmp(command, "blocks") == 0)
        return command_blocks(argc, argv);

    return usage_error("unknown command");
}
