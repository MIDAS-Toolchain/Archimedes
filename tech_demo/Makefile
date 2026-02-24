# ====================================================================
# Tech Demo - Builds the Archimedes tech demo
# ====================================================================

CC  = gcc
ECC = emcc

# Directories (relative to this Makefile)
ENGINE_SRC = ../src
ENGINE_INC = ../include
OBJ_DIR    = obj
BIN_DIR    = bin
INDEX_DIR  = index

OBJ_DIR_NATIVE = $(OBJ_DIR)/native
OBJ_DIR_EM     = $(OBJ_DIR)/em

# Flags
CINC    = -I$(ENGINE_INC)/
LDLIBS  = -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm
EFLAGS  = -s USE_SDL=2 -s USE_SDL_IMAGE=2 -s SDL2_IMAGE_FORMATS='["png"]' -s USE_SDL_MIXER=2 -s USE_SDL_TTF=2 -sALLOW_MEMORY_GROWTH

NATIVE_C_FLAGS  = -std=c99 -Wall -Wextra $(CINC) -ggdb
EMSCRIP_C_FLAGS = -std=c99 -Wall -Wextra $(CINC) $(EFLAGS)

# ====================================================================
# SOURCE FILES
# ====================================================================

ARCHIMEDES_SRCS = \
    a2DWorld.c \
    aAnimation.c \
    aAudio.c \
    aAUF.c \
    aAUFParser.c \
    aDeltaTime.c \
    aDraw.c \
    aImage.c \
    aInitialize.c \
    aInput.c \
    aLayout.c \
    aSpriteSheet.c \
    aText.c \
    aTileBitmask.c \
    aTimer.c \
    aUtils.c \
    aViewport.c \
    aWidgets.c

DEMO_SRCS = \
    player_actions.c \
    test_text.c \
    audio_test.c \
    enemy.c \
    hotbar.c \
    weapons.c \
    drops.c \
    pickups.c \
    game_audio.c \
    blood.c \
    game_director.c \
    collision.c \
    game_hud.c \
    xp.c \
    upgrades.c \
    fire_particles.c \
    stats.c

# ====================================================================
# OBJECT FILES
# ====================================================================

# Native: engine + demo compiled together
NATIVE_ENGINE_OBJS = $(patsubst %.c, $(OBJ_DIR_NATIVE)/%.o, $(ARCHIMEDES_SRCS))
NATIVE_DEMO_OBJS   = $(patsubst %.c, $(OBJ_DIR_NATIVE)/%.o, $(DEMO_SRCS))
NATIVE_MAIN_OBJ    = $(OBJ_DIR_NATIVE)/n_main.o

# Emscripten: demo objects link against static lib
EM_DEMO_OBJS = $(patsubst %.c, $(OBJ_DIR_EM)/%.o, $(DEMO_SRCS))
EM_MAIN_OBJ  = $(OBJ_DIR_EM)/em_main.o

# ====================================================================
# TARGETS
# ====================================================================

.PHONY: all native em clean

all: native
native: $(BIN_DIR)/native
em: $(INDEX_DIR)/index

# ====================================================================
# DIRECTORY RULES
# ====================================================================

$(BIN_DIR) $(OBJ_DIR_NATIVE) $(OBJ_DIR_EM):
	mkdir -p $@

$(INDEX_DIR):
	mkdir -p $@

# ====================================================================
# NATIVE COMPILATION
# ====================================================================

# Engine sources -> native objects
$(OBJ_DIR_NATIVE)/%.o: $(ENGINE_SRC)/%.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

# Demo sources -> native objects
$(OBJ_DIR_NATIVE)/%.o: %.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

# main.c gets a special rule (renamed to n_main.o, extra -I.)
$(OBJ_DIR_NATIVE)/n_main.o: main.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS) -I.

# ====================================================================
# EMSCRIPTEN COMPILATION
# ====================================================================

$(OBJ_DIR_EM)/%.o: %.c | $(OBJ_DIR_EM)
	$(ECC) -c $< -o $@ $(EMSCRIP_C_FLAGS)

$(OBJ_DIR_EM)/em_main.o: main.c | $(OBJ_DIR_EM)
	$(ECC) -c $< -o $@ $(EMSCRIP_C_FLAGS)

# ====================================================================
# LINKING
# ====================================================================

$(BIN_DIR)/native: $(NATIVE_ENGINE_OBJS) $(NATIVE_DEMO_OBJS) $(NATIVE_MAIN_OBJ) | $(BIN_DIR)
	$(CC) $^ -o $@ $(NATIVE_C_FLAGS) $(LDLIBS)

$(INDEX_DIR)/index: $(EM_MAIN_OBJ) $(EM_DEMO_OBJS) ../bin/libArchimedes.a | $(INDEX_DIR)
	$(ECC) $^ -s WASM=1 $(EFLAGS) --shell-file ../htmlTemplate/template.html --preload-file resources/@resources/ -o $@.html

# ====================================================================
# CLEAN
# ====================================================================

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
