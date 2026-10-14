/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "output-common.h"          // output_descriptor_t, output_format_t

/*
 * Callback invoked for each decoded message.
 *
 *  msg       - NUL-terminated string with the formatted message
 *              (valid only for the duration of the call).
 *  format    - output format of the message (OFMT_TEXT, OFMT_JSON, ...).
 *  userdata  - opaque pointer registered via out_callback_set().
 *
 * Invoked from a worker thread - do not touch non-thread-safe state
 * (eg. VCL in Delphi) without synchronization.
 */
typedef void (*hfdl_output_callback)(char const *msg, output_format_t format, void *userdata);

// Register the callback to be invoked by the "callback" output driver.
// Pass cb=NULL to unregister.
void out_callback_set(hfdl_output_callback cb, void *userdata);

// Unregister the callback (called during cleanup).
void out_callback_reset(void);

extern output_descriptor_t out_DEF_callback;
