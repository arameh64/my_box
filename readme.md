# mybox

A minimal Linux container runtime built on raw kernel primitives.

Runs a process in an isolated environment using PID, UTS, and mount namespaces, a pivot_root filesystem, and cgroups v2 resource limits. No Docker, no runc, no external dependencies.

---

## Requirements

- Linux kernel 5.2+ (cgroups v2)
- Root privileges
- A rootfs directory (Alpine minirootfs recommended)

---

## Build

```sh
make
```

---

## Usage

```sh
mybox run <rootfs> <cmd> [args...]
mybox run --mem 64m <rootfs> <cmd> [args...]
```

Example:

```sh
mybox run /opt/alpine /bin/sh
```

---

## What it does

- Isolates the process in its own PID, UTS, and mount namespaces
- Pivots the root filesystem into the provided rootfs
- Sets the container hostname to `mybox`
- Applies memory and PID limits via cgroups v2
- Cleans up fully on exit

## What it does not do

- No network isolation
- No image layering or registry
- No daemon or persistent state
- Single container at a time

---

## Project structure

```
mybox/
├── src/
│   ├── main.c
│   ├── container.c / container.h
│   ├── cgroup.c    / cgroup.h
│   └── fs.c        / fs.h
├── architecture.md
├── Makefile
└── README.md
```

See `architecture.md` for design decisions, call flow, and implementation details.