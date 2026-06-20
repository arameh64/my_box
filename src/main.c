#define _GNU_SOURCE
#include "container.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Parses argv: mybox run [--mem <limit>] [--pids <limit>] <rootfs> <cmd> [args...] */
static void print_usage(void);
static int parse_args(int argc, char **argv, container_t *c);

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: mybox run [--mem <limit>] [--pids <limit>] <rootfs> <cmd> [args...]\n"
		"Example: mybox run /opt/alpine /bin/sh\n"
		"  --mem   memory cap, accepts k/m/g suffix (e.g. 64m)\n"
		"  --pids  max processes (integer)\n");
}

/* "64m" -> bytes. strtol the digits, apply a k/m/g multiplier.
 * Returns -1 on any malformed input (negative, junk, unknown suffix). */
static long parse_mem(const char *s)
{
	char *end;
	long val;

	errno = 0;
	val = strtol(s, &end, 10);
	if (errno != 0 || end == s || val < 0)
		return -1;

	switch (*end) {
	case '\0':
		break;
	case 'k': case 'K':
		val *= 1024L;
		end++;
		break;
	case 'm': case 'M':
		val *= 1024L * 1024L;
		end++;
		break;
	case 'g': case 'G':
		val *= 1024L * 1024L * 1024L;
		end++;
		break;
	default:
		return -1;
	}

	/* nothing may follow the suffix */
	if (*end != '\0')
		return -1;

	return val;
}

static int parse_args(int argc, char **argv, container_t *c)
{
	int i;

	/* need at least: mybox run <rootfs> <cmd> */
	if (argc < 3) {
		print_usage();
		return -1;
	}

	/* only one subcommand in v1; check explicitly so adding "stop" etc.
	 * later doesn't reshape the parser */
	if (strcmp(argv[1], "run") != 0) {
		print_usage();
		return -1;
	}

	/* optional --flags before the positional args */
	i = 2;
	while (i < argc && strncmp(argv[i], "--", 2) == 0) {
		if (strcmp(argv[i], "--mem") == 0) {
			long bytes;

			if (i + 1 >= argc) {
				print_usage();
				return -1;
			}
			bytes = parse_mem(argv[i + 1]);
			if (bytes < 0) {
				fprintf(stderr, "mybox: bad --mem value: %s\n",
					argv[i + 1]);
				return -1;
			}
			c->mem_limit_bytes = bytes;
			i += 2;
		} else if (strcmp(argv[i], "--pids") == 0) {
			if (i + 1 >= argc) {
				print_usage();
				return -1;
			}
			c->pids_limit = atoi(argv[i + 1]);
			i += 2;
		} else {
			fprintf(stderr, "mybox: unknown flag: %s\n", argv[i]);
			print_usage();
			return -1;
		}
	}

	/* rootfs */
	if (i >= argc) {
		print_usage();
		return -1;
	}
	c->rootfs = argv[i];
	i++;

	/* command to exec */
	if (i >= argc) {
		print_usage();
		return -1;
	}

	/* point straight into the original argv: execvp gets
	 * cmd, arg1, ..., NULL with no copy (argv is NULL-terminated by the OS) */
	c->argv = &argv[i];

	return 0;
}

int main(int argc, char **argv)
{
	container_t c;

	/* namespaces and cgroups both need root; fail fast with a clear
	 * message instead of a confusing syscall error later */
	if (getuid() != 0) {
		fprintf(stderr, "mybox: must be run as root\n");
		return 1;
	}

	memset(&c, 0, sizeof c);

	if (parse_args(argc, argv, &c) == -1)
		return 1;

	/* propagate the container's own exit status as mybox's exit code,
	 * so `echo $?` reflects the command, not just "mybox ran fine" */
	return container_run(&c);
}
