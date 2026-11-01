/* SPDX-License-Identifier: GPL-3.0-or-later */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "spectrogram.h"
#include "fft.h"
#include "util.h"

#define SP_DEFAULT_WIDTH      512
#define SP_DEFAULT_HEIGHT     256
#define SP_DEFAULT_FFT_SIZE   512
#define SP_FLT_EPSILON        1.192092896e-07f

struct spectrogram_state {
	bool enabled;
	int width;
	int height;
	int fft_size;
	int frame_count;
	int end_col;
	char *path;
	int format;

	int32_t sample_rate;
	int32_t display_bandwidth;
	int resample_factor;

	float complex *samples;
	size_t sample_count;
	size_t sample_cap;

	float complex *resampled;
	size_t resampled_count;
	size_t resampled_cap;

	float complex *fft_in;
	float complex *fft_out;
	struct fft_plan_s *fft_plan;

	float db_min;
	float db_max;
	bool db_range_set;

	uint8_t *image;
};

static struct spectrogram_state sp = {0};

static uint8_t sp_db_to_color(float db) {
	if(sp.db_range_set) {
		float val = (db - sp.db_min) / (sp.db_max - sp.db_min);
		if(val < 0.0f) val = 0.0f;
		if(val > 1.0f) val = 1.0f;
		return (uint8_t)(val * 255.0f);
	}
	float val = (db + 100.0f) / 100.0f;
	if(val < 0.0f) val = 0.0f;
	if(val > 1.0f) val = 1.0f;
	return (uint8_t)(val * 255.0f);
}

static void sp_compute_frame(void) {
	if(sp.frame_count >= sp.width) return;
	csdr_fft_execute(sp.fft_plan);
	for(int j = 0; j < sp.height; j++) {
		float mag = cabsf(sp.fft_out[j]);
		float db = 10.0f * log10f(SP_FLT_EPSILON + mag * mag);
		uint8_t val = sp_db_to_color(db);
		int y = sp.height - 1 - j;
		uint8_t *pixel = sp.image + (y * sp.width + sp.frame_count) * 3;
		pixel[0] = 0;
		pixel[1] = val;
		pixel[2] = val;
	}
	sp.frame_count++;
}

static void sp_compute_frame_range_pass(void) {
	if(sp.frame_count >= sp.width) return;
	csdr_fft_execute(sp.fft_plan);
	for(int j = 0; j < sp.height; j++) {
		float mag = cabsf(sp.fft_out[j]);
		float db = 10.0f * log10f(SP_FLT_EPSILON + mag * mag);
		if(!sp.db_range_set || db < sp.db_min) sp.db_min = db;
		if(!sp.db_range_set || db > sp.db_max) sp.db_max = db;
	}
	sp.db_range_set = true;
	sp.frame_count++;
}

static void sp_draw_end_line(void) {
	if(sp.end_col < 0 || sp.end_col >= sp.width) return;
	for(int y = 0; y < sp.height; y++) {
		uint8_t *pixel = sp.image + (y * sp.width + sp.end_col) * 3;
		if((y & 1) == 0) {
			pixel[0] = 128;
			pixel[1] = 0;
			pixel[2] = 0;
		}
	}
}

