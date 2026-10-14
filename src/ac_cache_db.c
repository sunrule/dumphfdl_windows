/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ac_cache_db.c - SQLite-backed persistent storage for the aircraft
 * ID -> ICAO address cache. See ac_cache_db.h for the rationale.
 *
 * Implementation notes:
 *   - The database is opened in read-write-create mode, so that decoding
 *     a read-only recording does not require write access to the input.
 *   - WAL journaling mode is enabled for better concurrency (the cache
 *     is accessed from the decoder thread only, but WAL also avoids
 *     creating -journal files next to the database).
 *   - All statements are prepared once at open time and reused.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ac_cache_db.h"
#include "util.h"           // debug_print, UNUSED

#ifdef WITH_SQLITE

#include <sqlite3.h>

struct ac_cache_db {
	sqlite3 *db;
	sqlite3_stmt *stmt_insert;
	sqlite3_stmt *stmt_lookup;
	sqlite3_stmt *stmt_delete;
	sqlite3_stmt *stmt_delete_by_icao;
	sqlite3_stmt *stmt_expire;
};

#define AC_CACHE_DB_SCHEMA \
	"CREATE TABLE IF NOT EXISTS ac_cache (" \
	"  freq INTEGER NOT NULL," \
	"  id INTEGER NOT NULL," \
	"  icao_address INTEGER NOT NULL," \
	"  created_time INTEGER NOT NULL," \
	"  PRIMARY KEY (freq, id)" \
	");"

ac_cache_db *ac_cache_db_open(char const *path) {
	if(path == NULL) {
		return NULL;
	}
	NEW(ac_cache_db, cache);
	cache->db = NULL;

	int32_t rc = sqlite3_open_v2(path, &cache->db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "ac_cache_db: can't open database %s: %s\n",
				path, sqlite3_errmsg(cache->db));
		goto fail;
	}
	// WAL mode: better concurrency, no -journal files next to the DB.
	// Ignore failures (e.g. on filesystems which don't support it) -
	// the default rollback journal works too, just slower.
	sqlite3_exec(cache->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

	char *errmsg = NULL;
	rc = sqlite3_exec(cache->db, AC_CACHE_DB_SCHEMA, NULL, NULL, &errmsg);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "ac_cache_db: can't create table in %s: %s\n",
				path, errmsg ? errmsg : sqlite3_errmsg(cache->db));
		sqlite3_free(errmsg);
		goto fail;
	}

	rc = sqlite3_prepare_v2(cache->db,
			"INSERT OR REPLACE INTO ac_cache (freq, id, icao_address, created_time) "
			"VALUES (?, ?, ?, ?);", -1, &cache->stmt_insert, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "ac_cache_db: prepare insert failed: %s\n",
				sqlite3_errmsg(cache->db));
		goto fail;
	}
	rc = sqlite3_prepare_v2(cache->db,
			"SELECT icao_address, created_time FROM ac_cache WHERE freq = ? AND id = ?;",
			-1, &cache->stmt_lookup, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "ac_cache_db: prepare lookup failed: %s\n",
				sqlite3_errmsg(cache->db));
		goto fail;
	}
	rc = sqlite3_prepare_v2(cache->db,
			"DELETE FROM ac_cache WHERE freq = ? AND id = ?;",
			-1, &cache->stmt_delete, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "ac_cache_db: prepare delete failed: %s\n",
				sqlite3_errmsg(cache->db));
		goto fail;
	}
	rc = sqlite3_prepare_v2(cache->db,
			"DELETE FROM ac_cache WHERE icao_address = ?;",
			-1, &cache->stmt_delete_by_icao, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "ac_cache_db: prepare delete_by_icao failed: %s\n",
				sqlite3_errmsg(cache->db));
		goto fail;
	}
	rc = sqlite3_prepare_v2(cache->db,
			"DELETE FROM ac_cache WHERE created_time < ?;",
			-1, &cache->stmt_expire, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "ac_cache_db: prepare expire failed: %s\n",
				sqlite3_errmsg(cache->db));
		goto fail;
	}

	debug_print(D_CACHE, "ac_cache_db: opened %s\n", path);
	return cache;
fail:
	ac_cache_db_close(cache);
	return NULL;
}

void ac_cache_db_close(ac_cache_db *db) {
	if(db != NULL) {
		if(db->stmt_insert != NULL) sqlite3_finalize(db->stmt_insert);
		if(db->stmt_lookup != NULL) sqlite3_finalize(db->stmt_lookup);
		if(db->stmt_delete != NULL) sqlite3_finalize(db->stmt_delete);
		if(db->stmt_delete_by_icao != NULL) sqlite3_finalize(db->stmt_delete_by_icao);
		if(db->stmt_expire != NULL) sqlite3_finalize(db->stmt_expire);
		if(db->db != NULL) sqlite3_close(db->db);
		XFREE(db);
	}
}

