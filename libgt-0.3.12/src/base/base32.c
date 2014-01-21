/*
 * $Id: base32.c 74 2010-02-22 11:42:26Z ahto.truu $
 *
 * Copyright 2008-2010 GuardTime AS
 *
 * This file is part of the GuardTime client SDK.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "base32.h"

#include <openssl/crypto.h>

#include <string.h>
#include <assert.h>
#include <ctype.h>

static const char base32EncodeTable[32] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static const unsigned char base32NumDecTable[10] = {
	-1, -1, 26, 27, 28, 29, 30, 31, -1, -1
};

static int makeMask(int bit_count)
{
	int i;
	int ret = 0;

	for (i = 0; i < bit_count; i++) {
		ret <<= 1;
		ret |= 1;
	}

	return ret;
}

static void addBits(unsigned char *buf, int *bits_decoded, int bits)
{
	int bits_to_first_byte;
	int shift_count;
	int buf_idx;
	int selected_bits;

	if (bits < 0) {
		return;
	}

	bits_to_first_byte = 8 - *bits_decoded % 8;
	if (bits_to_first_byte > 5) {
		bits_to_first_byte = 5;
	}

	shift_count = 8 - bits_to_first_byte - *bits_decoded % 8;
	buf_idx = *bits_decoded / 8;
	selected_bits = (bits & (makeMask(bits_to_first_byte) <<
				(5 - bits_to_first_byte))) >> (5 - bits_to_first_byte);

	buf[buf_idx] |= selected_bits << shift_count;
	*bits_decoded += bits_to_first_byte;

	if (bits_to_first_byte < 5) {
		int bits_to_second_byte = 5 - bits_to_first_byte;

		shift_count = 8 - bits_to_second_byte;
		buf_idx++;
		selected_bits = bits & ((makeMask(bits_to_second_byte) <<
					(5 - bits_to_second_byte)) >> (5 - bits_to_second_byte));

		buf[buf_idx] |= selected_bits << shift_count;
		*bits_decoded += bits_to_second_byte;
	}
}

unsigned char* GT_base32Decode(const char *base32, int base32_len,
		size_t *ret_len)
{
	int bits_decoded = 0;
	char c;
	int i;
	unsigned char *ret = NULL;

	assert(base32 != NULL && ret_len != NULL);

	if (base32_len < 0) {
		base32_len = strlen(base32);
	}

	ret = OPENSSL_malloc(base32_len * 5 / 8 + 2);
	if (ret == NULL) {
		goto error;
	}
	memset(ret, 0, base32_len * 5 / 8 + 2);

	for (i = 0; i < base32_len; i++) {
		c = base32[i];
		if (c == '=') {
			break;
		}
		if (isdigit(c)) {
			addBits(ret, &bits_decoded, base32NumDecTable[c - '0']);
			continue;
		}
		/* It is not safe to use isalpha() here: it is locale dependent and
		 * can return true for characters that are invalid for base32 encoding.
		 */
		if ((c >= 'A' && c <= 'Z') || ((c >= 'a' && c <= 'z'))) {
			addBits(ret, &bits_decoded, toupper(c) - 'A');
			continue;
		}
	}

	/* We ignore padding errors. */

	/* This operation also truncates extra bits from the end (when input
	 * bit count was not divisible by 5). */
	*ret_len = bits_decoded / 8;
	return ret;

error:
	OPENSSL_free(ret);
	return NULL;
}

/* Returns -1 when EOF is encountered. */
static int readNextBits(const unsigned char *data, size_t data_len,
		int bits_read)
{
	int ret = 0;
	int first_byte_bits;
	size_t byte_to_read;
	int shift_count;

	byte_to_read = bits_read / 8;

	if (byte_to_read >= data_len) {
		return -1;
	}

	first_byte_bits = 8 - (bits_read - byte_to_read * 8);
	if (first_byte_bits > 5) {
		first_byte_bits = 5;
	}
	shift_count = 8 - bits_read % 8 - first_byte_bits;
	ret = (data[byte_to_read] & (makeMask(first_byte_bits) << shift_count)) >>
			shift_count;

	byte_to_read++;
	if (first_byte_bits < 5) {
		int second_byte_bits = 5 - first_byte_bits;
		ret <<= second_byte_bits;

		if (byte_to_read < data_len) {
			shift_count = 8 - second_byte_bits;
			ret |= (data[byte_to_read] & (makeMask(second_byte_bits) <<
						shift_count)) >> shift_count;
		}
	}

	return ret;
}

char* GT_base32Encode(const unsigned char *data, size_t data_len, size_t group_len)
{
	char *ret = NULL;
	int next_bits;
	size_t bits_read;
	size_t buf_len;
	size_t ret_len = 0;

	assert(data != NULL && data_len != 0);

	buf_len = (data_len * 8 + 39) / 40 * 8;
	if (group_len > 0) {
		buf_len += (buf_len - 1) / group_len;
	}
	++buf_len;

	ret = OPENSSL_malloc(buf_len);
	if (ret == NULL) {
		goto error;
	}

	for (bits_read = 0;
			(next_bits = readNextBits(data, data_len, bits_read)) != -1;
			bits_read += 5) {
		ret[ret_len++] = base32EncodeTable[next_bits];

		if (ret_len % (group_len + 1) == group_len && bits_read + 5 < data_len * 8) {
			ret[ret_len++] = '-';
		}
	}

	/* Pad output. */
	while (bits_read % 40 != 0) {
		ret[ret_len++] = '=';
		if (ret_len % (group_len + 1) == group_len && bits_read % 40 != 35) {
			ret[ret_len++] = '-';
		}
		bits_read += 5;
	}

	ret[ret_len++] = '\0';

	return ret;

error:
	OPENSSL_free(ret);
	return NULL;
}
