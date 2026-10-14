/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MCR input driver.
 *
 * Reads multichannel MCR recordings (as produced by some direction
 * finding receivers) and combines all channels into a single complex
 * I/Q stream, using the calibration coefficients stored in the file.
 * A phase correction, derived from the given bearing and elevation,
 * is applied to the coefficients before combining. This steers the
 * combined beam towards the wanted direction.
 *
 * The file layout and the combining algorithm are ported from the
 * Python implementation in mcr_loader.py (itself ported from the
 * Delphi uDetectorSSBForm.pas):
 *
 *   - 48 byte header (all integers and floats are little-endian):
 *       uint32  signature
 *       uint32  creator_id
 *       int64   record_id
 *       int64   beg_time
 *       int32   channels
 *       int32   samples_quantity
 *       int32   frequency          (Hz)
 *       int32   band_width
 *       int32   samples_per_sec
 *       int32   samples_quantity_high
 *
 *   - calibration data (three separate arrays):
 *       float32 radius[channels]   (antenna element radii)
 *       float32 angle[channels]    (antenna element angles, degrees)
 *       float64 calib[channels]    (complex, as real/imag pairs)
 *
 *   - sample data: one sample consists of channels * 2 int16 values,
 *     interleaved as I/Q pairs per channel:
 *       I[0], Q[0], I[1], Q[1], ..., I[channels-1], Q[channels-1]
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>
#include <errno.h>
#include <inttypes.h>
#include <unistd.h>         // usleep
#include <liquid/liquid.h>  // cbuffercf_*
#include "block.h"          // block_*
#include "input-common.h"   // input, sample_format, input_vtable
#include "input-helpers.h"  // complex_samples_produce
#include "input-mcr.h"      // mcr_input_vtable, mcr_input_preload_config
#include "util.h"           // debug_print, ASSERT, XCALLOC, NEW, container_of
#include "globals.h"        // do_exit

#define MCR_HEADER_SIZE             48
#define MCR_CALIB_ENTRY_SIZE        24      // 4 (radius) + 4 (angle) + 16 (calib coeff)
#define MCR_SIGNATURE_MIN           5391181
#define MCR_FREQUENCY_MIN           2941000
#define MCR_FREQUENCY_MAX           30000000
#define MCR_SAMPLE_RATE_MIN         3600.0
#define MCR_CHANNELS_MAX            4096
#define MCR_READ_BUFSIZE_DEFAULT    320000U
#define MCR_INT16_FULL_SCALE        32768.0f
#define SPEED_OF_LIGHT              299792458.0

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct mcr_header {
	uint32_t signature;
	uint32_t creator_id;
	int64_t record_id;
	int64_t beg_time;
	int32_t channels;
	int32_t samples_quantity;
	int32_t frequency;
	int32_t band_width;
	int32_t samples_per_sec;
	int32_t samples_quantity_high;
};

struct mcr_input {
	struct input input;
	FILE *fh;
	int32_t channels;
	double complex *calib;      // calibration coefficients with phase correction applied
};

