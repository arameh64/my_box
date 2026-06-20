#define _GNU_SOURCE
#include "cgroup.h"

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define CGROUP_ROOT "/sys/fs/cgroup/mybox"

/* Open .../mybox/<pid>/<filename> and write value with raw I/O.
 * Buffered stdio is avoided so a failed write surfaces immediately
 * instead of hiding in a flush; if this silently fails the child
 * joins no cgroup and limits never apply. Private to this file. */
static int write_cgroup_file(pid_t pid, const char *filename, const char *value)
{
	char path[PATH_MAX];
	int fd;
	size_t len;
	ssize_t n;

	if (snprintf(path, sizeof path, "%s/%d/%s",
		     CGROUP_ROOT, (int)pid, filename) >= (int)sizeof path) {
		fprintf(stderr, "cgroup path truncated: %s\n", filename);
		return -1;
	}

	fd = open(path, O_WRONLY);
	if (fd == -1) {
		perror(path);
		return -1;
	}

	len = strlen(value);
	n = write(fd, value, len);
	if (n == -1) {
		perror(path);
		close(fd);
		return -1;
	}
	if ((size_t)n != len) {
		fprintf(stderr, "short write to %s\n", path);
		close(fd);
		return -1;
	}

	if (close(fd) == -1) {
		perror(path);
		return -1;
	}

	return 0;
}

int cgroup_create(pid_t pid)
{
	char path[PATH_MAX];

	/* parent cgroup; only needs creating once but cheap to retry.
	 * EEXIST is fine here, anything else is fatal. */
	if (mkdir(CGROUP_ROOT, 0755) == -1 && errno != EEXIST) {
		perror(CGROUP_ROOT);
		return -1;
	}

	if (snprintf(path, sizeof path, "%s/%d",
		     CGROUP_ROOT, (int)pid) >= (int)sizeof path) {
		fprintf(stderr, "cgroup path truncated for pid %d\n", (int)pid);
		return -1;
	}

	/* per-pid cgroup must be fresh; EEXIST means pid collision or a
	 * stale dir from a crashed run, both real errors. */
	if (mkdir(path, 0755) == -1) {
		perror(path);
		return -1;
	}

	return 0;
}

int cgroup_add_pid(pid_t pid)
{
	char value[32];

	if (snprintf(value, sizeof value, "%d", (int)pid) >= (int)sizeof value) {
		fprintf(stderr, "pid string truncated\n");
		return -1;
	}

	return write_cgroup_file(pid, "cgroup.procs", value);
}

int cgroup_set_memory_limit(pid_t pid, long mem_limit_bytes)
{
	char value[32];

	if (mem_limit_bytes <= 0)
		return 0;

	if (snprintf(value, sizeof value, "%ld", mem_limit_bytes) >= (int)sizeof value) {
		fprintf(stderr, "memory limit string truncated\n");
		return -1;
	}

	return write_cgroup_file(pid, "memory.max", value);
}

int cgroup_set_pids_limit(pid_t pid, int pids_limit)
{
	char value[32];

	if (pids_limit <= 0)
		return 0;

	if (snprintf(value, sizeof value, "%d", pids_limit) >= (int)sizeof value) {
		fprintf(stderr, "pids limit string truncated\n");
		return -1;
	}

	return write_cgroup_file(pid, "pids.max", value);
}

int cgroup_destroy(pid_t pid)
{
	char path[PATH_MAX];

	if (snprintf(path, sizeof path, "%s/%d",
		     CGROUP_ROOT, (int)pid) >= (int)sizeof path) {
		fprintf(stderr, "cgroup path truncated for pid %d\n", (int)pid);
		return -1;
	}

	/* rmdir fails EBUSY if any pid still in cgroup.procs; container.c
	 * must call this only after the child exited and was reaped. */
	if (rmdir(path) == -1) {
		perror(path);
		return -1;
	}

	return 0;
}