int spectrogram_init(struct input_cfg const *cfg) {
	memset(&sp, 0, sizeof(sp));
	if(cfg == NULL || !cfg->spectrogram_enabled) return 0;

	sp.enabled = true;
	sp.width = (cfg->spectrogram_width > 0) ? cfg->spectrogram_width : SP_DEFAULT_WIDTH;
	sp.height = (cfg->spectrogram_height > 0) ? cfg->spectrogram_height : SP_DEFAULT_HEIGHT;
	sp.fft_size = (cfg->spectrogram_fft_size > 0) ? cfg->spectrogram_fft_size : SP_DEFAULT_FFT_SIZE;
	sp.path = cfg->spectrogram_path ? strdup(cfg->spectrogram_path) : NULL;
	sp.format = cfg->spectrogram_format;
	sp.end_col = -1;
	sp.sample_rate = cfg->sample_rate;
	sp.display_bandwidth = cfg->spectrogram_display_bandwidth;

	if(sp.path == NULL) {
		fprintf(stderr, "Spectrogram: no output path given\n");
		return -1;
	}

	// Calculate resampling factor to show full recording bandwidth
	// For complex FFT: coverage is 0..sample_rate. We want 0..display_bandwidth.
	// So target sample rate = display_bandwidth.
	if(sp.display_bandwidth > 0 && sp.sample_rate > sp.display_bandwidth) {
		int factor = 1;
		while(sp.sample_rate / factor > sp.display_bandwidth && factor < 1024) {
			factor *= 2;
		}
		sp.resample_factor = factor;
	} else {
		sp.resample_factor = 1;
	}

	fprintf(stderr, "Spectrogram: sample_rate=%d Hz, display_bw=%d Hz, resample_factor=%d\n",
			sp.sample_rate, sp.display_bandwidth, sp.resample_factor);

	sp.fft_in = XCALLOC(sp.fft_size, sizeof(float complex));
	sp.fft_out = XCALLOC(sp.fft_size, sizeof(float complex));
	sp.fft_plan = csdr_make_fft_c2c(sp.fft_size, sp.fft_in, sp.fft_out, 1, 0);
	if(sp.fft_plan == NULL) {
		fprintf(stderr, "Spectrogram: failed to create FFT plan\n");
		return -1;
	}

	sp.image = XCALLOC((size_t)sp.width * sp.height, 3);
	if(sp.image == NULL) {
		fprintf(stderr, "Spectrogram: out of memory for image\n");
		return -1;
	}

	sp.sample_cap = 4 * 1024 * 1024;
	sp.samples = XCALLOC(sp.sample_cap, sizeof(float complex));
	if(sp.samples == NULL) {
		fprintf(stderr, "Spectrogram: out of memory for sample buffer\n");
		return -1;
	}

	sp.resampled_cap = 2 * 1024 * 1024;
	sp.resampled = XCALLOC(sp.resampled_cap, sizeof(float complex));
	if(sp.resampled == NULL) {
		fprintf(stderr, "Spectrogram: out of memory for resampled buffer\n");
		return -1;
	}

	fprintf(stderr, "Spectrogram: enabled %dx%d FFT=%d path=%s\n",
			sp.width, sp.height, sp.fft_size, sp.path);
	return 0;
}

void spectrogram_accumulate(float complex const *samples, size_t count) {
	if(!sp.enabled || count == 0) return;
	if(sp.sample_count + count > sp.sample_cap) {
		size_t new_cap = sp.sample_cap * 2;
		while(new_cap < sp.sample_count + count) new_cap *= 2;
		float complex *new_buf = realloc(sp.samples, new_cap * sizeof(float complex));
		if(new_buf == NULL) return;
		sp.samples = new_buf;
		sp.sample_cap = new_cap;
	}
	memcpy(sp.samples + sp.sample_count, samples, count * sizeof(float complex));
	sp.sample_count += count;
}

static void sp_resample_samples(void) {
	if(sp.resample_factor <= 1 || sp.sample_count == 0) {
		sp.resampled_count = sp.sample_count;
		if(sp.resampled_count > sp.resampled_cap) {
			sp.resampled_cap = sp.resampled_count;
			sp.resampled = realloc(sp.resampled, sp.resampled_cap * sizeof(float complex));
		}
		memcpy(sp.resampled, sp.samples, sp.sample_count * sizeof(float complex));
		return;
	}

	int factor = sp.resample_factor;
	size_t new_count = sp.sample_count / (size_t)factor;
	if(new_count == 0) new_count = 1;

	if(new_count > sp.resampled_cap) {
		sp.resampled_cap = new_count;
		sp.resampled = realloc(sp.resampled, sp.resampled_cap * sizeof(float complex));
	}

	for(size_t i = 0; i < new_count; i++) {
		size_t start = i * (size_t)factor;
		float complex sum = 0.0f;
		size_t count = 0;
		for(int j = 0; j < factor && (start + j) < sp.sample_count; j++) {
			sum += sp.samples[start + j];
			count++;
		}
		sp.resampled[i] = sum / (float)count;
	}
	sp.resampled_count = new_count;
}

