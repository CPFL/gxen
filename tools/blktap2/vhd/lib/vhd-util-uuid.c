 /* Copyright (c) 2008, XenSource Inc.
 * Copyright (c) 2011, Citrix
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of XenSource Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(__linux__)

#include <uuid/uuid.h>

typedef struct {
    uuid_t uuid;
} vhd_uuid_t;

int vhd_uuid_is_nil(vhd_uuid_t *uuid)
{
	return uuid_is_null(uuid->uuid);
}

void vhd_uuid_generate(vhd_uuid_t *uuid)
{
	uuid_generate(uuid->uuid);
}

void vhd_uuid_to_string(vhd_uuid_t *uuid, char *out, size_t size)
{
	uuid_unparse(uuid->uuid, out);
}

void vhd_uuid_from_string(vhd_uuid_t *uuid, const char *in)
{
	uuid_parse(in, uuid->uuid);
}

void vhd_uuid_copy(vhd_uuid_t *dst, vhd_uuid_t *src)
{
	uuid_copy(dst->uuid, src->uuid);
}

void vhd_uuid_clear(vhd_uuid_t *uuid)
{
	uuid_clear(uuid->uuid);
}

int vhd_uuid_compare(vhd_uuid_t *uuid1, vhd_uuid_t *uuid2)
{
	return uuid_compare(uuid1->uuid, uuid2->uuid);
}

#elif defined(__NetBSD__)

#include <uuid.h>
#include <string.h>
#include <stdlib.h>

typedef uuid_t vhd_uuid_t;

int vhd_uuid_is_nil(vhd_uuid_t *uuid)
{
	uint32_t status;
	return uuid_is_nil((uuid_t *)uuid, &status);
}

void vhd_uuid_generate(vhd_uuid_t *uuid)
{
	uint32_t status;
	uuid_create((uuid_t *)uuid, &status);
}

void vhd_uuid_to_string(vhd_uuid_t *uuid, char *out, size_t size)
{
	uint32_t status;
	char *_out = NULL;
	uuid_to_string((uuid_t *)uuid, &_out, &status);
	strlcpy(out, _out, size);
	free(_out);
}

void vhd_uuid_from_string(vhd_uuid_t *uuid, const char *in)
{
	uint32_t status;
	uuid_from_string(in, (uuid_t *)uuid, &status);
}

void vhd_uuid_copy(vhd_uuid_t *dst, vhd_uuid_t *src)
{
	memcpy((uuid_t *)dst, (uuid_t *)src, sizeof(uuid_t));
}

void vhd_uuid_clear(vhd_uuid_t *uuid)
{
	memset((uuid_t *)uuid, 0, sizeof(uuid_t));
}

int vhd_uuid_compare(vhd_uuid_t *uuid1, vhd_uuid_t *uuid2)
{
	uint32_t status;
	return uuid_compare((uuid_t *)uuid1, (uuid_t *)uuid2, &status);
}

#else

#error "Please update vhd-util-uuid.c for your OS"

#endif
