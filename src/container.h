#ifndef CONTAINER_H
#define CONTAINER_H

#include <sys/types.h>

#define STACK_SIZE (1024 * 1024)  /* 1MB stack for cloned child */

typedef struct {
    char *rootfs;          /* path to rootfs directory */
    char **argv;           /* command + args to exec inside container */
    char *hostname;         /* defaults to "mybox" */
    long mem_limit_bytes;   /* 0 = unlimited */
    int pids_limit;         /* 0 = unlimited */
    pid_t child_pid;        /* filled in after clone() */
} container_t;

/* Allocates and zero-initializes a container_t */
container_t *container_create(char *rootfs, char **argv);

/* Entry point run inside the cloned child (matches clone() fn signature) */
int container_child_fn(void *arg);

/* clone()s the child into new namespaces, sets up cgroup, waits for exit.
 * Returns child exit status, or -1 on error. */
int container_run(container_t *c);

/* Cleans up cgroup dir, frees container_t */
void container_destroy(container_t *c);

#endif