#define _GNU_SOURCE
#include "container.h"
#include "fs.h"
#include "cgroup.h"

#include <sched.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

container_t *container_create(char *rootfs, char **argv)
{
	container_t *c;

	c = malloc(sizeof *c);
	if (c == NULL) {
		perror("malloc container_t");
		return NULL;
	}

	/* zero first so mem_limit_bytes/pids_limit start at 0 (= unlimited)
	 * instead of garbage that would look like a real limit */
	memset(c, 0, sizeof *c);

	/* borrow main's pointers; main outlives every use of the container */
	c->rootfs = rootfs;
	c->argv = argv;
	c->hostname = "mybox";

	return c;
}

int container_child_fn(void *arg)
{
	container_t *c = arg;

	/* runs inside the cloned child, already in the new namespaces.
	 * order matters: UTS first, then pivot, then the mounts that are
	 * resolved relative to the new root, then exec. */

	/* UTS ns active, safe to rename without touching the host */
	if (fs_set_hostname(c->hostname) == -1)
		return 1;

	/* must precede the special mounts: /proc etc. are relative to new root */
	if (fs_pivot_root(c->rootfs) == -1)
		return 1;

	if (fs_mount_special() == -1)
		return 1;

	/* replaces this image; only returns on failure */
	execvp(c->argv[0], c->argv);
	perror("execvp");
	exit(1);

	return 1;  /* unreachable, but clone()'s fn must return int */
}

int container_run(container_t *c)
{
	char *stack;
	char *stack_top;
	pid_t pid;
	int status;
	int flags = CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD;

	/* clone needs its own stack (unlike fork) */
	stack = malloc(STACK_SIZE);
	if (stack == NULL) {
		perror("malloc clone stack");
		return -1;
	}

	/* stack grows down, so hand clone() the top end */
	stack_top = stack + STACK_SIZE;

	/* clone() before cgroup setup: cgroups are pid-namespace-agnostic, so
	 * cgroup.procs needs the host-visible pid, which is exactly what clone()
	 * returns to the parent — and it only exists once the child is created. */
	pid = clone(container_child_fn, stack_top, flags, c);
	if (pid == -1) {
		perror("clone");
		free(stack);
		return -1;
	}
	c->child_pid = pid;

	/* race note: the child may reach execvp before this setup lands. for v1
	 * that window is tolerable; a pipe handshake would close it cleanly. */
	if (cgroup_create(pid) == -1 ||
	    cgroup_add_pid(pid) == -1 ||
	    cgroup_set_memory_limit(pid, c->mem_limit_bytes) == -1 ||
	    cgroup_set_pids_limit(pid, c->pids_limit) == -1) {
		/* never let the child run unconstrained: kill, reap, clean up */
		fprintf(stderr, "cgroup setup failed, killing child %d\n", (int)pid);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		cgroup_destroy(pid);
		free(stack);
		return -1;
	}

	if (waitpid(pid, &status, 0) == -1) {
		perror("waitpid");
		cgroup_destroy(pid);
		free(stack);
		return -1;
	}

	/* rmdir only now: cgroup.procs is empty once the child is reaped,
	 * otherwise cgroup_destroy's rmdir hits EBUSY. */
	cgroup_destroy(pid);
	free(stack);

	if (WIFEXITED(status))
		return WEXITSTATUS(status);

	/* killed by signal: report it the shell way (128 + signo) */
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);

	return -1;
}

void container_destroy(container_t *c)
{
	if (c == NULL)
		return;

	/* no cgroup cleanup here: that happened in container_run after waitpid,
	 * since cgroup_destroy is tied to a run's pid, not the struct lifetime. */
	free(c);
}
