/* Parses argv: mybox run [--mem <limit>] [--pids <limit>] <rootfs> <cmd> [args...] */
static void print_usage(void);
static int parse_args(int argc, char **argv, container_t *c);

int main(int argc, char **argv);