bool ac_cache_db_insert(ac_cache_db *db, int32_t freq, uint8_t id,
		uint32_t icao_address, time_t created_time) {
	if(db == NULL) {
		return false;
	}
	sqlite3_reset(db->stmt_insert);
	sqlite3_bind_int(db->stmt_insert, 1, freq);
	sqlite3_bind_int(db->stmt_insert, 2, id);
	sqlite3_bind_int64(db->stmt_insert, 3, (int64_t)icao_address);
	sqlite3_bind_int64(db->stmt_insert, 4, (int64_t)created_time);
	int32_t rc = sqlite3_step(db->stmt_insert);
	if(rc != SQLITE_DONE) {
		debug_print(D_CACHE, "ac_cache_db: insert failed: %s\n",
				sqlite3_errmsg(db->db));
		return false;
	}
	return true;
}

bool ac_cache_db_lookup(ac_cache_db *db, int32_t freq, uint8_t id,
		int32_t ttl, uint32_t *icao_address, time_t *created_time) {
	if(db == NULL || icao_address == NULL || created_time == NULL) {
		return false;
	}
	sqlite3_reset(db->stmt_lookup);
	sqlite3_bind_int(db->stmt_lookup, 1, freq);
	sqlite3_bind_int(db->stmt_lookup, 2, id);
	int32_t rc = sqlite3_step(db->stmt_lookup);
	if(rc == SQLITE_ROW) {
		*icao_address = (uint32_t)sqlite3_column_int64(db->stmt_lookup, 0);
		*created_time = (time_t)sqlite3_column_int64(db->stmt_lookup, 1);
		// TTL check: an entry older than ttl seconds is expired.
		if(ttl > 0 && (*created_time + (time_t)ttl) < time(NULL)) {
			debug_print(D_CACHE, "ac_cache_db: %hhu@%d: entry expired\n", id, freq);
			ac_cache_db_delete(db, freq, id);
			return false;
		}
		return true;
	}
	if(rc != SQLITE_DONE) {
		debug_print(D_CACHE, "ac_cache_db: lookup failed: %s\n",
				sqlite3_errmsg(db->db));
	}
	return false;
}

bool ac_cache_db_delete(ac_cache_db *db, int32_t freq, uint8_t id) {
	if(db == NULL) {
		return false;
	}
	sqlite3_reset(db->stmt_delete);
	sqlite3_bind_int(db->stmt_delete, 1, freq);
	sqlite3_bind_int(db->stmt_delete, 2, id);
	int32_t rc = sqlite3_step(db->stmt_delete);
	if(rc != SQLITE_DONE) {
		debug_print(D_CACHE, "ac_cache_db: delete failed: %s\n",
				sqlite3_errmsg(db->db));
		return false;
	}
	return (sqlite3_changes(db->db) > 0);
}

bool ac_cache_db_delete_by_icao(ac_cache_db *db, uint32_t icao_address) {
	if(db == NULL) {
		return false;
	}
	sqlite3_reset(db->stmt_delete_by_icao);
	sqlite3_bind_int64(db->stmt_delete_by_icao, 1, (int64_t)icao_address);
	int32_t rc = sqlite3_step(db->stmt_delete_by_icao);
	if(rc != SQLITE_DONE) {
		debug_print(D_CACHE, "ac_cache_db: delete_by_icao failed: %s\n",
				sqlite3_errmsg(db->db));
		return false;
	}
	return (sqlite3_changes(db->db) > 0);
}

int32_t ac_cache_db_expire(ac_cache_db *db, int32_t ttl) {
	if(db == NULL || ttl <= 0) {
		return 0;
	}
	sqlite3_reset(db->stmt_expire);
	sqlite3_bind_int64(db->stmt_expire, 1,
			(int64_t)(time(NULL) - (time_t)ttl));
	int32_t rc = sqlite3_step(db->stmt_expire);
	if(rc != SQLITE_DONE) {
		debug_print(D_CACHE, "ac_cache_db: expire failed: %s\n",
				sqlite3_errmsg(db->db));
		return -1;
	}
	return sqlite3_changes(db->db);
}

#else // !WITH_SQLITE

// Stubs for builds without SQLite support: the cache stays in-memory only.

ac_cache_db *ac_cache_db_open(char const *path) {
	UNUSED(path);
	return NULL;
}

void ac_cache_db_close(ac_cache_db *db) {
	UNUSED(db);
}

bool ac_cache_db_insert(ac_cache_db *db, int32_t freq, uint8_t id,
		uint32_t icao_address, time_t created_time) {
	UNUSED(db); UNUSED(freq); UNUSED(id); UNUSED(icao_address); UNUSED(created_time);
	return false;
}

bool ac_cache_db_lookup(ac_cache_db *db, int32_t freq, uint8_t id,
		int32_t ttl, uint32_t *icao_address, time_t *created_time) {
	UNUSED(db); UNUSED(freq); UNUSED(id); UNUSED(ttl);
	UNUSED(icao_address); UNUSED(created_time);
	return false;
}

bool ac_cache_db_delete(ac_cache_db *db, int32_t freq, uint8_t id) {
	UNUSED(db); UNUSED(freq); UNUSED(id);
	return false;
}

bool ac_cache_db_delete_by_icao(ac_cache_db *db, uint32_t icao_address) {
	UNUSED(db); UNUSED(icao_address);
	return false;
}

int32_t ac_cache_db_expire(ac_cache_db *db, int32_t ttl) {
	UNUSED(db); UNUSED(ttl);
	return 0;
}

#endif // WITH_SQLITE