// Little-endian readers. The bit patterns are assembled as integers, so the
// result does not depend on the endianness of the host machine.
static uint16_t le_u16(uint8_t const *p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le_u32(uint8_t const *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
			((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le_u64(uint8_t const *p) {
	return (uint64_t)le_u32(p) | ((uint64_t)le_u32(p + 4) << 32);
}

static int16_t le_i16(uint8_t const *p) {
	return (int16_t)le_u16(p);
}

static float le_f32(uint8_t const *p) {
	uint32_t bits = le_u32(p);
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static double le_f64(uint8_t const *p) {
	uint64_t bits = le_u64(p);
	double value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void mcr_header_parse(uint8_t const *buf, struct mcr_header *h) {
	h->signature = le_u32(buf + 0);
	h->creator_id = le_u32(buf + 4);
	h->record_id = (int64_t)le_u64(buf + 8);
	h->beg_time = (int64_t)le_u64(buf + 16);
	h->channels = (int32_t)le_u32(buf + 24);
	h->samples_quantity = (int32_t)le_u32(buf + 28);
	h->frequency = (int32_t)le_u32(buf + 32);
	h->band_width = (int32_t)le_u32(buf + 36);
	h->samples_per_sec = (int32_t)le_u32(buf + 40);
	h->samples_quantity_high = (int32_t)le_u32(buf + 44);
}

// Reads the 48 byte header from the current file position.
// Returns 0 on success, -1 on error.
static int32_t mcr_read_header(FILE *fh, struct mcr_header *h) {
	uint8_t buf[MCR_HEADER_SIZE];
	if(fread(buf, 1, MCR_HEADER_SIZE, fh) != MCR_HEADER_SIZE) {
		fprintf(stderr, "MCR: failed to read file header\n");
		return -1;
	}
	mcr_header_parse(buf, h);
	return 0;
}

// Returns 0 if the header is valid, -1 otherwise.
static int32_t mcr_validate_header(struct mcr_header const *h) {
	if(h->signature < MCR_SIGNATURE_MIN) {
		fprintf(stderr, "MCR: invalid signature: %" PRIu32 "\n", h->signature);
		return -1;
	}
	if(h->frequency < MCR_FREQUENCY_MIN || h->frequency > MCR_FREQUENCY_MAX) {
		fprintf(stderr, "MCR: invalid frequency: %" PRId32 " Hz (must be %d-%d Hz)\n",
				h->frequency, MCR_FREQUENCY_MIN, MCR_FREQUENCY_MAX);
		return -1;
	}
	if((double)h->samples_per_sec < MCR_SAMPLE_RATE_MIN) {
		fprintf(stderr, "MCR: invalid sampling frequency: %" PRId32 " Hz (must be > %d Hz)\n",
				h->samples_per_sec, (int32_t)MCR_SAMPLE_RATE_MIN);
		return -1;
	}
	if(h->channels <= 0 || h->channels > MCR_CHANNELS_MAX) {
		fprintf(stderr, "MCR: invalid channel count: %" PRId32 "\n", h->channels);
		return -1;
	}
	return 0;
}

// Reads the calibration block from the current file position and applies the
// phase correction for the given bearing and elevation. On success,
// *calib_out points to a heap-allocated array of channels complex
// coefficients (the caller must free it).
// Returns 0 on success, -1 on error.
static int32_t mcr_load_calibration(FILE *fh, struct mcr_header const *h,
		double bearing, double elevation, double complex **calib_out) {
	int32_t channels = h->channels;
	size_t calib_size = (size_t)channels * MCR_CALIB_ENTRY_SIZE;

	uint8_t *buf = XCALLOC(calib_size, sizeof(uint8_t));
	double *radius = XCALLOC((size_t)channels, sizeof(double));
	double *angle = XCALLOC((size_t)channels, sizeof(double));
	double complex *calib = XCALLOC((size_t)channels, sizeof(double complex));
	if(buf == NULL || radius == NULL || angle == NULL || calib == NULL) {
		fprintf(stderr, "MCR: out of memory\n");
		XFREE(buf);
		XFREE(radius);
		XFREE(angle);
		XFREE(calib);
		return -1;
	}

	if(fread(buf, 1, calib_size, fh) != calib_size) {
		fprintf(stderr, "MCR: failed to read calibration data\n");
		XFREE(buf);
		XFREE(radius);
		XFREE(angle);
		XFREE(calib);
		return -1;
	}

	// The calibration block consists of three separate arrays
	uint8_t *p = buf;
	for(int32_t c = 0; c < channels; c++) {
		radius[c] = (double)le_f32(p);
		p += 4;
	}
	for(int32_t c = 0; c < channels; c++) {
		angle[c] = (double)le_f32(p);
		p += 4;
	}
	for(int32_t c = 0; c < channels; c++) {
		double re = le_f64(p);
		double im = le_f64(p + 8);
		calib[c] = re + im * I;
		p += 16;
	}
	XFREE(buf);

	// Apply the phase correction for the given bearing and elevation
	double bear_rad = (bearing - 180.0) * M_PI / 180.0;
	double elev_rad = elevation * M_PI / 180.0;
	double coeff_arg = (double)h->frequency * 2.0 * M_PI * cos(elev_rad) / SPEED_OF_LIGHT;
	for(int32_t c = 0; c < channels; c++) {
		double arg = coeff_arg * radius[c] * cos(bear_rad - angle[c]);
		calib[c] *= cos(arg) + sin(arg) * I;
	}

	XFREE(radius);
	XFREE(angle);
	*calib_out = calib;
	return 0;
}

// Combines all channels of each sample into a single complex value using the
// calibration coefficients. The samples are normalized to the full scale of
// the 16-bit input format (same convention as the CS16 sample format).
static void mcr_convert_sample_buffer(struct input *input, void *inbuf,
		size_t len, float complex *outbuf) {
	struct mcr_input *mcr = container_of(input, struct mcr_input, input);
	int32_t channels = mcr->channels;
	size_t bytes_per_sample = (size_t)channels * 4;

	if(UNLIKELY(len % bytes_per_sample != 0)) {
		debug_print(D_SDR, "Warning: buf len %zu is not a multiple of %zu, truncating\n",
				len, bytes_per_sample);
		len -= (len % bytes_per_sample);
	}
	if(UNLIKELY(len == 0)) {
		return;
	}

	int16_t const *shortbuf = inbuf;
	size_t samples = len / bytes_per_sample;
	for(size_t s = 0; s < samples; s++) {
		double complex combined = 0.0;
		for(int32_t c = 0; c < channels; c++) {
			int16_t re = shortbuf[s * channels * 2 + c * 2];
			int16_t im = shortbuf[s * channels * 2 + c * 2 + 1];
			combined += ((double)re + (double)im * I) * mcr->calib[c];
		}
		combined /= MCR_INT16_FULL_SCALE;
		outbuf[s] = CMPLXF(creal(combined), cimag(combined));
	}
}

struct input *mcr_input_create(struct input_cfg *cfg) {
	UNUSED(cfg);
	NEW(struct mcr_input, mcr_input);
	return &mcr_input->input;
}

void mcr_input_destroy(struct input *input) {
	if(input != NULL) {
		struct mcr_input *mcr = container_of(input, struct mcr_input, input);
		if(mcr->fh != NULL) {
			fclose(mcr->fh);
		}
		XFREE(mcr->calib);
		XFREE(mcr);
	}
}

void *mcr_input_thread(void *ctx) {
	ASSERT(ctx);
	struct block *block = ctx;
	struct input *input = container_of(block, struct input, block);
	struct mcr_input *mcr = container_of(input, struct mcr_input, input);
	struct circ_buffer *circ_buffer = &block->producer.out->circ_buffer;

	ASSERT(mcr->fh != NULL);
	ASSERT(input->config->read_buffer_size > 0);
	size_t bufsize = input->config->read_buffer_size;

	void *inbuf = XCALLOC(bufsize, sizeof(uint8_t));
	float complex *outbuf = XCALLOC(bufsize / input->bytes_per_sample,
			sizeof(float complex));
	size_t space_available, len, samples_read;
	do {
		len = fread(inbuf, 1, bufsize, mcr->fh);
		samples_read = len / input->bytes_per_sample;
		while(true) {
			pthread_mutex_lock(circ_buffer->mutex);
			space_available = cbuffercf_space_available(circ_buffer->buf);
			pthread_mutex_unlock(circ_buffer->mutex);
			if(space_available * input->bytes_per_sample >= len) {
				break;
			}
			usleep(100000);
		}
		input->convert_sample_buffer(input, inbuf, len, outbuf);
		complex_samples_produce(circ_buffer, outbuf, samples_read);
	} while(len > 0 && do_exit == 0);
	fclose(mcr->fh);
	mcr->fh = NULL;
	debug_print(D_MISC, "Shutdown ordered, signaling consumer shutdown\n");
	block_connection_one2one_shutdown(block->producer.out);
	do_exit = 1;
	block->running = false;
	XFREE(inbuf);
	XFREE(outbuf);
	return NULL;
}

int32_t mcr_input_init(struct input *input) {
	ASSERT(input != NULL);
	struct mcr_input *mcr = container_of(input, struct mcr_input, input);
	struct input_cfg *cfg = input->config;

	if(cfg->source == NULL) {
		fprintf(stderr, "MCR: no input file given\n");
		return -1;
	}
	if(strcmp(cfg->source, "-") == 0) {
		fprintf(stderr, "MCR: reading from standard input is not supported\n");
		return -1;
	}
	mcr->fh = fopen(cfg->source, "rb");
	if(mcr->fh == NULL) {
		fprintf(stderr, "MCR: failed to open input file %s: %s\n",
				cfg->source, strerror(errno));
		return -1;
	}

	struct mcr_header h;
	if(mcr_read_header(mcr->fh, &h) < 0) {
		return -1;
	}
	if(mcr_validate_header(&h) < 0) {
		return -1;
	}

	// Use the values from the file header, unless overridden by the user
	if(cfg->sample_rate < 0) {
		cfg->sample_rate = h.samples_per_sec;
		debug_print(D_MISC, "MCR: sample rate from header: %d\n", cfg->sample_rate);
	}
	if(cfg->centerfreq < 0) {
		cfg->centerfreq = h.frequency;
		debug_print(D_MISC, "MCR: center frequency from header: %d\n", cfg->centerfreq);
	}

	if(mcr_load_calibration(mcr->fh, &h, cfg->bearing, cfg->elevation, &mcr->calib) < 0) {
		return -1;
	}
	mcr->channels = h.channels;

	fprintf(stderr, "MCR: %d channels, sample rate %d Hz, center frequency %d Hz\n",
			h.channels, cfg->sample_rate, cfg->centerfreq);
	fprintf(stderr, "MCR: bearing %.1f deg, elevation %.1f deg\n",
			cfg->bearing, cfg->elevation);

	input->bytes_per_sample = (int32_t)(h.channels * 4);
	input->full_scale = MCR_INT16_FULL_SCALE;
	if(cfg->read_buffer_size <= 0) {
		cfg->read_buffer_size = MCR_READ_BUFSIZE_DEFAULT;
	}
	// Round the buffer size down to a whole number of samples, so that
	// channel counts which don't divide the default size evenly still work
	if(cfg->read_buffer_size % (uint32_t)input->bytes_per_sample != 0) {
		uint32_t adjusted = cfg->read_buffer_size -
				(cfg->read_buffer_size % (uint32_t)input->bytes_per_sample);
		debug_print(D_MISC, "MCR: read buffer size %d is not a multiple of sample size %d, using %u\n",
				cfg->read_buffer_size, input->bytes_per_sample, adjusted);
		cfg->read_buffer_size = adjusted;
	}
	input->block.producer.max_tu = cfg->read_buffer_size / input->bytes_per_sample;
	debug_print(D_SDR, "%s: max_tu=%zu\n",
			cfg->source, input->block.producer.max_tu);

	// Install the channel combining converter. input_init() in input-common.c
	// must not overwrite it with a generic sample format converter.
	input->convert_sample_buffer = mcr_convert_sample_buffer;
	return 0;
}

int32_t mcr_input_preload_config(struct input_cfg *cfg) {
	ASSERT(cfg != NULL);
	FILE *fh = fopen(cfg->source, "rb");
	if(fh == NULL) {
		fprintf(stderr, "MCR: failed to open input file %s: %s\n",
				cfg->source, strerror(errno));
		return -1;
	}
	struct mcr_header h;
	int32_t ret = mcr_read_header(fh, &h);
	if(ret == 0) {
		ret = mcr_validate_header(&h);
	}
	if(ret == 0) {
		if(cfg->sample_rate < 0) {
			cfg->sample_rate = h.samples_per_sec;
		}
		if(cfg->centerfreq < 0) {
			cfg->centerfreq = h.frequency;
		}
		// Fill the file start time from the header unless the user has
		// explicitly overridden it with --file-start-time.
		// beg_time is stored in microseconds since epoch (UTC).
		if(cfg->file_start_time_us == 0 && h.beg_time > 0) {
			cfg->file_start_time_us = h.beg_time;
		}
	}
	fclose(fh);
	return ret;
}

struct input_vtable mcr_input_vtable = {
	.create = mcr_input_create,
	.init = mcr_input_init,
	.destroy = mcr_input_destroy,
	.rx_thread_routine = mcr_input_thread
};
