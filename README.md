# Power-of-2 Allocator

A power-of-2 memory allocator extracted from an xv6 kernel assignment and ported to a standalone userspace C program.

The original implementation lived inside a GWU operating systems course project ([hw6-19-power-of-2-malloc-bushidocodes](https://github.com/bushidocodes/hw6-19-power-of-2-malloc-bushidocodes)) and depended on xv6 kernel headers and the `sbrk` syscall stub from xv6's user library. This repo strips all of that away so the allocator builds and runs with a plain `gcc` and `make`.

## How it works

The allocator maintains **9 segregated free lists**, one per power-of-2 block size:

| Index | Block size | Max user payload |
|------:|----------:|----------------:|
| 0 | 32 B | 28 B |
| 1 | 64 B | 60 B |
| 2 | 128 B | 124 B |
| 3 | 256 B | 252 B |
| 4 | 512 B | 508 B |
| 5 | 1 024 B | 1 020 B |
| 6 | 2 048 B | 2 044 B |
| 7 | 4 096 B | 4 092 B |

Each allocation prepends a 4-byte header storing the requested size, then returns a pointer to the payload. On `p2free`, the header is used to locate the correct free list and the block is reinserted in address order (which sets up for future coalescing).

When a free list runs dry, the allocator calls `fake_sbrk(4096)` to claim a fresh 4 096-byte page from a 16 MB static backing buffer, carves it into same-sized blocks, and adds them all to the list.

The intrusive doubly-linked list (`ps_list.h`) was written by Gabriel Parmer and is redistributed under the BSD 2-clause license.

## Build

```
make        # produces p2test (or p2test.exe on Windows)
make run    # build and run all tests
make clean
```

Requires GCC (or any C11-compatible compiler) and GNU make.

## Tests

Five levels ported from the original xv6 assignment, each run with a fresh allocator state:

| Level | What it checks |
|------:|----------------|
| 0 | Basic alloc + free; `allocated` returns to 0 |
| 1 | 1 000 000 tight alloc/free cycles of 12 bytes |
| 2 | 8 live allocations, out-of-order free, memory zeroed after free |
| 3 | Sweep every size from 12 to 4 091 bytes (step 7), no frees |
| 4 | Stress: 1 000 000 allocs of 12 B then 300 B; first 1 000 of each size accumulate, the rest reuse freed blocks |

## Porting notes

| xv6 original | Userspace replacement |
|---|---|
| `#include "types.h"`, `"user.h"` | Standard `<stddef.h>`, `<stdio.h>`, `<string.h>` |
| `sbrk(4096)` | `fake_sbrk(4096)` — pointer into a 16 MB static array |
| `printf(1, ...)` / `printf(2, ...)` | `printf(...)` / `fprintf(stderr, ...)` |
| `exit()` (no argument) | `return` from `main` |
| Minimum block: 16 B | **Minimum block: 32 B** — `sizeof(struct p2freelist_node)` is 24 bytes on 64-bit; a 16-byte block cannot hold the freelist metadata |
| Each test level was a separate process | `p2reset()` added to restore fresh state between levels |
