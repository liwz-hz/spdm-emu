/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#include "library/spdm_common_lib.h"
#include "spdm_context.h"
#include "stdio.h"
#include "stdlib.h"

void libspdm_dump_hex_str(const uint8_t *buffer, size_t buffer_size)
{
    size_t index;

    for (index = 0; index < buffer_size; index++) {
        printf("%02x", buffer[index]);
    }
}

bool libspdm_read_input_file(const char *file_name, void **file_data,
                             size_t *file_size)
{
    FILE *fp_in;
    size_t temp_result;

    if ((fp_in = fopen(file_name, "rb")) == NULL) {
        printf("Unable to open file %s\n", file_name);
        *file_data = NULL;
        return false;
    }

    fseek(fp_in, 0, SEEK_END);
    *file_size = ftell(fp_in);
    if (*file_size == -1) {
        printf("Unable to get the file size %s\n", file_name);
        *file_data = NULL;
        fclose(fp_in);
        return false;
    }

    *file_data = (void *)malloc(*file_size);
    if (NULL == *file_data) {
        printf("No sufficient memory to allocate %s\n", file_name);
        fclose(fp_in);
        return false;
    }

    fseek(fp_in, 0, SEEK_SET);
    temp_result = fread(*file_data, 1, *file_size, fp_in);
    if (temp_result != *file_size) {
        printf("Read input file error %s", file_name);
        free((void *)*file_data);
        fclose(fp_in);
        return false;
    }

    fclose(fp_in);

    return true;
}

bool libspdm_write_output_file(const char *file_name, const void *file_data,
                               size_t file_size)
{
    (void)file_name;
    (void)file_data;
    (void)file_size;
    return false;
}