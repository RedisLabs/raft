CONTRIB_DIR = .
TEST_DIR = ./tests
SRC_DIR = src
BUILD_DIR = src
BIN_DIR = bin
LIB = -I libs
INC = -I include
GCOV_CCFLAGS = -fprofile-arcs -ftest-coverage
SHELL  = /bin/bash
CFLAGS += -Iinclude -Werror -Werror=return-type -Werror=uninitialized -Wcast-align \
	  -Wno-pointer-sign -fno-omit-frame-pointer -fno-common -fsigned-char \
	  -Wunused-variable -g -O2 -fPIC

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
ASANFLAGS = -fsanitize=address
SHAREDFLAGS = -dynamiclib
SHAREDEXT = dylib
# We need to include the El Capitan specific /usr/includes, aargh
CFLAGS += -I/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk/usr/include/
CFLAGS += -I/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.12.sdk/usr/include
CFLAGS += $(ASANFLAGS)
CFLAGS += -Wno-nullability-completeness
else
SHAREDFLAGS = -shared
SHAREDEXT = so
endif

OBJECTS = src/raft_server.o \
	src/raft_server_properties.o \
	src/raft_node.o src/raft_log.o

TEST_HELPERS = $(TEST_DIR)/CuTest.o \
	$(TEST_DIR)/linked_list_queue.o \
	$(TEST_DIR)/mock_send_functions.o

TESTS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_TARGETS = $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/%,$(TESTS))

tests: CFLAGS += $(GCOV_CCFLAGS)

all: static shared

.PHONY: shared
shared: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(CFLAGS) -fPIC $(SHAREDFLAGS) -o libraft.$(SHAREDEXT)

.PHONY: static
static: $(OBJECTS)
	ar -r libraft.a $(OBJECTS)

.PHONY: tests
tests:$(TEST_HELPERS) $(TEST_TARGETS)
	gcov src/raft_server.c src/raft_node.c src/raft_log.c src/raft_server_properties.c

$(TEST_TARGETS):$(BIN_DIR)/%: $(OBJECTS)
	$(CC) $(CFLAGS) $(TEST_DIR)/$*.c $(LIB) $(INC) $^ -o $@ $(TEST_HELPERS)
	./$@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

.PHONY: test_helper
test_helper: $(TEST_HELPERS)
	$(CC) $(CFLAGS) -o $@

.PHONY: test_fuzzer
test_fuzzer:
	python tests/log_fuzzer.py

.PHONY: tests_full
tests_full:
	make clean
	make tests
	make test_fuzzer
	make test_virtraft

.PHONY: test_virtraft
test_virtraft:
	python3 tests/virtraft2.py --servers 5 -i 20000 --compaction_rate 50 --drop_rate 5 -P 10 --seed 1 -m 3 $(VIRTRAFT_OPTS)
	python3 tests/virtraft2.py --servers 7 -i 20000 --compaction_rate 50 --drop_rate 5 -P 10 --seed 1 -m 3 $(VIRTRAFT_OPTS)
	python3 tests/virtraft2.py --servers 5 -i 20000 --compaction_rate 50 --drop_rate 5 -P 10 --seed 2 -m 3 $(VIRTRAFT_OPTS)
	python3 tests/virtraft2.py --servers 5 -i 20000 --compaction_rate 50 --drop_rate 5 -P 10 --seed 3 -m 3 $(VIRTRAFT_OPTS)
	python3 tests/virtraft2.py --servers 5 -i 20000 --compaction_rate 50 --drop_rate 5 -P 10 --seed 4 -m 3 $(VIRTRAFT_OPTS)
	python3 tests/virtraft2.py --servers 5 -i 20000 --compaction_rate 50 --drop_rate 5 -P 10 --seed 5 -m 3 $(VIRTRAFT_OPTS)
	python3 tests/virtraft2.py --servers 5 -i 20000 --compaction_rate 50 --drop_rate 5 -P 10 --seed 6 -m 3 $(VIRTRAFT_OPTS)

.PHONY: amalgamation
amalgamation:
	./scripts/amalgamate.sh > raft.h

.PHONY: infer
infer: do_infer

.PHONY: do_infer
do_infer:
	make clean
	infer -- make

clean:
	@rm -f src/*.o bin/* src/*.gcda src/*.gcno *.gcno *.gcda *.gcov tests/*.o tests/*.gcda tests/*.gcno; \
	if [ -f "libraft.$(SHAREDEXT)" ]; then rm libraft.$(SHAREDEXT); fi;\
	if [ -f libraft.a ]; then rm libraft.a; fi;
