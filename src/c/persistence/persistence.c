#include <pebble.h>
#include <string.h>
#include "./persistence.h"

#define PERSIST_KEY_DATA_BASE 1
#define PERSIST_CHUNK_SIZE 200
#define PERSIST_CHUNK_COUNT 16 // 3200 bytes: room for the plugin text slots

// Stores the struct size alongside the data. Persisted blobs written by a
// build with a different EclipseData layout (including any release that
// predates this struct) are rejected instead of being misread.
#define PERSIST_KEY_SCHEMA 100
#define PERSIST_SCHEMA_VERSION 6

typedef char persist_capacity_check[
  (PERSIST_CHUNK_SIZE * PERSIST_CHUNK_COUNT >= (int)sizeof(EclipseData)) ? 1 : -1];

static uint32_t schema_signature(void) {
  return ((uint32_t)PERSIST_SCHEMA_VERSION << 16) | (uint32_t)sizeof(EclipseData);
}

bool persistence_save(const EclipseData *data) {
  const uint8_t *bytes = (const uint8_t *)data;
  size_t remaining = sizeof(*data);
  bool ok = true;

  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t chunk_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    int written = persist_write_data(PERSIST_KEY_DATA_BASE + i,
                                     bytes + (size_t)i * PERSIST_CHUNK_SIZE,
                                     chunk_len);
    if (written != (int)chunk_len) ok = false;
    remaining -= chunk_len;
  }
  // Drop chunks left over from an older, larger layout.
  for (int i = (int)((sizeof(*data) + PERSIST_CHUNK_SIZE - 1) / PERSIST_CHUNK_SIZE); i < PERSIST_CHUNK_COUNT; i++) {
    if (persist_exists(PERSIST_KEY_DATA_BASE + i)) persist_delete(PERSIST_KEY_DATA_BASE + i);
  }
  persist_write_int(PERSIST_KEY_SCHEMA, (int32_t)schema_signature());
  return ok && remaining == 0;
}

bool persistence_load(EclipseData *data) {
  memset(data, 0, sizeof(*data));
  if (!persist_exists(PERSIST_KEY_SCHEMA) ||
      (uint32_t)persist_read_int(PERSIST_KEY_SCHEMA) != schema_signature()) return false;
  size_t total = sizeof(*data);
  size_t remaining = total;
  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t expect_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    if (!persist_exists(PERSIST_KEY_DATA_BASE + i) ||
        persist_get_size(PERSIST_KEY_DATA_BASE + i) != (int)expect_len) return false;
    remaining -= expect_len;
  }
  uint8_t *bytes = (uint8_t *)data;
  remaining = total;
  for (int i = 0; i < PERSIST_CHUNK_COUNT && remaining > 0; i++) {
    size_t chunk_len = remaining < PERSIST_CHUNK_SIZE ? remaining : PERSIST_CHUNK_SIZE;
    int read = persist_read_data(PERSIST_KEY_DATA_BASE + i,
                                  bytes + (size_t)i * PERSIST_CHUNK_SIZE,
                                  chunk_len);
    if (read != (int)chunk_len) {
      memset(data, 0, sizeof(*data));
      return false;
    }
    remaining -= chunk_len;
  }
  return remaining == 0;
}
