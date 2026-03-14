# Nom de l'exécutable
TARGET = DSM1SAR

# Répertoires
SRC_DIR = src
LIB_DIR = lib
TEST_DIR = test
BUILD_DIR = build

# Compilateur et options
CC = clang # Utiliser clang si on veut détecter + de warnings, sinon gcc
CFLAGS = -Wall -g # -I$(SRC_DIR) -I$(LIB_DIR) # visibilité

# Détection des OS
# UNAME_S := $(shell uname -s)
SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(LIB_DIR)/*.c) $(wildcard $(TEST_DIR)/*.c)

# LIBS = 
OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS:.c=.o)))

vpath %.c $(SRC_DIR) $(LIB_DIR) $(TEST_DIR)

# Par défaut, définir LOG_LEVEL à DEBUG, et LOG_TRIGGER à LOG_UNIQ
LOG_LEVEL ?= NO_LOG
LOG_TRIGGER ?= LOG_UNIQ

# Règle par défaut
all: $(TARGET)

# Génération de l'exécutable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

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

.PHONY: all clean re debug info warning error max_debug max_info max_warning max_error