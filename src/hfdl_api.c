/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hfdl_api.c - implementation of the dumphfdl DLL API.
 *
 * Exposes HFDL_DecodeFile() / HFDL_Version() as stdcall exports for
 * 32-bit Windows host applications (Delphi 7 etc).
 *
 * This is a "decode a whole file" API: it sets up the same block
 * pipeline as the dumphfdl command line tool (input -> FFT -> channel
 * blocks -> PDU decoder -> output), waits for it to finish and tears
 * it down. Messages are delivered via a callback.
 *
 * Important:
 *   - The API is NOT reentrant. Only one decode may run at a time.
 *   - The callback is invoked from a worker thread. In Delphi, use
 *     Synchronize() or PostMessage() - never touch VCL directly.
 *   - All error paths return an error code; the DLL never calls
 *     exit() (which would kill the host process).
 */
#include <stdlib.h>                     // NULL
#include <string.h>                     // strcmp, strlen
#include <stdio.h>                      // fprintf (diagnostics only)
#include <unistd.h>                     // usleep
#include <libacars/libacars.h>          // la_config_set_int
#include <libacars/acars.h>             // LA_ACARS_BEARER_HFDL

#define HFDL_API_EXPORTS
#include "hfdl_api.h"                   // HFDL_DecodeFile, HFDL_Version

#include "input-common.h"               // struct input_cfg, input_create, input_cfg_create
#include "input-mcr.h"                  // INPUT_TYPE_MCR, mcr_input_preload_config
#include "systable.h"                   // systable_*
#include "ac_cache.h"                   // ac_cache_create, ac_cache_destroy, AC_cache
#include "ac_data.h"                    // AC_data (stub when built without sqlite)
#include "globals.h"                    // do_exit, Config
#include "config.h"                     // HFDL_SYMBOL_RATE, SPS
#include "fft.h"                        // fft_create, fft_destroy, csdr_fft_init
#include "fastddc.h"                    // (not used, kept for include-order clarity)
#include "hfdl.h"                       // hfdl_channel_create, hfdl_pdu_decoder_*
#include "pdu.h"                        // hfdl_pdu_decoder_start/stop/init
#include "libcsdr.h"                    // compute_fft_decimation_rate
#include "output-common.h"              // fmtr_instance_new, output_instance_new
#include "output-callback.h"            // out_DEF_callback, out_callback_set
#include "fmtr-text.h"                  // fmtr_DEF_text
#include "fmtr-json.h"                  // fmtr_DEF_json
#include "fmtr-basestation.h"           // fmtr_DEF_basestation
#include "block.h"                      // block_connect_*, block_start
#include "util.h"                       // ASSERT, XFREE

// DUMPHFDL_VERSION is an extern variable defined in the generated
// version.c (declared in globals.h, already included above).

// Error codes returned by HFDL_DecodeFile
enum {
	API_OK            =  0,
	API_ERR_ARGS      = -1,
	API_ERR_MCR       = -2,
	API_ERR_NO_FREQ   = -3,
	API_ERR_INIT      = -4,
	API_ERR_ABORT     = -5
};

// Registered callback for the current decode
static hfdl_msg_callback api_callback = NULL;
static void *api_userdata = NULL;

// Start output threads for all formatter instances (same logic as
// start_all_output_threads() in main.c, which is static there).
static void api_start_output_thread(void *p, void *ctx);
static void api_start_output_threads_for_fmtr(void *p, void *ctx);

static void api_start_output_thread(void *p, void *ctx) {
	UNUSED(ctx);
	ASSERT(p != NULL);
	output_instance_t *output = p;
	start_thread(output->output_thread, output_thread, output);
}

static void api_start_output_threads_for_fmtr(void *p, void *ctx) {
	UNUSED(ctx);
	ASSERT(p != NULL);
	fmtr_instance_t *fmtr = p;
	la_list_foreach(fmtr->outputs, api_start_output_thread, NULL);
}

static void api_start_all_output_threads(la_list *outputs) {
	la_list_foreach(outputs, api_start_output_threads_for_fmtr, NULL);
}

// Adapter: bridges the output-callback driver to the DLL API callback.
// Runs on the output thread; must not block.
static void api_msg_adapter(char const *msg, output_format_t format, void *userdata) {
	UNUSED(format);
	UNUSED(userdata);
	if(api_callback != NULL && msg != NULL) {
		api_callback(msg, api_userdata);
	}
}

HFDL_API char const * HFDL_STDCALL HFDL_Version(void) {
	return DUMPHFDL_VERSION;
}

