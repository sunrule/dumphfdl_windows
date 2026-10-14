/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ac_cache_db.h - persistent storage backend for the aircraft ID -> ICAO
 * address cache (see ac_cache.c).
 *
 * The in-memory cache in ac_cache.c is volatile: it is destroyed when the
 * program (or the DLL decode call) exits. This backend stores the same
 * mappings in a SQLite database file, so that they survive across runs.
 *
 * Typical use case: decoding a series of MCR recordings. A Logon Confirm
 * seen while decoding recording #1 populates the cache; recordings #2..N
 * (which may contain position reports for the same aircraft, but no logon)
 * can then resolve the ICAO address from the database.
 *
 * All functions are no-ops (returning false/NULL) when built without
 * SQLite support.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef struct ac_cache_db ac_cache_db;

// Open (or create) the cache database at the given path.
// Creates the table if it does not exist yet.
// Returns NULL on error (message printed to stderr).
ac_cache_db *ac_cache_db_open(char const *path);

// Close the database. Safe to call with NULL.
void ac_cache_db_close(ac_cache_db *db);

// Insert or replace a mapping. Returns true on success.
bool ac_cache_db_insert(ac_cache_db *db, int32_t freq, uint8_t id,
		uint32_t icao_address, time_t created_time);

// Look up a mapping. Returns true and fills icao_address/created_time
// if a non-expired entry exists. Entries older than ttl seconds are
// treated as absent (and deleted).
bool ac_cache_db_lookup(ac_cache_db *db, int32_t freq, uint8_t id,
		int32_t ttl, uint32_t *icao_address, time_t *created_time);

// Delete the entry for the given (freq, id) key. Returns true if a row
// was deleted.
bool ac_cache_db_delete(ac_cache_db *db, int32_t freq, uint8_t id);

// Delete all entries for the given ICAO address (regardless of frequency).
// Returns true if at least one row was deleted.
bool ac_cache_db_delete_by_icao(ac_cache_db *db, uint32_t icao_address);

// Remove expired entries from the database. Returns the number of rows
// deleted, or -1 on error.
int32_t ac_cache_db_expire(ac_cache_db *db, int32_t ttl);
