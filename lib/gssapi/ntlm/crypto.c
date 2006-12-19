/*
 * Copyright (c) 2006 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "ntlm/ntlm.h"

RCSID("$Id$");

static void
encode_le_uint32(uint32_t n, unsigned char *p)
{
  p[0] = (n >> 0)  & 0xFF;
  p[1] = (n >> 8)  & 0xFF;
  p[2] = (n >> 16) & 0xFF;
  p[3] = (n >> 24) & 0xFF;
}


static void
decode_le_uint32(const void *ptr, uint32_t *n)
{
    const unsigned char *p = ptr;
    *n = (p[0] << 0) | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}


uint32_t
_krb5_crc_update (const char *p, size_t len, uint32_t res);
void
_krb5_crc_init_table(void);


OM_uint32 _gss_ntlm_get_mic
           (OM_uint32 * minor_status,
            const gss_ctx_id_t context_handle,
            gss_qop_t qop_req,
            const gss_buffer_t message_buffer,
            gss_buffer_t message_token
           )
{
    ntlm_ctx ctx = (ntlm_ctx)context_handle;
    OM_uint32 junk;

    if (minor_status)
	*minor_status = 0;
    if (message_token) {
	message_token->length = 0;
	message_token->value = NULL;
    }

    message_token->value = malloc(16);
    message_token->length = 16;
    if (message_token->value == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    if(ctx->flags & NTLM_NEG_SIGN) {
	unsigned char sigature[12];
	uint32_t crc;

	if ((ctx->status & STATUS_SESSIONKEY) == 0) {
	    gss_release_buffer(&junk, message_token);
	    return GSS_S_UNAVAILABLE;
	}

	_krb5_crc_init_table();
	crc = _krb5_crc_update(message_buffer->value, 
			       message_buffer->length, 0);
	encode_le_uint32(0, &sigature[0]);
	encode_le_uint32(crc, &sigature[4]);
	encode_le_uint32(ctx->crypto.send_seq, &sigature[8]);

	ctx->crypto.send_seq++;

	encode_le_uint32(1, message_token->value); /* version */
	RC4(&ctx->crypto.key, sizeof(sigature),
	    sigature, ((unsigned char *)message_token->value) + 4);

        return GSS_S_COMPLETE;
    } else if (ctx->flags & NTLM_NEG_ALWAYS_SIGN) {
	unsigned char *sigature;

	sigature = message_token->value;

	encode_le_uint32(1, &sigature[0]); /* version */
	encode_le_uint32(0, &sigature[4]);
	encode_le_uint32(0, &sigature[8]);
	encode_le_uint32(0, &sigature[12]);

        return GSS_S_COMPLETE;
    }

    return GSS_S_UNAVAILABLE;
}


OM_uint32
_gss_ntlm_verify_mic
           (OM_uint32 * minor_status,
            const gss_ctx_id_t context_handle,
            const gss_buffer_t message_buffer,
            const gss_buffer_t token_buffer,
            gss_qop_t * qop_state
	    )
{
    ntlm_ctx ctx = (ntlm_ctx)context_handle;

    if (qop_state != NULL)
	*qop_state = GSS_C_QOP_DEFAULT;
    *minor_status = 0;

    if (token_buffer->length != 16)
	return GSS_S_BAD_MIC;

    if(ctx->flags & NTLM_NEG_SIGN) {
	unsigned char sigature[12];
	uint32_t crc, num;

	if ((ctx->status & STATUS_SESSIONKEY) == 0)
	    return GSS_S_UNAVAILABLE;

	decode_le_uint32(token_buffer->value, &num);
	if (num != 1)
	    return GSS_S_BAD_MIC;

	RC4(&ctx->crypto.key, sizeof(sigature),
	    ((unsigned char *)token_buffer->value) + 4, sigature);

	_krb5_crc_init_table();
	crc = _krb5_crc_update(message_buffer->value, 
			       message_buffer->length, 0);
	decode_le_uint32(&sigature[0], &num);
	if (num != 0)
	    return GSS_S_BAD_MIC;
	decode_le_uint32(&sigature[4], &num);
	if (num != crc)
	    return GSS_S_BAD_MIC;
	decode_le_uint32(&sigature[8], &num);
	if (ctx->crypto.recv_seq != num)
	    return GSS_S_BAD_MIC;
	ctx->crypto.recv_seq++;

        return GSS_S_COMPLETE;
    } else if (ctx->flags & NTLM_NEG_ALWAYS_SIGN) {
	uint32_t num;
	unsigned char *p;

	p = (unsigned char*)(token_buffer->value);

	decode_le_uint32(&p[0], &num); /* version */
	if (num != 1) return GSS_S_BAD_MIC;
	decode_le_uint32(&p[4], &num);
	if (num != 0) return GSS_S_BAD_MIC;
	decode_le_uint32(&p[8], &num);
	if (num != 0) return GSS_S_BAD_MIC;
	decode_le_uint32(&p[12], &num);
	if (num != 0) return GSS_S_BAD_MIC;

        return GSS_S_COMPLETE;
    }

    return GSS_S_UNAVAILABLE;
}
