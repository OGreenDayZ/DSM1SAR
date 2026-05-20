# TARGET = dsm1sar

# SRC_DIR = src
# TEST_DIR = test
# INCLUDE_DIR = include
# BUILD_DIR = build
# TEST_USER_ERROR_BIN = $(BUILD_DIR)/test-config
# TEST_USER_ERROR_SRC = $(TEST_DIR)/test-config.c

# # Compilateur et options
# CC = gcc # Utiliser clang si on veut détecter + de warnings, sinon gcc
# CFLAGS = -Wall -fsanitize=address,leak -fno-omit-frame-pointer -g -I$(INCLUDE_DIR) -I$(SRC_DIR)

# SRC_SRCS = $(shell find $(SRC_DIR) $(LIB_DIR) -name "*.c" 2>/dev/null)
# SRC_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRC_SRCS:.c=.o)))

# TEST_SRCS = $(shell find $(TEST_DIR) -name "*.c" 2>/dev/null)
# TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(TEST_SRCS:.c=.o)))

# # Générer la liste des exécutables de test (on retire build/main pour éviter les doublons avec dsm1sar)
# TEST_BINS = $(filter-out $(BUILD_DIR)/main, $(TEST_OBJS:.o=))

# SRC_DIRS = $(shell find $(SRC_DIR) $(LIB_DIR) $(TEST_DIR) -type d 2>/dev/null)
# SRC_WITHOUT_MAIN = $(filter-out $(BUILD_DIR)/main.o, $(SRC_OBJS))
# vpath %.c $(SRC_DIRS)

# # Par défaut, définir LOG_LEVEL à DEBUG, et LOG_TRIGGER à LOG_UNIQ
# LOG_LEVEL ?= NO_LOG
# LOG_TRIGGER ?= LOG_UNIQ

# all: $(TARGET)

# $(TARGET): $(SRC_OBJS) $(BUILD_DIR)/main.o
# 	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# $(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(SRC_OBJS)
# 	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# tests: $(TEST_BINS)

# # Règle générique pour compiler les .o à partir des .c trouvés via VPATH
# $(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
# 	$(CC) $(CFLAGS) -c $< -o $@

# # Création du répertoire build/ si nécessaire
# $(BUILD_DIR):
# 	mkdir -p $(BUILD_DIR)

# # Mode débogage NOLOG & UNIQ
# debug: CFLAGS += -DDEBUG -DLOG_LEVEL=$(LOG_LEVEL) -DLOG_TRIGGER=$(LOG_TRIGGER)
# debug: re

# # Mode INFO & UNIQ
# info: LOG_LEVEL = LOG_INFO
# info: debug

# # Mode WARNING & UNIQ
# warning: LOG_LEVEL = LOG_WARNING
# warning: debug

# # Mode ERROR & UNIQ
# error: LOG_LEVEL = LOG_ERROR
# error: debug

# # Mode débogage NOLOG & MAX
# max_debug : LOG_TRIGGER = LOG_MAX
# max_debug : debug

# # Mode INFO & MAX
# max_info: LOG_TRIGGER = LOG_MAX
# max_info: info

# # Mode WARNING & MAX
# max_warning: LOG_TRIGGER = LOG_MAX
# max_warning: warning

# # Mode ERROR & MAX
# max_error: LOG_TRIGGER = LOG_MAX
# max_error: error

# # Nettoyage complet
# clean:
# 	rm -rf $(BUILD_DIR) $(TARGET)

# # Reconstruction complète
# re: clean all

# test-config: $(SRC_WITHOUT_MAIN) $(BUILD_DIR)/test-config.o | $(BUILD_DIR)
# 	$(CC) $(CFLAGS) -o $(TEST_USER_ERROR_BIN) $^ \
# 		-Wl,--wrap=dsm_discover \
# 		-Wl,--wrap=dsm_daemon_start \
# 		-Wl,--wrap=pthread_create \
# 		-Wl,--wrap=pthread_detach \
# 		-lpthread
# 	@clear
# 	./$(TEST_USER_ERROR_BIN)

