/*
 * $Id: asn1_time_get.c 74 2010-02-22 11:42:26Z ahto.truu $
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

#include "asn1_time_get.h"

#include <time.h>

#include <openssl/asn1.h>

static const char days[2][12] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static int pint(const unsigned char **s, int n, int min, int max, int *e)
{
	int retval = 0;

	while (n) {
		if (**s < '0' || **s > '9') {
			*e = GT_INVALID_FORMAT;
			return 0;
		}
		retval *= 10;
		retval += **s - '0';
		--n; ++(*s);
	}

	if (retval < min || retval > max)
		*e = GT_INVALID_FORMAT;

	return retval;
}

int GT_ASN1_TIME_get(const ASN1_TIME *a, GT_Time_t64 *result)
{
	int error = GT_OK;
	const unsigned char *s;
	int generalized;
	struct tm t;
	int i, year, isleap, offset;

	if (a->type == V_ASN1_GENERALIZEDTIME) {
		generalized = 1;
	} else if (a->type == V_ASN1_UTCTIME) {
		generalized = 0;
	} else {
		error = GT_INVALID_FORMAT;
		goto done;
	}
	s = a->data; /* Data should be always null terminated. */
	if (s == NULL || s[a->length] != '\0') {
		error = GT_INVALID_FORMAT;
		goto done;
	}

	if (generalized) {
		t.tm_year = pint(&s, 4, 0, 9999, &error) - 1900;
	} else {
		t.tm_year = pint(&s, 2, 0, 99, &error);
		if (t.tm_year < 50) t.tm_year += 100;
	}
	t.tm_mon = pint(&s, 2, 1, 12, &error) - 1;
	t.tm_mday = pint(&s, 2, 1, 31, &error);
	/* NOTE: It's not yet clear, if this implementation is 100% correct
	 * for GeneralizedTime... but at least misinterpretation is impossible
	 * --- we just return an error. */
	t.tm_hour = pint(&s, 2, 0, 23, &error);
	t.tm_min = pint(&s, 2, 0, 59, &error);
	if (*s >= '0' && *s <= '9') {
		t.tm_sec = pint(&s, 2, 0, 59, &error);
	} else {
		t.tm_sec = 0;
	}
	if (error != GT_OK) {
		/* Format violation. */
		goto done;
	}
	if (generalized) {
		/* Skip fractional seconds if any. */
		while (*s == '.' || *s == ',' || (*s >= '0' && *s <= '9')) ++s;
		/* Special treatment for local time. */
		if (*s == 0) {
			t.tm_isdst = -1;
#ifdef _WIN32
			*result = _mktime64(&t);
#else
			/* NOTE: In linux we depend on the size of time_t.
			 * If it is 64-bit, we get wider range of valid dates. */
			*result = mktime(&t); /* Local time is easy. :) */
#endif
			if (*result == (time_t)-1) {
				error = GT_TIME_OVERFLOW;
			}
			goto done;
		}
	}
	if (*s == 'Z') {
		offset = 0;
		++s;
	} else if (*s == '-' || *s == '+') {
		i = (*s++ == '-');
		offset = pint(&s, 2, 0, 12, &error);
		offset *= 60;
		offset += pint(&s, 2, 0, 59, &error);
		if (error != GT_OK) {
			/* Format violation. */
			goto done;
		}
		if (i) {
			offset = -offset;
		}
	} else {
		error = GT_INVALID_FORMAT;
		goto done;
	}
	if (*s) {
		error = GT_INVALID_FORMAT;
		goto done;
	}

	/* And here comes the hardest part --- there's no standard function to
	 * convert struct tm containing UTC time into time_t without messing
	 * global timezone settings (breaks multithreading and may cause other
	 * problems) and thus we have to do this "by hand".
	 *
	 * The overflow check does not detect too big overflows, but it is
	 * sufficient thanks to the fact that year numbers are limited to four
	 * digit non-negative values. */
	*result = t.tm_sec;
	*result += (t.tm_min - offset) * 60;
	*result += t.tm_hour * 3600;
	*result += (t.tm_mday - 1) * 86400;
	year = t.tm_year + 1900;

#ifdef _WIN32
	/* On Windows the 64-bit value still does not allow dates after the
	 * year 3000. */
	if (year < 0 || year > 3000) {
		error = GT_TIME_OVERFLOW;
		goto done;
	}
#endif

	/* Earlier versions of POSIX defined all years divisible by 4
	 * to be leap years, but this was corrected in the 2001 edition of
	 * the standard.
	 */
	isleap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
	for (i = t.tm_mon - 1; i >= 0; --i) {
		*result += days[isleap][i] * 86400;
	}
	*result += ((GT_Time_t64) year - 1970) * 31536000;
	if (year < 1970) {
		*result -= ((1970 - (GT_Time_t64) year + 2) / 4) * 86400;
		if (sizeof(*result) > 4) {
			for (i = 1900; i >= year; i -= 100) {
				if (i % 400 == 0) {
					continue;
				}
				*result += 86400;
			}
		}
		if (*result >= 0) {
			error = GT_TIME_OVERFLOW;
		}
	} else {
		*result += (((GT_Time_t64) year - 1970 + 1) / 4) * 86400;
		if (sizeof(*result) > 4) {
			for (i = 2100; i < year; i += 100) {
				/* The following condition is the reason to
				 * start with 2100 instead of 2000
				 */
				if (i % 400 == 0) {
					continue;
				}
				*result -= 86400;
			}
		}
		if (*result < 0) {
			error = GT_TIME_OVERFLOW;
		}
	}

done:

	return error;
}
