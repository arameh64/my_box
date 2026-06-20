#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

int fs_pivot_root(const char *new_root)
{
	char buf[PATH_MAX];

	if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
		perror("mount MS_PRIVATE");
		return -1;
	}

	if (mount(new_root, new_root, NULL, MS_BIND | MS_REC, NULL) == -1) {
		perror("mount bind new_root");
		return -1;
	}

	if (snprintf(buf, sizeof buf, "%s/.old_root", new_root) >= (int)sizeof buf) {
		fprintf(stderr, "old_root path truncated\n");
		return -1;
	}

	if (mkdir(buf, 0777) == -1) {
		perror("mkdir .old_root");
		return -1;
	}

	if (syscall(SYS_pivot_root, new_root, buf) == -1) {
		perror("pivot_root");
		return -1;
	}

	if (chdir("/") == -1) {
		perror("chdir /");
		return -1;
	}

	if (umount2("/.old_root", MNT_DETACH) == -1) {
		perror("umount2 .old_root");
		return -1;
	}

	if (rmdir("/.old_root") == -1) {
		perror("rmdir .old_root");
		return -1;
	}

	return 0;
}

int fs_mount_special(void)
{
	/* fresh procfs scoped to the new PID namespace; must be after
	 * pivot_root and after entering the PID ns, else it shows host PIDs */
	if (mount("proc", "/proc", "proc", 0, NULL) == -1) {
		perror("mount proc");
		return -1;
	}

	/* /sys for cgroup path reads if the container needs them */
	if (mount("sysfs", "/sys", "sysfs", 0, NULL) == -1) {
		perror("mount sysfs");
		return -1;
	}

	/* minimal /dev: plain tmpfs, no real device nodes yet (snowball) */
	if (mount("tmpfs", "/dev", "tmpfs", 0, NULL) == -1) {
		perror("mount tmpfs /dev");
		return -1;
	}

	/* devpts so /bin/sh gets a working pty (v1) */
	if (mkdir("/dev/pts", 0755) == -1) {
		perror("mkdir /dev/pts");
		return -1;
	}

	if (mount("devpts", "/dev/pts", "devpts", 0, NULL) == -1) {
		perror("mount devpts");
		return -1;
	}

	return 0;
}

int fs_set_hostname(const char *hostname)
{
	/* only safe because the child is in a new UTS namespace (CLONE_NEWUTS
	 * in the clone() call in container.c); without it this renames the host */
	if (sethostname(hostname, strlen(hostname)) == -1) {
		perror("sethostname");
		return -1;
	}

	return 0;
}