int spectrogram_write(void) {
	if(!sp.enabled || sp.sample_count == 0) return 0;

	// Resample to show full recording bandwidth, but only if we have
	// enough samples for at least one FFT frame after resampling.
	int fft_size = sp.fft_size;
	if(fft_size > sp.height) {
		fft_size = sp.height;
	}
	bool use_resampled = false;
	if(sp.resample_factor > 1 && sp.sample_count / (size_t)sp.resample_factor >= (size_t)fft_size) {
		sp_resample_samples();
		use_resampled = true;
	} else {
		sp.resampled_count = sp.sample_count;
		if(sp.resampled_count > sp.resampled_cap) {
			sp.resampled_cap = sp.resampled_count;
			sp.resampled = realloc(sp.resampled, sp.resampled_cap * sizeof(float complex));
		}
		memcpy(sp.resampled, sp.samples, sp.sample_count * sizeof(float complex));
	}

	int sample_rate = sp.sample_rate / sp.resample_factor;
	if(!use_resampled) {
		sample_rate = sp.sample_rate;
	}

	// Delphi hop/offset scaled by sample rate ratio
	// Reference: 1221 samples at 31250 Hz
	int hop = (int)roundf(1221.0f * (float)sample_rate / 31250.0f);
	int offset = (int)roundf(256.0f * (float)sample_rate / 31250.0f);
	if(hop < 1) hop = 1;
	if(offset < 0) offset = 0;
	if(offset + (int)fft_size > (int)sp.resampled_count) {
		offset = (int)sp.resampled_count - fft_size;
		if(offset < 0) offset = 0;
	}

	// For recordings > 5s, increase hop (time decimation by power of 2)
	double duration_sec = (double)sp.resampled_count / (double)sample_rate;
	if(duration_sec > 5.0) {
		int min_hop = (int)ceilf((float)(sp.resampled_count - offset) / (float)(sp.width * 2));
		int pow2_hop = 1;
		while(pow2_hop < min_hop && pow2_hop < 65536) {
			pow2_hop *= 2;
		}
		if(pow2_hop > hop) {
			hop = pow2_hop;
		}
	}

	// Number of available frames
	int64_t available = 0;
	if(sp.resampled_count > (size_t)(offset + fft_size)) {
		available = (sp.resampled_count - offset) / hop;
	}
	int64_t total_frames = available;
	if(total_frames < sp.width) {
		total_frames = sp.width;
	}

	sp.frame_count = 0;
	sp.db_range_set = false;
	for(int64_t i = 0; i < total_frames && sp.frame_count < sp.width; i++) {
		size_t start = (size_t)(offset + i * hop);
		if(start + (size_t)fft_size > sp.resampled_count) {
			break;
		}
		memcpy(sp.fft_in, sp.resampled + start, (size_t)fft_size * sizeof(float complex));
		sp_compute_frame_range_pass();
	}
	if(!sp.db_range_set) {
		sp.db_min = -100.0f;
		sp.db_max = 0.0f;
	}
	if(sp.db_max - sp.db_min < 1.0f) {
		sp.db_max = sp.db_min + 1.0f;
	}
	float range = sp.db_max - sp.db_min;
	if(range < 20.0f) {
		sp.db_max = sp.db_min + 20.0f;
	}

	sp.frame_count = 0;
	for(int64_t i = 0; i < total_frames && sp.frame_count < sp.width; i++) {
		size_t start = (size_t)(offset + i * hop);
		if(start + (size_t)fft_size > sp.resampled_count) {
			break;
		}
		memcpy(sp.fft_in, sp.resampled + start, (size_t)fft_size * sizeof(float complex));
		sp_compute_frame();
	}

	sp.end_col = sp.frame_count;
	sp_draw_end_line();

	fprintf(stderr, "Spectrogram: wrote %d columns (hop=%d, dB range %.1f..%.1f) from %zu samples (%zu resampled)\n",
			sp.frame_count, hop, sp.db_min, sp.db_max, sp.sample_count, sp.resampled_count);

	int rc = 0;
	switch(sp.format) {
		case 1:
			rc = stbi_write_png(sp.path, sp.width, sp.height, 3, sp.image, sp.width * 3) ? 0 : -1;
			break;
		case 2:
			rc = stbi_write_jpg(sp.path, sp.width, sp.height, 3, sp.image, 85) ? 0 : -1;
			break;
		case 0:
		default:
			rc = stbi_write_bmp(sp.path, sp.width, sp.height, 3, sp.image) ? 0 : -1;
			break;
	}
	if(rc != 0) {
		fprintf(stderr, "Spectrogram: failed to write %s\n", sp.path);
	} else {
		fprintf(stderr, "Spectrogram: saved %s\n", sp.path);
	}
	return rc;
}

void spectrogram_cleanup(void) {
	if(!sp.enabled) return;
	XFREE(sp.samples);
	XFREE(sp.resampled);
	XFREE(sp.image);
	if(sp.fft_plan) csdr_destroy_fft_c2c(sp.fft_plan);
	XFREE(sp.fft_in);
	XFREE(sp.fft_out);
	XFREE(sp.path);
	memset(&sp, 0, sizeof(sp));
}
