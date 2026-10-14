/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <time.h>
#include "cache.h"              // struct cache_entry

#define AC_CACHE_TTL_DEFAULT 3600L
#define AC_CACHE_EXPIRATION_INTERVAL 309L

typedef struct ac_cache ac_cache;

struct ac_cache_entry {
	char *callsign;
	uint32_t icao_address;
};

// db_path: optional path to a SQLite database file for persistent
// storage of the mappings across program runs. NULL = in-memory
// cache only (no persistence).
ac_cache *ac_cache_create(char const *db_path);
void ac_cache_entry_create(ac_cache *cache, int32_t freq,
		uint8_t id, uint32_t icao_address);
bool ac_cache_entry_delete(ac_cache *cache, int32_t freq,
		uint32_t icao_address);
struct ac_cache_entry *ac_cache_entry_lookup(ac_cache *cache,
		int32_t freq, uint8_t id);
void ac_cache_destroy(ac_cache *cache);
