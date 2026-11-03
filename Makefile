CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
INCLUDES = -Iinclude

SRCS = $(shell find src -name '*.c')
CORE_SRCS = $(filter-out src/cli/main.c, $(SRCS))

BUILD_DIR = .build

.PHONY: all build test sanitize clean run

all: build

build: $(BUILD_DIR)/os_sim

$(BUILD_DIR)/os_sim: $(SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) -o $@

test: $(BUILD_DIR)/test_queue $(BUILD_DIR)/test_memory $(BUILD_DIR)/test_scheduler $(BUILD_DIR)/test_interpreter $(BUILD_DIR)/test_simulation build
	@echo "=== Running C Unit Tests ==="
	@$(BUILD_DIR)/test_queue
	@$(BUILD_DIR)/test_memory
	@$(BUILD_DIR)/test_scheduler
	@$(BUILD_DIR)/test_interpreter
	@echo "=== Running C Integration Tests ==="
	@$(BUILD_DIR)/test_simulation
	@echo "=== Running JSON Interface Tests ==="
	@$(BUILD_DIR)/os_sim --scheduler RR --quantum 2 --programs programs --process "1:Program 1.txt:0" --process "2:Program_2.txt:1" --process "3:Program_3.txt:4" > $(BUILD_DIR)/sim_output.json
	@python3 -c "import json; d = json.load(open('$(BUILD_DIR)/sim_output.json')); assert len(d['outputs']) > 0; assert len(d['snapshots']) > 10; print('✓ JSON CLI trace verified (', len(d['snapshots']), 'snapshots )')"
	@echo "=== All Tests Passed Successfully ==="

$(BUILD_DIR)/test_queue: tests/unit/test_queue.c src/queue/queue.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(BUILD_DIR)/test_memory: tests/unit/test_memory.c src/memory/os_memory.c src/queue/queue.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(BUILD_DIR)/test_scheduler: tests/unit/test_scheduler.c src/scheduler/scheduler.c src/queue/queue.c src/memory/os_memory.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(BUILD_DIR)/test_interpreter: tests/unit/test_interpreter.c $(CORE_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

$(BUILD_DIR)/test_simulation: tests/integration/test_simulation.c $(CORE_SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@

sanitize: CFLAGS += -fsanitize=address,undefined -g
sanitize: clean test
	@echo "=== Sanitizer checks passed with zero leaks/UB ==="

clean:
	rm -rf $(BUILD_DIR) *.log
