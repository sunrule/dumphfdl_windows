/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * systable_db.c - SQLite-backed storage for partially received System
 * Table PDU sets. See systable_db.h for the rationale.
 */
#include <stdio.h>
#include <string.h>

#include "systable_db.h"
#include "util.h"           // debug_print, UNUSED

#ifdef WITH_SQLITE

#include <sqlite3.h>

struct systable_db {
	sqlite3 *db;
	sqlite3_stmt *stmt_store;
	sqlite3_stmt *stmt_load;
	sqlite3_stmt *stmt_clear;
	sqlite3_stmt *stmt_count;
};

#define SYSTABLE_DB_SCHEMA \
	"CREATE TABLE IF NOT EXISTS systable_pdu (" \
	"  version INTEGER NOT NULL," \
	"  total_pdu_cnt INTEGER NOT NULL," \
	"  seq_num INTEGER NOT NULL," \
	"  data BLOB NOT NULL," \
	"  PRIMARY KEY (version, total_pdu_cnt, seq_num)" \
	");"

systable_db *systable_db_open(char const *path) {
	if(path == NULL) {
		return NULL;
	}
	NEW(systable_db, sdb);
	sdb->db = NULL;

	int32_t rc = sqlite3_open_v2(path, &sdb->db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "systable_db: can't open database %s: %s\n",
				path, sqlite3_errmsg(sdb->db));
		goto fail;
	}
	sqlite3_exec(sdb->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

	char *errmsg = NULL;
	rc = sqlite3_exec(sdb->db, SYSTABLE_DB_SCHEMA, NULL, NULL, &errmsg);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "systable_db: can't create table in %s: %s\n",
				path, errmsg ? errmsg : sqlite3_errmsg(sdb->db));
		sqlite3_free(errmsg);
		goto fail;
	}

	rc = sqlite3_prepare_v2(sdb->db,
			"INSERT OR REPLACE INTO systable_pdu (version, total_pdu_cnt, seq_num, data) "
			"VALUES (?, ?, ?, ?);", -1, &sdb->stmt_store, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "systable_db: prepare store failed: %s\n",
				sqlite3_errmsg(sdb->db));
		goto fail;
	}
	rc = sqlite3_prepare_v2(sdb->db,
			"SELECT seq_num, data FROM systable_pdu "
			"WHERE version = ? AND total_pdu_cnt = ? ORDER BY seq_num;",
			-1, &sdb->stmt_load, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "systable_db: prepare load failed: %s\n",
				sqlite3_errmsg(sdb->db));
		goto fail;
	}
	rc = sqlite3_prepare_v2(sdb->db,
			"DELETE FROM systable_pdu WHERE version = ? AND total_pdu_cnt = ?;",
			-1, &sdb->stmt_clear, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "systable_db: prepare clear failed: %s\n",
				sqlite3_errmsg(sdb->db));
		goto fail;
	}
	rc = sqlite3_prepare_v2(sdb->db,
			"SELECT COUNT(DISTINCT version || ':' || total_pdu_cnt) FROM systable_pdu;",
			-1, &sdb->stmt_count, NULL);
	if(rc != SQLITE_OK) {
		fprintf(stderr, "systable_db: prepare count failed: %s\n",
				sqlite3_errmsg(sdb->db));
		goto fail;
	}

	debug_print(D_CACHE, "systable_db: opened %s\n", path);
	return sdb;
fail:
	systable_db_close(sdb);
	return NULL;
}

void systable_db_close(systable_db *sdb) {
	if(sdb != NULL) {
		if(sdb->stmt_store != NULL) sqlite3_finalize(sdb->stmt_store);
		if(sdb->stmt_load != NULL) sqlite3_finalize(sdb->stmt_load);
		if(sdb->stmt_clear != NULL) sqlite3_finalize(sdb->stmt_clear);
		if(sdb->stmt_count != NULL) sqlite3_finalize(sdb->stmt_count);
		if(sdb->db != NULL) sqlite3_close(sdb->db);
		XFREE(sdb);
	}
}

