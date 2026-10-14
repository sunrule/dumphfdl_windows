/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hfdl_api.h - exported C API of the dumphfdl DLL.
 *
 * This API is designed to be called from 32-bit Windows applications
 * (e.g. Borland Delphi 7). All exported functions use the stdcall
 * calling convention. Strings are passed as NUL-terminated ANSI
 * (PAnsiChar) byte strings, because Delphi 7 is not Unicode-aware.
 *
 * Threading model:
 *   - HFDL_DecodeFile() blocks until the whole file has been decoded.
 *   - The message callback is invoked from a worker thread inside the
 *     decoder. In Delphi, do NOT touch VCL controls directly from the
 *     callback - use Synchronize() or PostMessage() instead.
 *   - The API is not reentrant: only one decode may run at a time.
 *
 * File paths:
 *   - mcr_path and systable_path may be local paths or UNC network
 *     paths (\\server\share\file, \\192.168.1.10\share\file, or the
 *     extended \\?\UNC\... form). Reading files over the network is
 *     fully supported.
 */
#ifndef HFDL_API_H
#define HFDL_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Calling convention for all exported functions. */
#ifndef HFDL_STDCALL
#ifdef _WIN32
#define HFDL_STDCALL __stdcall
#else
#define HFDL_STDCALL
#endif
#endif

// Maximum number of channels which can be decoded simultaneously
// (a systable with many in-band frequencies gets truncated to this).
#define HFDL_MAX_CHANNELS 64

#ifdef HFDL_API_EXPORTS
#define HFDL_API __declspec(dllexport)
#else
#define HFDL_API __declspec(dllimport)
#endif

/*
 * Message callback.
 *
 *  msg      - NUL-terminated ANSI string with the formatted message.
 *             The string is only valid for the duration of the call -
 *             copy it if you need to keep it.
 *  userdata - opaque pointer passed through from HFDL_DecodeFile().
 *
 * The callback is invoked from a decoder worker thread.
 */
typedef void (HFDL_STDCALL *hfdl_msg_callback)(char const *msg, void *userdata);

/*
 * Decode a multichannel MCR recording.
 *
 *  mcr_path       - path to the .mcr file to decode (ANSI string).
 *  bearing        - bearing of the receiving antenna, in degrees (0..360).
 *  elevation      - elevation of the receiving antenna, in degrees.
 *  systable_path  - path to the system table file (systable.conf), or NULL.
 *                   If given, it is used both for ground station details
 *                   and for automatic channel selection (see below).
 *  cache_db_path  - path to a SQLite database file used to persist the
 *                   aircraft ID to ICAO address mappings across runs,
 *                   or NULL. When given, a Logon Confirm decoded in this
 *                   (or a previous) run is remembered, so that position
 *                   reports from later recordings (without a logon) can
 *                   still be resolved to ICAO addresses. Requires the
 *                   DLL to be built with SQLite support.
 *  systable_db_path - path to a SQLite database file used to persist
 *                   partially received System Table PDU sets across runs,
 *                   or NULL. When given, a system table split across
 *                   multiple recordings can still be assembled: PDUs
 *                   received in earlier runs are restored automatically.
 *                   Requires the DLL to be built with SQLite support.
 *  output_format  - message format, one of HFDL_FMT_* constants below.
 *  callback       - message callback (see hfdl_msg_callback). Must not be NULL.
 *  userdata       - opaque pointer passed to the callback. May be NULL.
 *
 * Channel frequencies:
 *   If systable_path is given, the channels are auto-selected from the
 *   system table (only frequencies falling within the band recorded in
 *   the MCR file are used). If systable_path is NULL, the single HFDL
 *   frequency closest to the center of the recorded band is used.
 *
 * Returns 0 on success, negative value on error:
 *   -1  invalid argument (NULL path or NULL callback, bad format)
 *   -2  could not open or parse the MCR file
 *   -3  no channel frequencies available (no system table given and
 *       none could be derived, or the system table contains no
 *       frequency within the recorded band)
 *   -4  failed to initialize the decoder
 *   -5  decode aborted (decoder thread error)
 */

/* Output formats for HFDL_DecodeFile() */
#define HFDL_FMT_TEXT       1   /* human-readable text (default) */
#define HFDL_FMT_JSON       2   /* one JSON object per message, one per line */
#define HFDL_FMT_BASESTATION 3  /* Basestation CSV format (aircraft positions) */

HFDL_API int32_t HFDL_STDCALL HFDL_DecodeFile(
		char const *mcr_path,
		double bearing,
		double elevation,
		char const *systable_path,
		char const *cache_db_path,
		char const *systable_db_path,
		int32_t output_format,
		hfdl_msg_callback callback,
		void *userdata);

/*
 * Returns a NUL-terminated ANSI string with the library version
 * (e.g. "dumphfdl 1.7.0"). The returned pointer points to static
 * storage - do not free it.
 */
HFDL_API char const * HFDL_STDCALL HFDL_Version(void);

#ifdef __cplusplus
}
#endif

#endif /* HFDL_API_H */
