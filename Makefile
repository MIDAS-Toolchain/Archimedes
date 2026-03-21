# ====================================================================
# PROJECT CONFIGURATION
# ====================================================================

# Compiler and Tools
CC = gcc
ECC = emcc
EMAR = emar rcs

# Directories
SRC_DIR   = src
OBJ_DIR   = obj
INC_DIR   = include
BIN_DIR   = bin
TEST_DIR  = test
JSON_DIR  = json
EDITOR_DIR      = tools/WidgetEditor/src
EDITOR_INC_DIR  = tools/WidgetEditor/include
BITMASK_DIR     = tools/BitmaskGenerator/src
BITMASK_INC_DIR = tools/BitmaskGenerator/include

# Object Directories (Separated for different build types)
OBJ_DIR_NATIVE  = obj/native
OBJ_DIR_EDITOR  = obj/editor
OBJ_DIR_BITMASK = obj/bitmask
OBJ_DIR_SHARED  = obj/shared
OBJ_DIR_EM      = obj/em

#Flags
CINC = -I$(INC_DIR)/
EDINC = -I$(EDITOR_INC_DIR)/
BMINC = -I$(BITMASK_INC_DIR)/
LDLIBS = -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm
EFLAGS = -s USE_SDL=2 -s USE_SDL_IMAGE=2 -s SDL2_IMAGE_FORMATS='["png"]' -s USE_SDL_MIXER=2 -s USE_SDL_TTF=2 -sALLOW_MEMORY_GROWTH

C_FLAGS  = -std=c99 -Wall -Wextra $(CINC)
ED_FLAGS = -std=c99 -Wall -Wextra $(EDINC) #editor flags
BM_FLAGS = -std=c99 -Wall -Wextra $(BMINC) #bitmask flags
NATIVE_C_FLAGS  = $(C_FLAGS) -ggdb
SHARED_C_FLAGS  = $(C_FLAGS) -fPIC -pedantic
EDITOR_C_FLAGS  = $(ED_FLAGS) -ggdb -lArchimedes
BITMASK_C_FLAGS = $(BM_FLAGS) -ggdb -lArchimedes
EMSCRIP_C_FLAGS = $(C_FLAGS) $(EFLAGS)


# ====================================================================
# ARCHIMEDES LIBRARY OBJECTS (Core C Files)
# ====================================================================

ARCHIMEDES_SRCS = \
    a2DWorld.c \
    aAnimation.c \
    aAudio.c \
    aAUF.c \
    aAUFParser.c \
		aCamera2D.c \
    aDeltaTime.c \
    aDraw.c \
    aImage.c \
    aInitialize.c \
    aInput.c \
	  aLayout.c\
	  aSpriteSheet.c\
    aText.c \
    aTile.c \
    aTileBitmask.c \
    aTimer.c \
	  aUtils.c \
	  aViewport.c \
    aWidgets.c

WIDGET_EIDTOR_SRCS = WE_stage.c

BITMASK_GEN_SRCS = Bitmask_stage.c

SHARED_OBJS = $(patsubst %.c, $(OBJ_DIR_SHARED)/%.o, $(ARCHIMEDES_SRCS))
EMCC_OBJS = $(patsubst %.c, $(OBJ_DIR_EM)/%.o, $(ARCHIMEDES_SRCS))
NATIVE_LIB_OBJS = $(patsubst %.c, $(OBJ_DIR_NATIVE)/%.o, $(ARCHIMEDES_SRCS))
EIDTOR_LIB_OBJS = $(patsubst %.c, $(OBJ_DIR_EDITOR)/%.o, $(WIDGET_EIDTOR_SRCS))
BITMASK_LIB_OBJS = $(patsubst %.c, $(OBJ_DIR_BITMASK)/%.o, $(BITMASK_GEN_SRCS))

TEST_WID_OBJ = $(OBJ_DIR_NATIVE)/test_widgets.o
EDITOR_OBJ = $(OBJ_DIR_EDITOR)/WidgetEditor.o
BITMASK_OBJ = $(OBJ_DIR_BITMASK)/BitmaskGen.o

TEST_EXE_OBJS = $(NATIVE_LIB_OBJS) $(TEST_WID_OBJ)
EDITOR_EXE_OBJS = $(EDITOR_LIB_OBJS) $(EDITOR_OBJ)
BITMASK_EXE_OBJS = $(BITMASK_LIB_OBJS) $(BITMASK_OBJ)


# ====================================================================
# PHONY TARGETS
# ====================================================================

.PHONY: all shared editor bitmask EMARCH test clean install uninstall ainstall auninstall updateHeader bear bearclean verify
all: shared
shared: $(BIN_DIR)/libArchimedes.so
test:$(BIN_DIR)/test
editor:$(BIN_DIR)/editor
bitmask:$(BIN_DIR)/bitmask

# Emscripten Targets

EMARCH: $(BIN_DIR)/libArchimedes.a


# ====================================================================
# DIRECTORY & UTILITY RULES
# ====================================================================

# Ensure the directories exist before attempting to write files to them
$(BIN_DIR) $(OBJ_DIR_NATIVE) $(OBJ_DIR_SHARED) $(OBJ_DIR_EDITOR) $(OBJ_DIR_BITMASK) $(OBJ_DIR_EM):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@clear

bear:
	bear -- make

bearclean:
	rm compile_commands.json

install: $(BIN_DIR)/libArchimedes.so
	sudo cp $< /usr/lib/
	sudo cp $(INC_DIR)/Archimedes.h /usr/include/

uninstall:
	sudo rm /usr/lib/libArchimedes.so
	sudo rm /usr/include/Archimedes.h


# ====================================================================
# COMPILATION RULES
# ====================================================================

$(OBJ_DIR_SHARED)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR_SHARED)
	$(CC) -c $< -o $@ $(SHARED_C_FLAGS)

$(OBJ_DIR_NATIVE)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

$(OBJ_DIR_EDITOR)/%.o: $(EDITOR_DIR)/%.c | $(OBJ_DIR_EDITOR)
	$(CC) -c $< -o $@ $(EDITOR_C_FLAGS)

$(OBJ_DIR_BITMASK)/%.o: $(BITMASK_DIR)/%.c | $(OBJ_DIR_BITMASK)
	$(CC) -c $< -o $@ $(BITMASK_C_FLAGS)

$(OBJ_DIR_NATIVE)/test_widgets.o: $(TEST_DIR)/test_widgets.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

# ====================================================================
# COMPILATION RULES (Emscripten - ECC)
# ====================================================================

$(OBJ_DIR_EM)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR_EM)
	$(ECC) -c $< -o $@ $(EMSCRIP_C_FLAGS)

# ====================================================================
# LINKING RULES
# ====================================================================

# Target: Shared Library (.so)
$(BIN_DIR)/libArchimedes.so: $(SHARED_OBJS) | $(BIN_DIR)
	$(CC) -shared $^ -o $@ $(LDLIBS)

$(BIN_DIR)/test: $(TEST_EXE_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(NATIVE_C_FLAGS) $(LDLIBS)

$(BIN_DIR)/editor: $(EDITOR_EXE_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(EDITOR_C_FLAGS) $(LDLIBS)

$(BIN_DIR)/bitmask: $(BITMASK_EXE_OBJS) | $(BIN_DIR)
	$(CC) $^ -o $@ $(BITMASK_C_FLAGS) $(LDLIBS)

$(BIN_DIR)/libArchimedes.a: $(EMCC_OBJS) | $(BIN_DIR)
	$(EMAR) $@ $^