HFDL_API int32_t HFDL_STDCALL HFDL_DecodeFile(
		char const *mcr_path,
		double bearing,
		double elevation,
		char const *systable_path,
		char const *cache_db_path,
		char const *systable_db_path,
		int32_t output_format,
		hfdl_msg_callback callback,
		void *userdata) {

	// ---- argument validation ----
	if(mcr_path == NULL || callback == NULL) {
		return API_ERR_ARGS;
	}

	// Select the formatter and output format descriptor.
	fmtr_descriptor_t *fmttd = NULL;
	output_format_t outfmt = OFMT_UNKNOWN;
	switch(output_format) {
		case HFDL_FMT_TEXT:
			fmttd = &fmtr_DEF_text;
			outfmt = OFMT_TEXT;
			break;
		case HFDL_FMT_JSON:
			fmttd = &fmtr_DEF_json;
			outfmt = OFMT_JSON;
			break;
		case HFDL_FMT_BASESTATION:
			fmttd = &fmtr_DEF_basestation;
			outfmt = OFMT_BASESTATION;
			break;
		default:
			return API_ERR_ARGS;
	}

	// ---- reset global state from a possible previous call ----
	do_exit = 0;
	api_callback = callback;
	api_userdata = userdata;
	out_callback_set(api_msg_adapter, NULL);

	int32_t ret = API_OK;

	// All resources are declared here and initialized to NULL, so that
	// the single cleanup section at the end can safely release whatever
	// has been allocated so far. All destroy functions used below are
	// NULL-safe.
	struct input_cfg *input_cfg = input_cfg_create();
	la_list *outputs = NULL;
	int32_t channel_cnt = 0;
	int32_t *frequencies = NULL;
	struct block *input = NULL;
	struct block *fft = NULL;
	struct block *channel_blocks[HFDL_MAX_CHANNELS];
	for(int32_t i = 0; i < HFDL_MAX_CHANNELS; i++) {
		channel_blocks[i] = NULL;
	}
	int32_t fft_decimation_rate = 0;
	float fftfilt_transition_bw = 0.0f;
	if(input_cfg == NULL) {
		ret = API_ERR_INIT;
		goto cleanup;
	}

	// ---- configure input ----
	input_cfg->source = strdup(mcr_path);
	input_cfg->type = INPUT_TYPE_MCR;
	input_cfg->bearing = bearing;
	input_cfg->elevation = elevation;

	// Read sample rate and center frequency from the MCR header.
	// mcr_input_preload_config() also validates the header and returns
	// -1 on error (bad signature, truncated file, etc).
	if(mcr_input_preload_config(input_cfg) < 0) {
		fprintf(stderr, "HFDL_DecodeFile: failed to read MCR header from '%s'\n", mcr_path);
		ret = API_ERR_MCR;
		goto cleanup;
	}

	// ---- system table ----
	// systable_save_file is NULL: we never save the table back to disk.
	Systable = systable_create(NULL, systable_db_path);
	if(systable_path != NULL && strlen(systable_path) > 0) {
		Systable_lock();
		if(systable_read_from_file(Systable, systable_path) == false) {
			fprintf(stderr, "HFDL_DecodeFile: could not load system table from '%s'\n",
					systable_path);
			Systable_unlock();
			ret = API_ERR_INIT;
			goto cleanup;
		}
		Systable_unlock();
	}

	// ---- channel frequencies ----
	if(systable_is_available(Systable)) {
		// Auto-select the frequencies which fall within the recorded band.
		int32_t half_bandwidth = input_cfg->sample_rate / 2;
		frequencies = systable_get_frequencies_in_band(Systable,
				input_cfg->centerfreq, half_bandwidth, &channel_cnt);
		if(frequencies == NULL) {
			fprintf(stderr, "HFDL_DecodeFile: no HFDL channels from the system table "
					"fall within the recorded band\n");
			ret = API_ERR_NO_FREQ;
			goto cleanup;
		}
	} else {
		// No system table: monitor the single channel at the center of
		// the recorded band. This is a pragmatic default - the host
		// application is expected to pass a system table for full
		// functionality.
		channel_cnt = 1;
		frequencies = XCALLOC(1, sizeof(int32_t));
		if(frequencies == NULL) {
			ret = API_ERR_INIT;
			goto cleanup;
		}
		frequencies[0] = input_cfg->centerfreq;
	}
	if(channel_cnt > HFDL_MAX_CHANNELS) {
		channel_cnt = HFDL_MAX_CHANNELS;
	}

	if(input_cfg->sample_rate < HFDL_SYMBOL_RATE * SPS) {
		fprintf(stderr, "HFDL_DecodeFile: sample rate too low\n");
		ret = API_ERR_NO_FREQ;
		goto cleanup;
	}

	// ---- aircraft cache ----
	if((AC_cache = ac_cache_create(cache_db_path)) == NULL) {
		fprintf(stderr, "HFDL_DecodeFile: unable to initialize aircraft address cache\n");
		ret = API_ERR_INIT;
		goto cleanup;
	}

	// ---- output: single callback output on the selected formatter ----
	fmtr_instance_t *fmtr = fmtr_instance_new(fmttd, FMTR_INTYPE_DECODED_FRAME);
	if(fmtr == NULL) {
		ret = API_ERR_INIT;
		goto cleanup;
	}
	{
		output_instance_t *out = output_instance_new(&out_DEF_callback, outfmt, NULL);
		if(out == NULL) {
			fmtr_instance_destroy(fmtr);
			ret = API_ERR_INIT;
			goto cleanup;
		}
		fmtr->outputs = la_list_append(fmtr->outputs, out);
	}
	outputs = la_list_append(outputs, fmtr);

	// ---- pipeline ----
	input = input_create(input_cfg);
	if(input == NULL || input_init(input) < 0) {
		fprintf(stderr, "HFDL_DecodeFile: unable to initialize input\n");
		ret = API_ERR_INIT;
		goto cleanup;
	}

	csdr_fft_init(1);   // single FFT thread is enough for a file decode

	fft_decimation_rate = compute_fft_decimation_rate(
			input_cfg->sample_rate, HFDL_SYMBOL_RATE * SPS);
	fftfilt_transition_bw = compute_filter_relative_transition_bw(
			input_cfg->sample_rate, HFDL_CHANNEL_TRANSITION_BW_HZ);

	fft = fft_create(fft_decimation_rate, fftfilt_transition_bw);
	if(fft == NULL) {
		ret = API_ERR_INIT;
		goto cleanup;
	}

	la_config_set_int("acars_bearer", LA_ACARS_BEARER_HFDL);
	hfdl_init_globals();

	for(int32_t i = 0; i < channel_cnt; i++) {
		channel_blocks[i] = hfdl_channel_create(input_cfg->sample_rate,
				fft_decimation_rate, fftfilt_transition_bw,
				input_cfg->centerfreq, frequencies[i],
				input_cfg->file_start_time_us);
		if(channel_blocks[i] == NULL) {
			fprintf(stderr, "HFDL_DecodeFile: failed to initialize channel %d\n", i);
			ret = API_ERR_INIT;
			goto cleanup;
		}
	}

	if(block_connect_one2one(input, fft) != 1 ||
			block_connect_one2many(fft, channel_cnt, channel_blocks) != channel_cnt) {
		ret = API_ERR_INIT;
		goto cleanup;
	}

	// Start output threads and the PDU decoder.
	api_start_all_output_threads(outputs);
	hfdl_pdu_decoder_init();
	if(hfdl_pdu_decoder_start(outputs) != 0) {
		fprintf(stderr, "HFDL_DecodeFile: failed to start decoder thread\n");
		ret = API_ERR_INIT;
		goto cleanup;
	}

	// Start the pipeline: channels, FFT, input.
	if(block_set_start(channel_cnt, channel_blocks) != channel_cnt ||
			block_start(fft) != 1 ||
			block_start(input) != 1) {
		fprintf(stderr, "HFDL_DecodeFile: failed to start pipeline\n");
		ret = API_ERR_ABORT;
		goto cleanup;
	}

	// ---- wait for completion ----
	// do_exit is set to 1 by the input thread when the file is exhausted.
	while(do_exit == 0) {
		usleep(500000);
	}
	hfdl_pdu_decoder_stop();

	// Wait for all threads to finish.
	while(block_is_running(input) ||
			block_is_running(fft) ||
			block_set_is_any_running(channel_cnt, channel_blocks) ||
			hfdl_pdu_decoder_is_running() ||
			output_thread_is_any_running(outputs)) {
		usleep(500000);
	}

	hfdl_print_summary();

cleanup:
	// ---- teardown ----
	// Safe on both the success path and every error path: all destroy
	// functions are NULL-safe and disconnect calls are no-ops on
	// disconnected blocks.
	if(fft != NULL) {
		block_disconnect_one2many(fft, channel_cnt, channel_blocks);
		block_disconnect_one2one(input, fft);
		fft_destroy(fft);
	}
	for(int32_t i = 0; i < channel_cnt; i++) {
		if(channel_blocks[i] != NULL) {
			hfdl_channel_destroy(channel_blocks[i]);
		}
	}
	csdr_fft_destroy();

	input_destroy(input);
	XFREE(frequencies);
	la_list_free_full(outputs, fmtr_instance_destroy);

	Systable_lock();
	systable_destroy(Systable);
	Systable_unlock();

	AC_cache_lock();
	ac_cache_destroy(AC_cache);
	AC_cache_unlock();

	XFREE(input_cfg->source);
	input_cfg_destroy(input_cfg);

	api_callback = NULL;
	api_userdata = NULL;
	out_callback_reset();
	return ret;
}
