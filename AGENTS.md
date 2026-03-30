# AGENTS.md - Coding Agent Guidelines for spdm-emu

This document provides guidelines for AI coding agents working on the spdm-emu repository.

## Project Overview

spdm-emu is a sample SPDM (Security Protocol and Data Model) emulator implementation using libspdm. It provides requester and responder emulators for testing SPDM protocol implementations.

## Build Commands

### Prerequisites
- Initialize submodules: `git submodule update --init --recursive`
- Required: GCC (>=5), LLVM/Clang (9+), or Visual Studio (2015/2019/2022)

### Linux Build (GCC)
```bash
mkdir build && cd build
cmake -DARCH=x64 -DTOOLCHAIN=GCC -DTARGET=Release -DCRYPTO=mbedtls ..
make copy_sample_key
make -j$(nproc)
```

### Linux Build (Clang)
```bash
mkdir build && cd build
cmake -DARCH=x64 -DTOOLCHAIN=CLANG -DTARGET=Release -DCRYPTO=openssl ..
make copy_sample_key
make -j$(nproc)
```

### Windows Build (Visual Studio)
Use x86 prompt for ARCH=ia32, x64 prompt for ARCH=x64:
```cmd
mkdir build && cd build
cmake -G"NMake Makefiles" -DARCH=x64 -DTOOLCHAIN=VS2019 -DTARGET=Release -DCRYPTO=mbedtls ..
nmake copy_sample_key
nmake
```

### Build Options
- `ARCH`: x64, ia32, arm, aarch64, riscv32, riscv64, arc
- `TOOLCHAIN`: GCC, CLANG, VS2015, VS2019, VS2022 (Windows); GCC, CLANG (Linux)
- `TARGET`: Debug, Release
- `CRYPTO`: mbedtls, openssl
- `DEVICE`: sample, tpm (default: sample)
- `ENABLE_SYSTEMD`: ON, OFF (Linux only)

## Test Commands

### Run Full Emulator Test
Tests require running responder first, then requester connects:
```bash
cd build/bin
./spdm_responder_emu &
sleep 5
./spdm_requester_emu
```

### Run with Different Transport
```bash
./spdm_responder_emu --trans PCI_DOE &
sleep 5
./spdm_requester_emu --trans PCI_DOE
```

### Run Validator Test
```bash
cd build/bin
./spdm_responder_emu &
sleep 5
./spdm_device_validator_sample
```

### Run TCP Transport Test
```bash
./spdm_responder_emu --trans TCP &
sleep 5
./spdm_requester_emu --trans TCP
```

### Custom Port Testing
```bash
./spdm_responder_emu --port 2323 &
./spdm_requester_emu --port 2323
```

## Format/Lint Commands

### Check Code Formatting
```bash
./script/format_nix.sh --check
```

### Apply Code Formatting
```bash
./script/format_nix.sh
```

### Tab Character Check (CI requirement)
```bash
grep -rn "	" * --include=*.c --include=*.h --include=*.md
# Should return nothing (no tabs allowed)
```

## Code Style Guidelines

### General Rules
- **No tabs**: Use 4-space indentation throughout
- **C99 standard**: Project uses `-std=c99` flag
- **Strict compiler warnings**: `-Wall -Werror` enabled
- **No comments**: Do NOT add comments unless explicitly requested by the user

### Copyright Header
Every source file must start with:
```c
/**
 *  Copyright Notice:
 *  Copyright 2021-2025 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/spdm-emu/blob/main/LICENSE.md
 **/
```

### Naming Conventions
- **Functions**: snake_case with module prefix (e.g., `spdm_transport_none_encode_message`, `libspdm_get_context_size`)
- **Variables**: snake_case (e.g., `message_size`, `transport_message`)
- **Global variables**: prefix `m_` (e.g., `m_spdm_context`, `m_use_version`)
- **Constants/Macros**: UPPER_CASE with `LIBSPDM_` or module prefix (e.g., `LIBSPDM_STATUS_SUCCESS`, `EXE_MODE_SHUTDOWN`)
- **Types**: snake_case with `_t` suffix (e.g., `libspdm_return_t`, `spdm_version_number_t`)
- **Struct names**: snake_case (e.g., `libspdm_data_parameter_t`)