# .PHONY: all clean re debug info warning error max_debug max_info max_warning max_error tests test_user_error
TARGET = dsm1sar

SRC_DIR = src
TEST_DIR = test
INCLUDE_DIR = include
BUILD_DIR = build
TEST_USER_ERROR_BIN = $(BUILD_DIR)/test-config
TEST_USER_ERROR_SRC = $(TEST_DIR)/test-config.c

# --- CONFIGURATION DU COMPILATEUR ---
CC = gcc
CFLAGS = -Wall -fsanitize=address,leak -fno-omit-frame-pointer -g -I$(INCLUDE_DIR) -I$(SRC_DIR)
LDFLAGS = 
LIBS = -lpthread

# --- RECHERCHE DES SOURCES PRINCIPALES ---
SRC_SRCS = $(shell find $(SRC_DIR) -name "*.c" 2>/dev/null)
SRC_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRC_SRCS:.c=.o)))
SRC_WITHOUT_MAIN = $(filter-out $(BUILD_DIR)/main.o, $(SRC_OBJS))

# --- RECHERCHE ET SEPARATION DES TESTS ---
ALL_TEST_SRCS = $(shell find $(TEST_DIR) -name "*.c" 2>/dev/null)

# On isole test_functions.c pour ne pas en faire un binaire autonome
TEST_SHARED_SRCS = $(TEST_DIR)/test_functions.c
TEST_SHARED_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(TEST_SHARED_SRCS:.c=.o)))

# Les vrais exécutables (on exclut test_functions.c et test-config.c)
REAL_TEST_SRCS = $(filter-out $(TEST_SHARED_SRCS) $(TEST_USER_ERROR_SRC), $(ALL_TEST_SRCS))
REAL_TEST_OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(REAL_TEST_SRCS:.c=.o)))
TEST_BINS = $(filter-out $(BUILD_DIR)/main, $(REAL_TEST_OBJS:.o=))

# Configuration des dossiers pour VPATH
SRC_DIRS = $(shell find $(SRC_DIR) $(TEST_DIR) -type d 2>/dev/null)
vpath %.c $(SRC_DIRS)

# Gestion des logs par défaut
LOG_LEVEL ?= NO_LOG
LOG_TRIGGER ?= LOG_UNIQ

# --- REGLES DE COMPILATION ---

all: $(TARGET)

# Compilation du binaire principal
$(TARGET): $(SRC_OBJS) $(BUILD_DIR)/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# Règle pour créer chaque exécutable de test en liant l'utilitaire partagé
$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(SRC_OBJS) $(TEST_SHARED_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

tests: $(TEST_BINS)

# Règle générique de compilation des objets (.o)
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- OPTIONS DE DEBUG / LOGS ---
debug: CFLAGS += -DDEBUG -DLOG_LEVEL=$(LOG_LEVEL) -DLOG_TRIGGER=$(LOG_TRIGGER)
debug: re

info: LOG_LEVEL = LOG_INFO
info: debug

warning: LOG_LEVEL = LOG_WARNING
warning: debug

error: LOG_LEVEL = LOG_ERROR
error: debug

max_debug : LOG_TRIGGER = LOG_MAX
max_debug : debug

max_info: LOG_TRIGGER = LOG_MAX
max_info: info

max_warning: LOG_TRIGGER = LOG_MAX
max_warning: warning

max_error: LOG_TRIGGER = LOG_MAX
max_error: error

# --- NETTOYAGE ---
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

re: clean all

# Version simplifiée sur une seule ligne pour éviter le bug de pattern d'écharpage (\)
test-config: $(SRC_WITHOUT_MAIN) $(BUILD_DIR)/test-config.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(TEST_USER_ERROR_BIN) $^ -Wl,--wrap=dsm_discover -Wl,--wrap=dsm_daemon_start -Wl,--wrap=pthread_create -Wl,--wrap=pthread_detach $(LIBS)
	@clear
	./$(TEST_USER_ERROR_BIN)

.PHONY: all clean re debug info warning error max_debug max_info max_warning max_error tests test-config