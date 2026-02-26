#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "save_data.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// ---------------------------------------------------------------------------
// In-memory key-value store
// ---------------------------------------------------------------------------

#define MAX_ENTRIES 128
#define MAX_KEY_LEN 32
#define MAX_VAL_LEN 32

typedef struct {
  char key[MAX_KEY_LEN];
  char val[MAX_VAL_LEN];
} SaveEntry_t;

static SaveEntry_t entries[MAX_ENTRIES];
static int entry_count = 0;

static SaveEntry_t* find_entry(const char* key)
{
  for (int i = 0; i < entry_count; i++) {
    if (strcmp(entries[i].key, key) == 0)
      return &entries[i];
  }
  return NULL;
}

static SaveEntry_t* find_or_create(const char* key)
{
  SaveEntry_t* e = find_entry(key);
  if (e) return e;
  if (entry_count >= MAX_ENTRIES) return NULL;
  e = &entries[entry_count++];
  snprintf(e->key, MAX_KEY_LEN, "%s", key);
  e->val[0] = '\0';
  return e;
}

// ---------------------------------------------------------------------------
// Platform: Emscripten (localStorage)
// ---------------------------------------------------------------------------

#ifdef __EMSCRIPTEN__

EM_JS(void, js_save_set, (const char* key, const char* value), {
  localStorage.setItem("archimedes_" + UTF8ToString(key), UTF8ToString(value));
});

EM_JS(int, js_save_get, (const char* key, char* buf, int buf_len), {
  var val = localStorage.getItem("archimedes_" + UTF8ToString(key));
  if (val === null) return 0;
  stringToUTF8(val, buf, buf_len);
  return 1;
});

void save_data_init(void)
{
  entry_count = 0;
}

void save_data_set_int(const char* key, int value)
{
  SaveEntry_t* e = find_or_create(key);
  if (!e) return;
  snprintf(e->val, MAX_VAL_LEN, "%d", value);
  js_save_set(key, e->val);
}

void save_data_set_float(const char* key, float value)
{
  SaveEntry_t* e = find_or_create(key);
  if (!e) return;
  snprintf(e->val, MAX_VAL_LEN, "%f", value);
  js_save_set(key, e->val);
}

int save_data_get_int(const char* key, int default_val)
{
  char buf[MAX_VAL_LEN];
  if (!js_save_get(key, buf, MAX_VAL_LEN)) return default_val;
  return atoi(buf);
}

float save_data_get_float(const char* key, float default_val)
{
  char buf[MAX_VAL_LEN];
  if (!js_save_get(key, buf, MAX_VAL_LEN)) return default_val;
  return (float)atof(buf);
}

void save_data_flush(void)
{
  /* no-op: writes go to localStorage immediately */
}

EM_JS(void, js_save_clear, (void), {
  var keys = [];
  for (var i = 0; i < localStorage.length; i++) {
    var k = localStorage.key(i);
    if (k && k.startsWith("archimedes_")) keys.push(k);
  }
  for (var j = 0; j < keys.length; j++) localStorage.removeItem(keys[j]);
});

void save_data_clear_all(void)
{
  entry_count = 0;
  memset(entries, 0, sizeof(entries));
  js_save_clear();
}

EM_JS(long, js_save_get_size, (void), {
  var total = 0;
  for (var i = 0; i < localStorage.length; i++) {
    var k = localStorage.key(i);
    if (k && k.startsWith("archimedes_")) {
      total += k.length * 2;
      var v = localStorage.getItem(k);
      if (v) total += v.length * 2;
    }
  }
  return total;
});

long save_data_get_size(void)
{
  return js_save_get_size();
}

// ---------------------------------------------------------------------------
// Platform: Native (save.dat file)
// ---------------------------------------------------------------------------

#else

#define SAVE_FILE "save.dat"

void save_data_init(void)
{
  entry_count = 0;

  FILE* f = fopen(SAVE_FILE, "r");
  if (!f) return;

  char line[MAX_KEY_LEN + MAX_VAL_LEN + 4];
  while (fgets(line, sizeof(line), f)) {
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    char* val = eq + 1;

    /* strip trailing newline */
    char* nl = strchr(val, '\n');
    if (nl) *nl = '\0';

    SaveEntry_t* e = find_or_create(line);
    if (e) snprintf(e->val, MAX_VAL_LEN, "%s", val);
  }
  fclose(f);
}

void save_data_set_int(const char* key, int value)
{
  SaveEntry_t* e = find_or_create(key);
  if (!e) return;
  snprintf(e->val, MAX_VAL_LEN, "%d", value);
}

void save_data_set_float(const char* key, float value)
{
  SaveEntry_t* e = find_or_create(key);
  if (!e) return;
  snprintf(e->val, MAX_VAL_LEN, "%f", value);
}

int save_data_get_int(const char* key, int default_val)
{
  SaveEntry_t* e = find_entry(key);
  if (!e) return default_val;
  return atoi(e->val);
}

float save_data_get_float(const char* key, float default_val)
{
  SaveEntry_t* e = find_entry(key);
  if (!e) return default_val;
  return (float)atof(e->val);
}

void save_data_flush(void)
{
  FILE* f = fopen(SAVE_FILE, "w");
  if (!f) return;
  for (int i = 0; i < entry_count; i++) {
    fprintf(f, "%s=%s\n", entries[i].key, entries[i].val);
  }
  fclose(f);
}

void save_data_clear_all(void)
{
  entry_count = 0;
  memset(entries, 0, sizeof(entries));
  /* overwrite save file with empty content */
  FILE* f = fopen(SAVE_FILE, "w");
  if (f) fclose(f);
}

long save_data_get_size(void)
{
  FILE* f = fopen(SAVE_FILE, "r");
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fclose(f);
  return size;
}

#endif