bool systable_db_store_pdu(systable_db *sdb, int16_t version,
		uint8_t total_pdu_cnt, uint8_t seq_num,
		uint8_t const *buf, uint32_t len) {
	if(sdb == NULL || buf == NULL) {
		return false;
	}
	sqlite3_reset(sdb->stmt_store);
	sqlite3_bind_int(sdb->stmt_store, 1, version);
	sqlite3_bind_int(sdb->stmt_store, 2, total_pdu_cnt);
	sqlite3_bind_int(sdb->stmt_store, 3, seq_num);
	sqlite3_bind_blob(sdb->stmt_store, 4, buf, (int)len, SQLITE_STATIC);
	int32_t rc = sqlite3_step(sdb->stmt_store);
	if(rc != SQLITE_DONE) {
		debug_print(D_CACHE, "systable_db: store failed: %s\n",
				sqlite3_errmsg(sdb->db));
		return false;
	}
	return true;
}

int32_t systable_db_load_set(systable_db *sdb, int16_t version,
		uint8_t total_pdu_cnt, systable_db_pdu_callback cb, void *userdata) {
	if(sdb == NULL || cb == NULL) {
		return -1;
	}
	sqlite3_reset(sdb->stmt_load);
	sqlite3_bind_int(sdb->stmt_load, 1, version);
	sqlite3_bind_int(sdb->stmt_load, 2, total_pdu_cnt);

	int32_t count = 0;
	while(true) {
		int32_t rc = sqlite3_step(sdb->stmt_load);
		if(rc == SQLITE_ROW) {
			uint8_t seq_num = (uint8_t)sqlite3_column_int(sdb->stmt_load, 0);
			uint8_t const *data = (uint8_t const *)sqlite3_column_blob(sdb->stmt_load, 1);
			int data_len = sqlite3_column_bytes(sdb->stmt_load, 1);
			if(data != NULL && data_len > 0) {
				cb(version, seq_num, total_pdu_cnt,
						(uint8_t *)data, (uint32_t)data_len, userdata);
				count++;
			}
		} else if(rc == SQLITE_DONE) {
			break;
		} else {
			debug_print(D_CACHE, "systable_db: load failed: %s\n",
					sqlite3_errmsg(sdb->db));
			return -1;
		}
	}
	return count;
}

bool systable_db_clear_set(systable_db *sdb, int16_t version,
		uint8_t total_pdu_cnt) {
	if(sdb == NULL) {
		return false;
	}
	sqlite3_reset(sdb->stmt_clear);
	sqlite3_bind_int(sdb->stmt_clear, 1, version);
	sqlite3_bind_int(sdb->stmt_clear, 2, total_pdu_cnt);
	int32_t rc = sqlite3_step(sdb->stmt_clear);
	if(rc != SQLITE_DONE) {
		debug_print(D_CACHE, "systable_db: clear failed: %s\n",
				sqlite3_errmsg(sdb->db));
		return false;
	}
	return (sqlite3_changes(sdb->db) > 0);
}

int32_t systable_db_get_set_count(systable_db *sdb) {
	if(sdb == NULL) {
		return 0;
	}
	sqlite3_reset(sdb->stmt_count);
	int32_t rc = sqlite3_step(sdb->stmt_count);
	if(rc == SQLITE_ROW) {
		return sqlite3_column_int(sdb->stmt_count, 0);
	}
	return 0;
}

#else // !WITH_SQLITE

// Stubs for builds without SQLite support.

systable_db *systable_db_open(char const *path) {
	UNUSED(path);
	return NULL;
}

void systable_db_close(systable_db *sdb) {
	UNUSED(sdb);
}

bool systable_db_store_pdu(systable_db *sdb, int16_t version,
		uint8_t total_pdu_cnt, uint8_t seq_num,
		uint8_t const *buf, uint32_t len) {
	UNUSED(sdb); UNUSED(version); UNUSED(total_pdu_cnt); UNUSED(seq_num);
	UNUSED(buf); UNUSED(len);
	return false;
}

int32_t systable_db_load_set(systable_db *sdb, int16_t version,
		uint8_t total_pdu_cnt, systable_db_pdu_callback cb, void *userdata) {
	UNUSED(sdb); UNUSED(version); UNUSED(total_pdu_cnt); UNUSED(cb); UNUSED(userdata);
	return -1;
}

bool systable_db_clear_set(systable_db *sdb, int16_t version,
		uint8_t total_pdu_cnt) {
	UNUSED(sdb); UNUSED(version); UNUSED(total_pdu_cnt);
	return false;
}

int32_t systable_db_get_set_count(systable_db *sdb) {
	UNUSED(sdb);
	return 0;
}

#endif // WITH_SQLITE
