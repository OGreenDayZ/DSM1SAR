TARGET = dsm1sar

SRC_DIR = src
TEST_DIR = test
INCLUDE_DIR = include
BUILD_DIR = build

ARGS =

CC = gcc
CFLAGS = -Wall -fsanitize=address,leak -fno-omit-frame-pointer -g -I$(INCLUDE_DIR) -I$(SRC_DIR) -I$(TEST_DIR)/util
SRC_SRCS = $(shell find $(SRC_DIR) $(LIB_DIR) -name "*.c" 2>/dev/null)
SRC_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRC_SRCS:.c=.o)))

# Séparer les .c de test avec main() des utilitaires sans main()
TEST_SRCS_WITH_MAIN = $(shell grep -rl "int main" $(TEST_DIR) --include="*.c" 2>/dev/null | grep -v "$(TEST_DIR)/main.c")
TEST_SRCS_NO_MAIN   = $(filter-out $(TEST_SRCS_WITH_MAIN) $(TEST_DIR)/main.c, $(shell find $(TEST_DIR) -name "*.c" 2>/dev/null))

TEST_OBJS_WITH_MAIN = $(addprefix $(BUILD_DIR)/, $(notdir $(TEST_SRCS_WITH_MAIN:.c=.o)))
TEST_UTIL_OBJS      = $(addprefix $(BUILD_DIR)/, $(notdir $(TEST_SRCS_NO_MAIN:.c=.o)))


TEST_BINS = $(TEST_OBJS_WITH_MAIN:.o=)

SRC_DIRS = $(shell find $(SRC_DIR) $(LIB_DIR) $(TEST_DIR) -type d 2>/dev/null)
vpath %.c $(SRC_DIRS)

LOG_LEVEL ?= NO_LOG
LOG_TRIGGER ?= LOG_UNIQ

all: $(TARGET)

$(TARGET): $(SRC_OBJS) $(BUILD_DIR)/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# Les binaires de test linkent aussi les utilitaires de test
$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(SRC_OBJS) $(TEST_UTIL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

tests: $(TEST_BINS)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

debug: CFLAGS += -DDEBUG -DLOG_LEVEL=$(LOG_LEVEL) -DLOG_TRIGGER=$(LOG_TRIGGER)
debug: re

info: LOG_LEVEL = LOG_INFO
info: debug

warning: LOG_LEVEL = LOG_WARNING
warning: debug

error: LOG_LEVEL = LOG_ERROR
error: debug

max_debug: LOG_TRIGGER = LOG_MAX
max_debug: debug

max_info: LOG_TRIGGER = LOG_MAX
max_info: info

max_warning: LOG_TRIGGER = LOG_MAX
max_warning: warning

max_error: LOG_TRIGGER = LOG_MAX
max_error: error

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

re: clean all

test-config: $(filter-out $(BUILD_DIR)/main.o, $(SRC_OBJS)) $(BUILD_DIR)/test_config.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/test_config $^ -Wl,--wrap=dsm_discover -Wl,--wrap=dsm_daemon_start -Wl,--wrap=pthread_create -Wl,--wrap=pthread_detach $(LIBS)
	@clear
	./$(BUILD_DIR)/test_config

test-simple:
	clear
	@echo "=== Exécution de test-simple ==="
	@./$(BUILD_DIR)/test_simple

test-sync:
	clear
	@echo "=== Exécution de test-sync ==="
	@./$(BUILD_DIR)/test_sync

test-integrity: 
	clear 
	@echo "=== Exécution de test-integrity ==="
	@./$(BUILD_DIR)/test_integrity

test-profiling: 
	clear 
	@echo "=== Exécution de test-profiling ==="
	@./$(BUILD_DIR)/test_profiling

test-false-sharing:
	clear 
	@echo "=== Exécution de test-false-sharing ==="
	@./$(BUILD_DIR)/test_false_sharing

test-matrix: 
	clear 
	@echo "=== Exécution de test-matrix ==="
	@./$(BUILD_DIR)/test_matrix $(ARGS)

help:
	@echo "Utilisation du projet DSM :"
	@echo "  make run ARGS='...'"
	@echo ""
	@echo "Arguments disponibles pour l'exécutable :"
	@echo "  -W<id> : Lance le processus en tant que Worker avec l'ID spécifié (ex: -W1)"
	@echo "  -M     : Lance le processus en tant que Manager"
	@echo "  -n <N> : Définit le nombre total de nœuds"
	@echo "  -m <M> : Définit le nombre de managers"
	@echo ""
	@echo "Exemples :"
	@echo "  make run ARGS='-W1 -n 4 -m 1'"
	@echo "  make run ARGS='-M -n 4 -m 1'"

.PHONY: all clean re debug info warning error max_debug max_info max_warning max_error tests