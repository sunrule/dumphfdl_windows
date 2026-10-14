/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * output-callback.c - "callback" output driver.
 *
 * Delivers formatted messages to a user-supplied callback function
 * (function pointer). Used by the DLL API (see hfdl_api.c) to pass
 * decoded messages to the host application (eg. Delphi).
 *
 * The callback is invoked from the output thread of this driver, ie.
 * from a worker thread - not from the thread which called the API
 * entry point.
 */
#include <stdlib.h>                     // NULL
#include <string.h>                     // strlen
#include "output-common.h"              // output_descriptor_t, output_qentry_t
#include "output-callback.h"            // out_callback_set, out_callback_reset
#include "util.h"                       // ASSERT, NEW

typedef struct {
	hfdl_output_callback callback;      // user-supplied callback (may be NULL)
	void *userdata;                     // opaque pointer passed to the callback
} out_callback_ctx_t;

// Global callback registration.
// Only one callback can be active at a time. The DLL API is not
// reentrant (single decode at a time), so this is sufficient.
static hfdl_output_callback out_callback_fn = NULL;
static void *out_callback_userdata = NULL;

void out_callback_set(hfdl_output_callback cb, void *userdata) {
	out_callback_fn = cb;
	out_callback_userdata = userdata;
}

void out_callback_reset(void) {
	out_callback_fn = NULL;
	out_callback_userdata = NULL;
}

static bool out_callback_supports_format(output_format_t format) {
	return(format == OFMT_TEXT || format == OFMT_JSON || format == OFMT_BASESTATION);
}

static void *out_callback_configure(kvargs *kv) {
	UNUSED(kv);
	NEW(out_callback_ctx_t, cfg);
	cfg->callback = NULL;
	cfg->userdata = NULL;
	return cfg;
}

static void out_callback_ctx_destroy(void *ctxptr) {
	if(ctxptr != NULL) {
		XFREE(ctxptr);
	}
}

static int32_t out_callback_init(void *selfptr) {
	ASSERT(selfptr != NULL);
	return 0;
}

static int32_t out_callback_produce(void *selfptr, output_format_t format,
		struct metadata *metadata, struct octet_string *msg) {
	ASSERT(selfptr != NULL);
	UNUSED(selfptr);
	UNUSED(metadata);

	if(out_callback_fn == NULL || msg == NULL || msg->buf == NULL) {
		return 0;
	}
	// msg->buf is not guaranteed to be NUL-terminated, so make a
	// temporary NUL-terminated copy for the callback.
	// The formatted messages are short (a few hundred bytes at most),
	// so this is not performance-critical.
	size_t len = msg->len;
	char *tmp = XCALLOC(len + 1, sizeof(char));
	if(tmp == NULL) {
		return -1;
	}
	memcpy(tmp, msg->buf, len);
	tmp[len] = '\0';
	out_callback_fn(tmp, format, out_callback_userdata);
	XFREE(tmp);
	return 0;
}

static void out_callback_handle_shutdown(void *selfptr) {
	UNUSED(selfptr);
}

static void out_callback_handle_failure(void *selfptr) {
	UNUSED(selfptr);
}

output_descriptor_t out_DEF_callback = {
	.name = "callback",
	.description = "invoke a user-supplied callback function for each message",
	.options = NULL,
	.supports_format = out_callback_supports_format,
	.configure = out_callback_configure,
	.ctx_destroy = out_callback_ctx_destroy,
	.init = out_callback_init,
	.produce = out_callback_produce,
	.handle_shutdown = out_callback_handle_shutdown,
	.handle_failure = out_callback_handle_failure
};
