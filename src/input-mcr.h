/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "input-common.h"       // struct input_vtable, struct input_cfg

// Reads the MCR file header and fills in the sample rate and center
// frequency in the input config (unless the user has overridden them).
// Returns 0 on success, -1 on error.
int32_t mcr_input_preload_config(struct input_cfg *cfg);

extern struct input_vtable mcr_input_vtable;
