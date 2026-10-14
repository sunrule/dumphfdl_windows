/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * systable_db.h - persistent storage for partially received System Table
 * PDU sets.
 *
 * The HFDL System Table is transmitted as a set of PDUs (typically a few
 * of them). All PDUs of a set must be received before the table can be
 * decoded. When decoding a recording, the set is often incomplete at the
 * end of the file - the remaining PDUs arrive in the next recording.
 *
 * This backend stores the received raw PDUs in a SQLite database, so that
 * a system table can be assembled across multiple decoding runs (of
 * multiple MCR files), not only within a single continuous SDR session.
 *
 * Storage model: one row per received PDU, keyed by
 * (version, total_pdu_cnt, seq_num). When a complete set has been decoded,
 * its rows are removed from the database (systable_db_clear_set()).
 *
 * All functions are no-ops when built without SQLite support.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct systable_db systable_db;

// Open (or create) the PDU storage database at the given path.
// Returns NULL on error.
systable_db *systable_db_open(char const *path);

// Close the database. Safe to call with NULL.
void systable_db_close(systable_db *db);

// Store a received PDU (INSERT OR REPLACE).
// Returns true on success.
bool systable_db_store_pdu(systable_db *db, int16_t version,
		uint8_t total_pdu_cnt, uint8_t seq_num,
		uint8_t const *buf, uint32_t len);

// Load all stored PDUs of a given PDU set (identified by version and
// total_pdu_cnt). Calls the callback for each stored PDU, in ascending
// seq_num order. Returns the number of PDUs loaded, or -1 on error.
//
// The callback signature mirrors systable_store_pdu() in systable.h,
// so the loaded PDUs can be fed straight back into the in-memory
// PDU set:
//   void cb(int16_t version, uint8_t idx, uint8_t pdu_set_len,
//           uint8_t *buf, uint32_t len, void *userdata)
typedef void (*systable_db_pdu_callback)(int16_t version, uint8_t idx,
		uint8_t pdu_set_len, uint8_t *buf, uint32_t len, void *userdata);

int32_t systable_db_load_set(systable_db *db, int16_t version,
		uint8_t total_pdu_cnt, systable_db_pdu_callback cb, void *userdata);

// Remove all stored PDUs of the given PDU set (called after the set has
// been successfully decoded). Returns true if any rows were deleted.
bool systable_db_clear_set(systable_db *db, int16_t version,
		uint8_t total_pdu_cnt);

// Returns the number of distinct (version, total_pdu_cnt) sets currently
// stored in the database (0 if none or on error). Useful for diagnostics.
int32_t systable_db_get_set_count(systable_db *db);
