# mybox Implementation Plan

## Status

All source files exist but are empty. Docs and architecture are complete. Nothing compiles yet.

---

## Build Order

Implement in this order — each step depends on the previous.

```
1. container.h       (shared struct, no deps)
2. cgroup.h / cgroup.c
3. fs.h / fs.c
4. container.h / container.c
5. main.c
6. Makefile
```

---

## Step 1 — `src/container.h`

Define the central struct used by every module.

```c
typedef struct {
    char *rootfs;
    char *cmd;
    char **args;
    int   mem_limit;   // bytes, 0 = no limit
    pid_t pid;
} container_t;
```

Declare `container_run(container_t *c)` — returns 0 on success, -1 on error.

**Gate:** header compiles clean with `-Wall`.

---

## Step 2 — `src/cgroup.h` / `src/cgroup.c`

### Interface

```c
int  cgroup_create(container_t *c);   // mkdir + write limits + add pid
void cgroup_destroy(container_t *c);  // rmdir cgroup path
```

### Implementation details

- Cgroup path: `/sys/fs/cgroup/mybox/<pid>/`
- `cgroup_create()` sequence:
  1. `mkdir /sys/fs/cgroup/mybox/` (ignore EEXIST)
  2. `mkdir /sys/fs/cgroup/mybox/<pid>/`
  3. If `mem_limit > 0`: write to `memory.max`
  4. Write `"max"` to `pids.max` (or a fixed small number like `"32"`)
  5. Write container pid to `cgroup.procs`
- `cgroup_destroy()` sequence:
  1. Write `""` or `"0"` is not needed — kernel removes procs on exit
  2. `rmdir /sys/fs/cgroup/mybox/<pid>/`
  3. Attempt `rmdir /sys/fs/cgroup/mybox/` (ignore ENOTEMPTY — other containers may exist)

### Failure modes to handle

- `/sys/fs/cgroup/` not mounted (not cgroups v2 kernel) — fatal, print clear error
- `mkdir` fails for reason other than EEXIST — fatal
- Write to `memory.max` fails — fatal (kernel may not have memory controller enabled)

**Gate:** can create and destroy a cgroup path for a known PID without running a container.

---

## Step 3 — `src/fs.h` / `src/fs.c`

### Interface

```c
int fs_setup(const char *rootfs);
```

Called inside the child process after `clone()`. Returns 0 on success, -1 on error.

### Implementation: 7-step mount sequence

```
1. mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)
   — make root private so mounts don't propagate to host

2. mount(rootfs, rootfs, NULL, MS_BIND, NULL)
   — bind mount rootfs onto itself (pivot_root requires a mount point)

3. mkdir("<rootfs>/.old_root", 0700)
   — create landing dir for old root

4. pivot_root(rootfs, "<rootfs>/.old_root")
   — swap root mount

5. chdir("/")
   — update CWD to new root

6. mount("proc", "/proc", "proc", 0, NULL)
   — mount fresh /proc inside container

7. umount2("/.old_root", MNT_DETACH)
   rmdir("/.old_root")
   — sever host filesystem access
```

### Failure modes to handle

- `rootfs` path does not exist or is not a directory — fatal before clone
- `mount MS_PRIVATE` fails — fatal
- `pivot_root` fails — fatal (common cause: rootfs not on its own mount)
- `/proc` mount fails — warn but don't abort (some rootfs ship without /proc dir)
- `.old_root` already exists — treat as recoverable, reuse it

**Gate:** can pivot into an Alpine minirootfs and `ls /` shows Alpine layout, not host layout.

---

## Step 4 — `src/container.h` / `src/container.c`

### Interface

```c
int container_run(container_t *c);
```

### Implementation

```c
int container_run(container_t *c) {
    // 1. validate: rootfs exists, cmd non-null
    // 2. cgroup_create(c)  — before clone so pid is known? No: pid comes from clone.
    //    Solution: clone first, then write pid to cgroup.procs inside cgroup_create
    //    OR: create cgroup dir with parent pid, add child pid after clone
    //    Decision: clone first, capture pid, then cgroup_create(c) with c->pid set

    // 3. clone(child_fn, stack_top, CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD, c)
    //    child_fn receives container_t* via arg

    // 4. cgroup_create(c)  — now c->pid is set

    // 5. waitpid(c->pid, &status, 0)

    // 6. cgroup_destroy(c)

    // return WEXITSTATUS(status)
}
```

### child_fn (static, internal)

