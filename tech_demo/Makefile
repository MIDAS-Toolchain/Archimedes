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
CINC    = -I$(ENGINE_INC)/ -Isrc -Isrc/systems -Isrc/enemies -Isrc/enemies/mimic -I.
LDLIBS  = -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm
EFLAGS  = -s USE_SDL=2 -s USE_SDL_IMAGE=2 -s SDL2_IMAGE_FORMATS='["png"]' -s USE_SDL_MIXER=2 -s USE_SDL_TTF=2 -sALLOW_MEMORY_GROWTH

NATIVE_C_FLAGS  = -std=c99 -Wall -Wextra $(CINC) -ggdb
EMSCRIP_C_FLAGS = -std=c99 -Wall -Wextra $(CINC) $(EFLAGS)

# ====================================================================
# SOURCE FILES
# ====================================================================

SRC_SRCS = \
    src/player_actions.c \
    src/hotbar.c \
    src/weapons.c \
    src/drops.c \
    src/pickups.c \
    src/game_audio.c \
    src/blood.c \
    src/game_hud.c \
    src/xp.c \
    src/upgrades.c \
    src/fire_particles.c \
    src/menu.c \
    src/main_menu.c \
    src/achievements_menu.c \
    src/weapons_menu.c \
    src/enemy_types_menu.c \
    src/upgrades_menu.c

ENEMY_SRCS = \
    src/enemies/enemy.c \
    src/enemies/beholder.c \
    src/enemies/shaman.c \
    src/enemies/snake.c \
    src/enemies/mimic/mimic.c \
    src/enemies/mimic/mimic_wand.c \
    src/enemies/mimic/mimic_spin.c \
    src/enemies/mimic/mimic_bomb.c \
    src/enemies/mimic/mimic_chain.c \
    src/enemies/mimic/mimic_turret.c \
    src/enemies/mimic/mimic_trail.c \
    src/enemies/mimic/mimic_orbit.c \
    src/enemies/mimic/mimic_scythe.c

SYS_SRCS = \
    src/systems/collision.c \
    src/systems/game_director.c \
    src/systems/stats.c \
    src/systems/progress.c \
    src/systems/save_data.c \
    src/systems/achievements.c

DEMO_SRCS = $(SRC_SRCS) $(ENEMY_SRCS) $(SYS_SRCS)

# ====================================================================
# OBJECT FILES
# ====================================================================

# Native: demo objects, engine linked as shared library
NATIVE_DEMO_OBJS   = $(patsubst %.c, $(OBJ_DIR_NATIVE)/%.o, $(notdir $(DEMO_SRCS)))
NATIVE_MAIN_OBJ    = $(OBJ_DIR_NATIVE)/n_main.o

# Emscripten: demo objects link against static lib
EM_DEMO_OBJS = $(patsubst %.c, $(OBJ_DIR_EM)/%.o, $(notdir $(DEMO_SRCS)))
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

# src/ sources -> native objects
$(OBJ_DIR_NATIVE)/%.o: src/%.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

# src/enemies/ sources -> native objects
$(OBJ_DIR_NATIVE)/%.o: src/enemies/%.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

# src/enemies/mimic/ sources -> native objects
$(OBJ_DIR_NATIVE)/%.o: src/enemies/mimic/%.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

# src/systems/ sources -> native objects
$(OBJ_DIR_NATIVE)/%.o: src/systems/%.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

# main.c stays at root
$(OBJ_DIR_NATIVE)/n_main.o: main.c | $(OBJ_DIR_NATIVE)
	$(CC) -c $< -o $@ $(NATIVE_C_FLAGS)

# ====================================================================
# EMSCRIPTEN COMPILATION
# ====================================================================

$(OBJ_DIR_EM)/%.o: src/%.c | $(OBJ_DIR_EM)
	$(ECC) -c $< -o $@ $(EMSCRIP_C_FLAGS)

$(OBJ_DIR_EM)/%.o: src/enemies/%.c | $(OBJ_DIR_EM)
	$(ECC) -c $< -o $@ $(EMSCRIP_C_FLAGS)

$(OBJ_DIR_EM)/%.o: src/enemies/mimic/%.c | $(OBJ_DIR_EM)
	$(ECC) -c $< -o $@ $(EMSCRIP_C_FLAGS)

$(OBJ_DIR_EM)/%.o: src/systems/%.c | $(OBJ_DIR_EM)
	$(ECC) -c $< -o $@ $(EMSCRIP_C_FLAGS)

$(OBJ_DIR_EM)/em_main.o: main.c | $(OBJ_DIR_EM)
	$(ECC) -c $< -o $@ $(EMSCRIP_C_FLAGS)

# ====================================================================
# LINKING
# ====================================================================

$(BIN_DIR)/native: $(NATIVE_DEMO_OBJS) $(NATIVE_MAIN_OBJ) | $(BIN_DIR)
	$(CC) $^ -o $@ $(NATIVE_C_FLAGS) $(LDLIBS) -L../bin -lArchimedes -Wl,-rpath,'$$ORIGIN/../../bin'

$(INDEX_DIR)/index: $(EM_MAIN_OBJ) $(EM_DEMO_OBJS) ../bin/libArchimedes.a | $(INDEX_DIR)
	$(ECC) $^ -s WASM=1 $(EFLAGS) --shell-file ../htmlTemplate/template.html --preload-file resources/@resources/ -o $@.html

# ====================================================================
# CLEAN
# ====================================================================

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
