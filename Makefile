# mybox — minimal container runtime
# ---------------------------------------------------------------------------
# Targets:
#   make            build the release binary (default)
#   make debug      build with -g -O0 -fsanitize=undefined and DEBUG defined
#   make run        build then run as root (sudo) — override ROOTFS/CMD/MEM/PIDS
#   make clean      remove build artifacts
#   make install    install to $(PREFIX)/bin (default /usr/local)
#   make uninstall  remove the installed binary
#   make compile_commands.json   regenerate clangd db (needs `bear`)
# ---------------------------------------------------------------------------

CC       ?= cc
BIN      := mybox

SRC_DIR   := src
BUILD_DIR := build

# auto-discover every .c under src/ so new modules need no edits here
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# warnings on, C11 + GNU extensions (clone, pivot_root need them).
# -MMD -MP emit per-object .d files so headers are tracked automatically.
CPPFLAGS ?=
CFLAGS   ?= -std=gnu11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
CFLAGS   += -MMD -MP
LDFLAGS  ?=
LDLIBS   ?=

# release vs debug flavor
RELEASE_FLAGS := -O2 -DNDEBUG
DEBUG_FLAGS   := -O0 -g3 -DDEBUG -fsanitize=address,undefined

# install location
PREFIX  ?= /usr/local
DESTDIR ?=

# run knobs — override on the command line, e.g.
#   make run ROOTFS=/opt/alpine CMD="/bin/sh" MEM=64m PIDS=64
ROOTFS ?= /opt/alpine
CMD    ?= /bin/sh
MEM    ?=
PIDS   ?=
RUNFLAGS := $(if $(MEM),--mem $(MEM)) $(if $(PIDS),--pids $(PIDS))

.PHONY: all release debug run clean install uninstall

all: release

release: CFLAGS += $(RELEASE_FLAGS)
release: $(BIN)

debug: CFLAGS += $(DEBUG_FLAGS)
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(BIN)

# link
$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# compile; build dir is an order-only prereq so timestamps don't force rebuilds
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# needs root for namespaces + cgroups; refuse if rootfs is missing
run: release
	@test -d "$(ROOTFS)" || { echo "mybox: ROOTFS '$(ROOTFS)' not found — set ROOTFS=<dir>"; exit 1; }
	sudo ./$(BIN) run $(RUNFLAGS) $(ROOTFS) $(CMD)

install: release
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)

clean:
	rm -rf $(BUILD_DIR) $(BIN)

compile_commands.json:
	bear -- $(MAKE) -B

# pull in auto-generated header dependencies
-include $(DEPS)
