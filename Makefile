# Executable arguments
args = 5980

# Compiler settings
CC      = gcc

build ?= debug

# CFLAGS = -Wall -Wextra -Werror -Iinclude -MMD -MP
CFLAGS = -Wall -Iinclude -MMD -MP

ifeq ($(build), release)
    CFLAGS += -g -O3
else
    # Debug-specific flags
    CFLAGS += -g -O0 -fno-omit-frame-pointer -fsanitize=address
endif

# Directories
OBJDIR  = build
SRCDIR  = src
TESTDIR = tests

ALL_SRC = $(wildcard $(SRCDIR)/*.c)

# Keep everything EXCEPT main.c and router.c for the core library
LIB_SRC = $(filter-out $(SRCDIR)/main.c $(SRCDIR)/router.c, $(ALL_SRC))
LIB_OBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(LIB_SRC))

APP_SRC = $(SRCDIR)/main.c
APP_OBJ = $(OBJDIR)/main.o
TARGET  = $(OBJDIR)/main

ROUTER_SRC = $(SRCDIR)/router.c
ROUTER_OBJ = $(OBJDIR)/router.o
ROUTER_TARGET = $(OBJDIR)/router

TEST_SRC = $(wildcard $(TESTDIR)/*.c)
TEST_OBJ = $(patsubst $(TESTDIR)/%.c,$(OBJDIR)/%.o,$(TEST_SRC))

ALL_OBJ = $(LIB_OBJ) $(APP_OBJ)

DEPS = $(ALL_OBJ:.o=.d) $(ROUTER_OBJ:.o=.d) $(TEST_OBJ:.o=.d)

all: $(TARGET)

# Main server executable
$(TARGET): $(ALL_OBJ)
	@echo "Linking $@"
	$(CC) $(CFLAGS) $(ALL_OBJ) -o $(TARGET)

# Router executable
router: $(ROUTER_TARGET)

$(ROUTER_TARGET): $(LIB_OBJ) $(ROUTER_OBJ)
	@echo "Linking $@"
	$(CC) $(CFLAGS) $(LIB_OBJ) $(ROUTER_OBJ) -o $(ROUTER_TARGET)

# Compile src/*.c -> build/*.o
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	@echo "Compiling source $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Build the test executable
TEST_TARGET = $(OBJDIR)/test_runner

$(TEST_TARGET): $(LIB_OBJ) $(TEST_OBJ)
	@echo "Linking $@"
	$(CC) $(CFLAGS) $^ -o $@

# Compile test files
$(OBJDIR)/%.o: tests/%.c | $(OBJDIR)
	@echo "Compiling test $<"
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -rf $(OBJDIR) \
	rm callgrind.out.* \
	rm database.txt \
	rm server.log

run: $(TARGET)
	./$(TARGET) $(args)

run-router: $(ROUTER_TARGET)
	./$(ROUTER_TARGET)

compdb: clean
	bear -- make all
	@echo "compile_commands.json generated for your LSP!"

format:
	clang-format -i $(ALL_SRC) $(APP_SRC) $(TEST_SRC) include/*.h

.PHONY: all clean run router run-router test compdb format

-include $(DEPS)
