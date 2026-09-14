#include <pebble.h>
#include <string.h>
#include "persistence.h"

#define PERSIST_KEY_DATA_BASE 1
#define PERSIST_CHUNK_SIZE 200
#define PERSIST_CHUNK_COUNT 12

typedef char persist_capacity_check[
  (PERSIST_CHUNK_SIZE * PERSIST_CHUNK_COUNT >= (int)sizeof(EclipseData)) ? 1 : -1];

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
  return ok && remaining == 0;
}

bool persistence_load(EclipseData *data) {
  memset(data, 0, sizeof(*data));
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
