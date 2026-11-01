/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "input-common.h"

#ifdef __cplusplus
extern "C" {
#endif

int spectrogram_init(struct input_cfg const *cfg);
void spectrogram_accumulate(float complex const *samples, size_t count);
int spectrogram_write(void);
void spectrogram_cleanup(void);

#ifdef __cplusplus
}
#endif
