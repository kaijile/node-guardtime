/*
 * $Id: asn1_time_get.h 74 2010-02-22 11:42:26Z ahto.truu $
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

#ifndef ASN1_TIME_GET_H_INCLUDED
#define ASN1_TIME_GET_H_INCLUDED

#include <openssl/asn1.h>

#include "gt_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert ASN1_TIME to time_t type.
 * Unfortunately OpenSSL does not provide such function.
 *
 * \param a \c (in)         - time in ASN1_TIME format.
 * \param result \c (out)   - as the name says, the result of convertion.
 * \return                  - GT_ error code.
 *
 * \note GT_OK is returned on success;
 * GT_INVALID_FORMAT - when input time wasn't correct;
 * GT_TIME_OVERFLOW - when overflow error occurs (time_t can't hold the
 * result).
 */
int GT_ASN1_TIME_get(const ASN1_TIME *a, GT_Time_t64 *result);

#ifdef __cplusplus
}
#endif

#endif /* not ASN1_TIME_GET_H_INCLUDED */
