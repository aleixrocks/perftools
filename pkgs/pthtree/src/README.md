# pthtree – Visualise a POSIX‑thread hierarchy in real time

`pthtree` is a lightweight LD\_PRELOAD interposer that captures **who‑spawned‑whom** in a multi‑threaded Linux process and prints an ASCII tree of:

* **Thread IDs (TIDs)**
* **Creator hierarchy** (parent → child)
* **Shared object** that holds each thread’s start routine
* **Nesting level**
* **Per‑thread CPU time** (seconds and % of total wall time)
* **Child counts** – direct & recursive

Example output:

```text
19241 (main) 0lvl 0.087s (1.2%) [2/6]
├── 19242 (libomp.so) 1lvl 3.213s (44.7%) [0/0]
└── 19243 (libomp.so) 1lvl 3.174s (44.1%) [2/2]
    ├── 19244 (libpotato.so) 2lvl 0.478s (6.6%) [0/0]
    └── 19345 (libbanana.so) 2lvl ?s (?) [0/0]
```

```
<tid> (<lib>) <nesting>lvl <cpu‑sec>s (<cpu‑%>) [<direct>/<total‑descendants>]
```

A `?` means the thread was still running when the program ended and its CPU time couldn’t be obtained.

---

## Build

```bash
# Build the shared library (requires gcc & pthread headers)
make            # or: gcc -shared -fPIC -o lib/libpthtree.so src/pthread_tree.c -ldl -pthread
```

The Makefile builds into `lib/` by default so the wrapper can locate it without extra flags.

---

## Quick start

```bash
# Run any program under pthtree (basenames only)
./bin/pthtree ./my_mt_program arg1 arg2

# Show full library paths
./bin/pthtree --verbose ./my_mt_program

# Give threads a chance to finish before dumping (see caveats)
./bin/pthtree --delay-dump ./my_mt_program
```

The wrapper is just sugar; you can also invoke manually:

```bash
PTHTREE_VERBOSE=1 LD_PRELOAD=/full/path/lib/libpthtree.so ./my_mt_program
```

---

## Wrapper (`pthtree`) options

| Option               | Env flag            | Description                                                                                                                                                                                                                                          |
| -------------------- | ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `-v`, `--verbose`    | `PTHTREE_VERBOSE=1` | Print the **full pathname** of every shared library instead of just its basename.                                                                                                                                                                    |
| `-d`, `--delay-dump` | `PTHTREE_DELAY=1`   | **Risky.** The interposer defers its final dump until the **last** thread exits. If the *main* thread ends first, its destructor may never run → **no tree printed**. Use when you see many `?` CPU‑time fields and want to wait for clean shutdown. |
| `-h`, `--help`       |  —                  | Show usage and option descriptions.                                                                                                                                                                                                                  |

Additional variable:

* `PTHTREE_LIB` – override the path to `libpthtree.so` that the wrapper injects.

---

## How it works

1. The interposer replaces `pthread_create`/`pthread_exit` and wraps every new
   thread in a trampoline that:

   * records **parent → child** relationship (siblings inside a process are not natively linked by the kernel);
   * times its own CPU usage with `CLOCK_THREAD_CPUTIME_ID` on normal return.
2. A destructor prints the collected tree; for threads still running, if -d was passed, it delays the dump
   until the last thread has exited.

---

## Caveats & Limitations

* Only tested on **Linux**.
* Threads created via non‑pthread APIs that bypass `pthread_create` (very rare) won’t be captured.
* `--delay-dump` greatly improves accuracy but risks suppressing output if the main thread exits early.
* The ASCII tree is printed to **stderr**; redirect it if you need to parse or log.
* Built with ChatGPT, be aware of bugs :-)

---

## License

MIT License – see `LICENSE` for details.

---

*Happy hacking & may your threads form tidy trees!*

