# mybox Architecture

## Overview

mybox is a minimal Linux container runtime built on raw kernel primitives. It provides process isolation using namespaces, filesystem isolation via pivot_root, and resource limiting through cgroups v2. The goal is a working `mybox run <cmd>` that isolates a process from the host with no external dependencies.

---

## Data Model

A single struct drives the entire lifecycle:

```c
typedef struct {
    char *rootfs;      // path to alpine rootfs
    char *cmd;         // command to run inside the container
    char **args;       // arguments to that command
    int   mem_limit;   // memory limit in bytes, 0 = no limit
    pid_t pid;         // filled in after clone()
} container_t;
```

---

## Call Flow

```
main()
  └── parse_args()              fills container_t from CLI input
  └── container_run()
        ├── cgroup_create()     creates cgroup, writes resource limits
        ├── clone(child_fn,     CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD)
        │     └── child_fn()
        │           ├── fs_setup()        pivots root into rootfs
        │           ├── sethostname()     sets hostname to "mybox"
        │           └── execvp(cmd)       executes the target command
        └── waitpid()           blocks until container process exits
        └── cgroup_destroy()    removes cgroup and cleans up
```

---

## Design Decisions

### clone() over fork() + unshare()

`fork()` creates a child process that inherits all namespaces from the parent — same PID namespace, same filesystem view, same UTS. It cannot create isolated namespaces on its own because it is implemented internally as `clone()` with a fixed set of flags.

Calling `clone()` directly allows passing namespace flags explicitly, so the child process is born already isolated rather than inheriting the parent's context and detaching afterward.

The alternative — `fork()` followed by `unshare()` inside the child — works but introduces a window where the child exists in the parent's namespaces before detaching. `clone()` with flags eliminates that window and is the cleaner single-call approach.

### cgroups v2 over v1

cgroups v2 exposes a unified hierarchy under `/sys/fs/cgroup/`. Resource limits are applied by writing to files in that hierarchy directly — no library required, no legacy per-subsystem mount points. This keeps the implementation simple and forward-compatible since cgroups v1 is being phased out in modern kernels.

### Alpine Linux as rootfs base

Alpine's minirootfs is approximately 3MB. It provides a complete enough filesystem (shell, basic utilities, package manager) to meaningfully demonstrate container isolation without managing a large image. The user supplies the rootfs path — mybox does not bundle or fetch it.

### pivot_root over chroot

`chroot()` changes the root directory for a process but does not fully isolate it from the host filesystem — a process with sufficient privileges can escape a chroot jail. `pivot_root()` replaces the root mount of the current mount namespace entirely, making the old root unreachable without an explicit unmount step. This is the approach real container runtimes use and is the correct choice when combined with a mount namespace.

---

## Namespace Breakdown

| Namespace | Flag | Purpose |
|-----------|------|---------|
| PID | `CLONE_NEWPID` | Container gets its own process tree. The first process inside is PID 1. Host processes are invisible. |
| UTS | `CLONE_NEWUTS` | Container gets its own hostname and domain name, set independently of the host. |
| Mount | `CLONE_NEWNS` | Container gets its own filesystem view. Mounts inside do not propagate to the host. |
| Network | not included | Out of scope for v1. Would require a veth pair and bridge setup. |
| User | not included | User namespace remapping adds complexity. Running as root inside the container maps to root on the host in v1. |

---

## Filesystem Isolation

The mount sequence inside the container after `clone()`:

1. Remount root as private to prevent propagation to host: `mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)`
2. Bind mount the rootfs path onto itself: `mount(rootfs, rootfs, NULL, MS_BIND, NULL)`
3. Create a `.old_root` directory inside the rootfs
4. Call `pivot_root(rootfs, old_root_path)` to switch the root mount
5. `chdir("/")` to update the working directory
6. Mount `/proc` inside the container: `mount("proc", "/proc", "proc", 0, NULL)`
7. Unmount and remove `.old_root` to sever access to the host filesystem

---

## Cgroup Layout

Each container gets its own cgroup under:

```
/sys/fs/cgroup/mybox/<pid>/
```

Files written during setup:

| File | Purpose |
|------|---------|
| `cgroup.procs` | Add the container PID to this cgroup |
| `memory.max` | Set memory limit in bytes |
| `pids.max` | Limit number of processes inside the container |

On container exit, `cgroup_destroy()` removes the directory. The kernel requires `cgroup.procs` to be empty before removal, which is guaranteed once the container process has exited.

---

## Lifecycle

```
mybox run /bin/sh
      |
      | parse_args()
      v
  container_t populated
      |
      | cgroup_create()
      v
  /sys/fs/cgroup/mybox/<pid>/ created, limits written
      |
      | clone()
      |---------> child_fn()
      |                 |
      |                 | fs_setup()       pivot into Alpine rootfs
      |                 | sethostname()    set hostname to "mybox"
      |                 | execvp()         exec target command
      |                 v
      |             [container running]
      |                 |
      |                 | process exits
      v                 v
  waitpid() returns
      |
      | cgroup_destroy()
      v
  cleanup complete, mybox exits
```

---

## Known Limitations

- No network isolation. The container shares the host network stack.
- No image layering. A flat rootfs directory is required, no overlay filesystem.
- No rootfs bundled. The user must provide a valid rootfs path.
- Single container at a time. No daemon, no container registry, no state tracking.
- Root only. Running mybox requires root privileges on the host.
- No user namespace remapping. Root inside the container is root on the host.

---

## Snowball Candidates

The following are natural next steps if the project continues beyond v1, in order of increasing complexity:

1. **Network namespace + veth pair** — give the container its own network interface, connect it to the host via a virtual ethernet pair and a bridge
2. **Overlay filesystem** — layer a writable upper dir over a read-only base image so the rootfs is not mutated
3. **Image format** — a minimal tar-based image format with a manifest, enabling `mybox pull` and `mybox images`
4. **User namespace remapping** — map container root to an unprivileged host UID for better host security
5. **Multiple concurrent containers** — a simple state file tracking running container PIDs and their cgroups