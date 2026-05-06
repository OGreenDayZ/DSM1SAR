TARGET = dsm1sar

SRC_DIR = src
TEST_DIR = test
INCLUDE_DIR = include
BUILD_DIR = build

# Compilateur et options
CC = gcc # Utiliser clang si on veut détecter + de warnings, sinon gcc
CFLAGS = -Wall -fsanitize=address -fsanitize=leak -g -I$(INCLUDE_DIR) -I$(SRC_DIR)

SRC_SRCS = $(shell find $(SRC_DIR) $(LIB_DIR) -name "*.c" 2>/dev/null)
SRC_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRC_SRCS:.c=.o)))

TEST_SRCS = $(shell find $(TEST_DIR) -name "*.c" 2>/dev/null)
TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(TEST_SRCS:.c=.o)))

# Générer la liste des exécutables de test (on retire build/main pour éviter les doublons avec dsm1sar)
TEST_BINS = $(filter-out $(BUILD_DIR)/main, $(TEST_OBJS:.o=))

SRC_DIRS = $(shell find $(SRC_DIR) $(LIB_DIR) $(TEST_DIR) -type d 2>/dev/null)
vpath %.c $(SRC_DIRS)

# Par défaut, définir LOG_LEVEL à DEBUG, et LOG_TRIGGER à LOG_UNIQ
LOG_LEVEL ?= NO_LOG
LOG_TRIGGER ?= LOG_UNIQ

all: $(TARGET)

$(TARGET): $(SRC_OBJS) $(BUILD_DIR)/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(SRC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

tests: $(TEST_BINS)

# Règle générique pour compiler les .o à partir des .c trouvés via VPATH
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Création du répertoire build/ si nécessaire
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Mode débogage NOLOG & UNIQ
debug: CFLAGS += -DDEBUG -DLOG_LEVEL=$(LOG_LEVEL) -DLOG_TRIGGER=$(LOG_TRIGGER)
debug: re

# Mode INFO & UNIQ
info: LOG_LEVEL = LOG_INFO
info: debug

# Mode WARNING & UNIQ
warning: LOG_LEVEL = LOG_WARNING
warning: debug

# Mode ERROR & UNIQ
error: LOG_LEVEL = LOG_ERROR
error: debug

# Mode débogage NOLOG & MAX
max_debug : LOG_TRIGGER = LOG_MAX
max_debug : debug

# Mode INFO & MAX
max_info: LOG_TRIGGER = LOG_MAX
max_info: info

# Mode WARNING & MAX
max_warning: LOG_TRIGGER = LOG_MAX
max_warning: warning

# Mode ERROR & MAX
max_error: LOG_TRIGGER = LOG_MAX
max_error: error

# Nettoyage complet
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Reconstruction complète
re: clean all

.PHONY: all clean re debug info warning error max_debug max_info max_warning max_error tests