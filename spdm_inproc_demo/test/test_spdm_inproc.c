/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPROC_DEMO_PATH "./spdm_inproc_demo"
#define MAX_OUTPUT_SIZE 8192

static int run_demo_and_check_output(void)
{
    FILE *fp;
    char output[MAX_OUTPUT_SIZE];
    size_t output_len;
    int return_code;
    int found_lock_interface = 0;
    int found_demo_complete = 0;
    int found_session_created = 0;

    fp = popen(INPROC_DEMO_PATH, "r");
    if (fp == NULL) {
        printf("TEST_ERROR: Failed to execute spdm_inproc_demo\n");
        return -1;
    }

    output_len = fread(output, 1, sizeof(output) - 1, fp);
    output[output_len] = '\0';
    return_code = pclose(fp);

    if (return_code != 0) {
        printf("TEST_ERROR: Demo exited with non-zero return code: %d\n", return_code);
        printf("TEST_ERROR: Output:\n%s\n", output);
        return -1;
    }

    if (strstr(output, "TDISP LOCK_INTERFACE flow completed successfully") != NULL) {
        found_lock_interface = 1;
    }

    if (strstr(output, "SPDM In-Process Demo completed") != NULL) {
        found_demo_complete = 1;
    }

    if (strstr(output, "Secure session created successfully") != NULL) {
        found_session_created = 1;
    }

    if (!found_session_created) {
        printf("TEST_ERROR: SPDM session creation not detected in output\n");
        return -1;
    }

    if (!found_lock_interface) {
        printf("TEST_ERROR: TDISP LOCK_INTERFACE flow not completed\n");
        return -1;
    }

    if (!found_demo_complete) {
        printf("TEST_ERROR: Demo completion message not found\n");
        return -1;
    }

    printf("TEST_SUCCESS: All SPDM session and TDISP flow validations passed\n");
    return 0;
}

int main(void)
{
    int result;

    printf("TEST_INFO: Starting SPDM In-Process Demo test\n");

    result = run_demo_and_check_output();

    if (result == 0) {
        printf("TEST_INFO: SPDM In-Process Demo test PASSED\n");
    } else {
        printf("TEST_INFO: SPDM In-Process Demo test FAILED\n");
    }

    return result;
}