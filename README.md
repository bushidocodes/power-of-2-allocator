# Power-of-2 Allocator

A power-of-2 memory allocator extracted from an xv6 kernel assignment and ported to a standalone userspace C program, then hardened for use in real applications.

The original implementation lived inside a GWU operating systems course project ([hw6-19-power-of-2-malloc-bushidocodes](https://github.com/bushidocodes/hw6-19-power-of-2-malloc-bushidocodes)) and depended on xv6 kernel headers and the `sbrk` syscall stub. This repo strips all of that away and adds the features needed for production use.

## How it works

**Slab layer** — 8 segregated free lists, one per power-of-2 block size:

| Index | Block size | Max user payload |
|------:|----------:|----------------:|
| 0 | 32 B | 16 B |
| 1 | 64 B | 48 B |
| 2 | 128 B | 112 B |
| 3 | 256 B | 240 B |
| 4 | 512 B | 496 B |
| 5 | 1 024 B | 1 008 B |
| 6 | 2 048 B | 2 032 B |
| 7 | 4 096 B | 4 080 B |

Each allocation prepends a 16-byte header (sized to `__BIGGEST_ALIGNMENT__` on the platform) storing the user-requested size and an OS-allocation flag.  The pointer returned to the caller is always aligned to `__BIGGEST_ALIGNMENT__`.

When a free list runs dry, the allocator requests a fresh 4 096-byte page from the OS, carves it into same-sized blocks, and adds them to the list.  Freed blocks are re-inserted in address order (a foundation for future coalescing).

**Large layer** — allocations above `P2_SLAB_MAX` (4 080 bytes) bypass the slab and go straight to the OS via `mmap` (POSIX) or `VirtualAlloc` (Windows).  Those pages are returned to the OS immediately on `p2free`.

The intrusive doubly-linked list (`ps_list.h`) was written by Gabriel Parmer and is redistributed under the BSD 2-clause license.

## API

```c
void  *p2malloc(size_t size);
void   p2free(void *ptr);          /* NULL is a no-op */
void  *p2calloc(size_t n, size_t size);
void  *p2realloc(void *ptr, size_t new_size);
size_t p2allocated(void);          /* sum of live user-requested bytes */
size_t p2totmem(void);             /* total bytes obtained from the OS */
```

Define `P2_REPLACE_SYSTEM_MALLOC` before including `p2malloc.h` to redirect `malloc`/`free`/`calloc`/`realloc` to the p2 allocator, enabling use as a drop-in via a link-order trick or `LD_PRELOAD`.

## Build

```
make          # produces p2test / p2test.exe (integration test runner)
make test     # produces and runs test_p2malloc (38 Unity unit tests)
make run      # build and run the integration runner
make clean
```

Requires GCC (C11) and GNU make.  On POSIX, links `-lpthread`; on Windows, uses native `SRWLOCK` (no extra library).

## Tests

**Unit tests** (`make test`) — 38 tests covering:
- Invalid inputs (zero, overflow, `NULL` free)
- Allocation tracking (`p2allocated`, `p2totmem`)
- Alignment guarantee on every returned pointer
- Free behaviour (decrement tracking, first-word zeroing, totmem unchanged)
- Freelist reuse (no new OS pages after fill + drain)
- Exact slot counts per bucket (128 / 64 / 1 per page)
- Large allocations: success, tracking, OS page return, read/write
- `p2calloc`: zeroing, edge cases, overflow guard
- `p2realloc`: in-place same-bucket, cross-bucket copy, null/zero edge cases

**Integration tests** (`make run`) — 5 levels ported from the original xv6 assignment, each run with fresh allocator state via `p2reset()`.

## Changes from the xv6 original

| Area | xv6 original | This port |
|---|---|---|
| Backing store | Static 16 MB array (fake_sbrk) | OS pages on demand: `mmap` / `VirtualAlloc` |
| Large allocations | Returns NULL above 4 092 B | Direct OS allocation, returned on free |
| Alignment | 4-byte header → unaligned returns | 16-byte header → `__BIGGEST_ALIGNMENT__`-aligned |
| API | `p2malloc(int)` / `p2free` only | `size_t`, `p2calloc`, `p2realloc`, NULL-safe `p2free` |
| Thread safety | None | Per-freelist `SRWLOCK` / `pthread_mutex`; atomic counters |
| Ordered insert | Relies on BSS < heap address ordering | Explicit sentinel check; correct with arbitrary OS addresses |
| Min block size | 16 B (broken on 64-bit) | 32 B (fits `p2freelist_node` on 64-bit) |
| Node layout | `pow2` then `ps_list` | `ps_list` then `pow2` — keeps list pointers in the header region, away from user data |