### Function Documentation
Use Doxygen-style documentation for public API functions:
```c
/**
 * Encode an SPDM or APP message to a transport layer message.
 *
 * @param  spdm_context       A pointer to the SPDM context.
 * @param  session_id         Indicates if it is a secured message.
 * @param  message_size       Size in bytes of the message.
 * @param  message            A pointer to the message buffer.
 *
 * @retval LIBSPDM_STATUS_SUCCESS  The message was encoded successfully.
 * @retval LIBSPDM_STATUS_INVALID_PARAMETER  Invalid parameter provided.
 */
```

### Return Types and Error Handling
- Use `libspdm_return_t` for SPDM library functions
- Use `bool` for simple success/failure functions
- Check errors with `LIBSPDM_STATUS_IS_ERROR(status)` macro
- Return specific status codes: `LIBSPDM_STATUS_SUCCESS`, `LIBSPDM_STATUS_INVALID_PARAMETER`, `LIBSPDM_STATUS_UNSUPPORTED_CAP`, etc.

### Logging Macros
Use provided logging macros, not printf directly:
- `EMU_LOG(fmt, ...)` - Debug traces (only when `--verbose` flag set)
- `EMU_INFO(fmt, ...)` - Informational messages to stdout
- `EMU_ERR(fmt, ...)` - Error messages to stderr

### Imports/Includes Order
1. Project-specific headers (e.g., `"spdm_emu.h"`, `"library/spdm_transport_none_lib.h"`)
2. External library headers from libspdm (e.g., `"hal/base.h"`, `"library/spdm_common_lib.h"`)
3. Standard C headers (e.g., `<stdio.h>`, `<stdlib.h>`, `<string.h>`)

Example:
```c
#include "spdm_emu.h"
#include "library/spdm_transport_none_lib.h"
#include "hal/base.h"
#include "stdio.h"
#include "stdlib.h"
```

### Pointer Parameters
- Use `const` for input pointers that shouldn't be modified
- Use double pointers (`**`) for output buffer parameters
- Document whether NULL is acceptable in function docs

### Platform-Specific Code
Use preprocessor conditionals for platform differences:
```c
#ifdef _MSC_VER
    EMU_ERR("Error - %x\n", WSAGetLastError());
#else
    EMU_ERR("Error - %x\n", errno);
#endif
```

### Memory Management
- Use libspdm memory functions from `hal/library/memlib.h`
- Check allocation results before use
- Use `size_t` for size parameters

### Boolean Values
- Use `true`/`false` (not 1/0) for boolean variables
- Use `bool` type from `hal/base.h`

## Project Structure

- `include/library/` - Public header files
- `library/` - Library implementation (transport layers, protocol libs)
- `spdm_emu/` - Main emulator executables
  - `spdm_requester_emu/` - Requester implementation
  - `spdm_responder_emu/` - Responder implementation
  - `spdm_emu_common/` - Shared emulator code
  - `spdm_device_validator_sample/` - Validation tests
- `SPDM-Responder-Validator/` - Conformance test framework
- `script/` - Build/format scripts
- `doc/` - Documentation

## Key Dependencies

- `libspdm/` - Core SPDM library (submodule)
- Crypto library: mbedtls or openssl
- Build output: `build/bin/` directory

## Important Notes

- This is sample code for concept demonstration, not production quality
- Run `copy_sample_key` target before running tests (copies test certificates)
- Tests require responder running before requester can connect
- Use `--verbose` flag for detailed debug output during testing
- CI checks for tab characters in source files will fail build