#ifndef CGROUP_H
#define CGROUP_H

#include <sys/types.h>

/* Creates /sys/fs/cgroup/mybox/<pid>/ */
int cgroup_create(pid_t pid);

/* Writes pid into cgroup.procs to join the child to the cgroup */
int cgroup_add_pid(pid_t pid);

/* Writes memory.max if mem_limit_bytes > 0 */
int cgroup_set_memory_limit(pid_t pid, long mem_limit_bytes);

/* Writes pids.max if pids_limit > 0 */
int cgroup_set_pids_limit(pid_t pid, int pids_limit);

/* Removes the cgroup directory on container exit */
int cgroup_destroy(pid_t pid);

#endif