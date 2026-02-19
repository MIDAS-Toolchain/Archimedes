# ====================================================================
# PROJECT CONFIGURATION
# ====================================================================

# Compiler and Tools
CC = gcc

# Directories
SRC_DIR   = src
OBJ_DIR   = obj
INC_DIR   = include
BIN_DIR   = bin

#Flags
CINC = -I$(INC_DIR)/
LDLIBS = -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm

C_FLAGS  = -std=c99 -Wall -Wextra $(CINC)
BITMASK_C_FLAGS = $(C_FLAGS) -ggdb -lArchimedes


# ====================================================================
# BITMASK GENERATOR LIBRARY OBJECTS (Core C Files)
# ====================================================================

BITMASK_GEN_SRCS = Bitmask_stage.c

BITMASK_LIB_OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(BITMASK_GEN_SRCS))

BITMASK_OBJ = $(OBJ_DIR)/BitmaskGen.o

BITMASK_EXE_OBJS = $(BITMASK_LIB_OBJS) $(BITMASK_OBJ)


# ====================================================================
# PHONY TARGETS
# ====================================================================

.PHONY: all clean bear bearclean
all: $(BIN_DIR)/bitmask


# ====================================================================
# DIRECTORY & UTILITY RULES
# ====================================================================

# Ensure the directories exist before attempting to write files to them
$(BIN_DIR) $(OBJ_DIR):
	mkdir -p $@

$(INDEX_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@clear

bear:
	bear -- make
bearclean:
	rm compile_commands.json


# ====================================================================
# COMPILATION RULES
# ====================================================================

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) -c $< -o $@ $(BITMASK_C_FLAGS)


# ====================================================================
# LINKING RULES
# ====================================================================

$(BIN_DIR)/bitmask: $(BITMASK_EXE_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(BITMASK_C_FLAGS) $(LDLIBS)

