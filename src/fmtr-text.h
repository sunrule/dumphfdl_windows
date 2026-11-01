/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "output-common.h"              // fmtr_descriptor_t
#include <libacars/libacars.h>          // la_proto_node
#include "metadata.h"                   // struct metadata
#include "util.h"                       // struct octet_string

extern fmtr_descriptor_t fmtr_DEF_text;
// Экспортируем для JSON-форматтера: получить человекочитаемый текст
// для одного PDU (MPDU, LPDU или SPDU).
struct octet_string *fmtr_text_format_decoded_msg(struct metadata *metadata,
                                                   la_proto_node *root);