```c
static int child_fn(void *arg) {
    container_t *c = arg;
    fs_setup(c->rootfs);
    sethostname("mybox", 5);
    execvp(c->cmd, c->args);
    perror("execvp");   // only reached on exec failure
    return 1;
}
```

### Stack allocation for clone()

`clone()` requires a manually allocated stack. Allocate on heap:

```c
#define STACK_SIZE (1024 * 1024)   // 1MB
char *stack = malloc(STACK_SIZE);
char *stack_top = stack + STACK_SIZE;  // stack grows down
```

Free after `waitpid()`.

### Cgroup timing problem

`CLONE_NEWPID` means the child's PID in the parent namespace is returned by `clone()`. Write it to `cgroup.procs` immediately after `clone()` returns, before the child gets scheduled. This is a race but acceptable for v1 — no synchronization mechanism needed unless we add a pipe-based barrier later.

**Gate:** `mybox run /opt/alpine /bin/sh` drops into isolated shell, hostname shows `mybox`, `ps` shows only container processes, `cat /proc/meminfo` is visible.

---

## Step 5 — `src/main.c`

### Responsibilities

- Parse `argc/argv`
- Populate `container_t`
- Call `container_run()`
- Exit with container's exit code

### CLI spec

```
mybox run [--mem <size>] <rootfs> <cmd> [args...]
```

`--mem` accepts: `64m`, `512m`, `1g` (parse to bytes).

### Implementation

```c
int main(int argc, char **argv) {
    if (argc < 4) { usage(); exit(1); }
    if (strcmp(argv[1], "run") != 0) { usage(); exit(1); }

    container_t c = {0};
    int i = 2;

    if (strcmp(argv[i], "--mem") == 0) {
        c.mem_limit = parse_mem(argv[i+1]);
        i += 2;
    }

    c.rootfs = argv[i++];
    c.cmd    = argv[i];
    c.args   = &argv[i];

    return container_run(&c);
}
```

`parse_mem()`: strip trailing `m`/`g`, multiply by 1024^2 or 1024^3.

**Gate:** `--mem 64m` sets `memory.max` to `67108864` in the cgroup.

---

## Step 6 — `Makefile`

```makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g
SRC     = src/main.c src/container.c src/cgroup.c src/fs.c
OBJ     = $(SRC:.c=.o)
TARGET  = mybox

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: clean
```

**Gate:** `make` produces `mybox` binary with no warnings.

---

## Testing Sequence

Run these in order. Each must pass before moving to next.

| # | Command | Expected |
|---|---------|----------|
| 1 | `make` | Compiles clean, no warnings |
| 2 | `sudo ./mybox` | Prints usage |
| 3 | `sudo ./mybox run /opt/alpine /bin/hostname` | Prints `mybox` |
| 4 | `sudo ./mybox run /opt/alpine /bin/ps aux` | Shows only 2 processes (ps + shell) |
| 5 | `sudo ./mybox run /opt/alpine /bin/ls /` | Shows Alpine layout, not host |
| 6 | `sudo ./mybox run --mem 64m /opt/alpine /bin/sh` | Check `/sys/fs/cgroup/mybox/<pid>/memory.max` = `67108864` |
| 7 | After exit: `ls /sys/fs/cgroup/mybox/` | Cgroup directory removed |
| 8 | Run `cat /etc/hostname` inside container | Should not show host hostname |

---

## Known Gaps to Resolve During Implementation

1. **Cgroup v2 memory controller** — on some systems it's disabled by default. Check `/sys/fs/cgroup/memory.controller` or handle `ENOENT` on `memory.max` write gracefully with a clear error.

2. **`pivot_root` requires rootfs on separate mount** — the bind mount in step 2 of `fs_setup` handles this, but if rootfs is already a separate mountpoint the bind is redundant (harmless).

3. **`/proc` in rootfs** — Alpine minirootfs ships with `/proc` dir. If a custom rootfs doesn't have it, `mount("proc", ...)` fails. Create it if missing.

4. **Stack direction** — `stack + STACK_SIZE` assumes downward growth (x86/arm standard). Correct but worth a comment.

5. **Signal handling** — if parent receives SIGINT while child is running, cgroup_destroy won't be called. For v1, acceptable. Future: `atexit()` or signal handler.

6. **`--mem` flag placement** — parser assumes `--mem` comes before rootfs. Document this. Don't implement general flag parsing.

---

## Out of Scope for v1

- Network namespace / veth pair
- Overlay filesystem
- Image pull / registry
- User namespace remapping
- Multiple concurrent containers
- Daemon / state file
- seccomp / capabilities dropping
