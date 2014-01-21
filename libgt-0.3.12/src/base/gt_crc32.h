/*
 * $Id: gt_crc32.h 74 2010-02-22 11:42:26Z ahto.truu $
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

#ifndef GT_CRC32_H_INCLUDED
#define GT_CRC32_H_INCLUDED

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Calculates CRC32 checksum.
 *
 * \param data Pointer to the data.
 *
 * \param length Length of the data.
 *
 * \param ival Initial value. Pass 0 for the first or single call to this
 * function and pass result from the previous call for the next part of the
 * data.
 *
 * \return CRC32 of the data.
 */
unsigned long GT_crc32(const void *data, size_t length, unsigned long ival);

#ifdef __cplusplus
}
#endif

#endif /* not GT_CRC32_H_INCLUDED */
