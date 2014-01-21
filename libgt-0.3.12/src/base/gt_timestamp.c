/*
 * $Id: gt_timestamp.c 177 2014-01-16 22:18:43Z ahto.truu $
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

#include "gt_base.h"

#include <assert.h>
#include <string.h>

#include "gt_publicationsfile.h"

#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/pkcs7.h>

#include "gt_internal.h"
#include "hashchain.h"
#include "base32.h"
#include "asn1_time_get.h"

#ifdef _WIN32
#define snprintf _snprintf
#endif

/**
 * This internal structure represents decoded timestamp. We cannot use PKCS7
 * from the OpenSSL directly because this would add namespace pollution to
 * the public header.
 */
struct GTTimestamp_st {
	/**
	 * This structure contains actual timestamp.
	 */
	PKCS7 *token;
	/**
	 * This member holds TSTInfo structure that is extracted from the token.
	 * It must be kept in sync with the contents of the token.
	 */
	GTTSTInfo *tst_info;
	/**
	 * Pointer to the signer info inside the token. Exists for convenience.
	 */
	PKCS7_SIGNER_INFO *signer_info;
	/**
	 * Extracted (from the token) and decoded TimeSignature. Must be kept in
	 * sync with the contents of the token.
	 */
	GTTimeSignature *time_signature;
};

/**/

static GTTimestamp* GTTimestamp_new()
{
	GTTimestamp *timestamp;

	timestamp = GT_malloc(sizeof(GTTimestamp));
	if (timestamp != NULL) {
		timestamp->token = NULL;
		timestamp->tst_info = NULL;
		timestamp->signer_info = NULL;
		timestamp->time_signature = NULL;
	}

	return timestamp;
}

/**/

void GTTimestamp_free(GTTimestamp *timestamp)
{
	if (timestamp != NULL) {
		PKCS7_free(timestamp->token);
		GTTSTInfo_free(timestamp->tst_info);
		GTTimeSignature_free(timestamp->time_signature);
		GT_free(timestamp);
	}
}

/* Internal helper that updates contents of the tst_info from the token and
 * performs trivial checks to ensure that token is in fact a proper
 * timestamp. */
static int GTTimestamp_updateTSTInfo(GTTimestamp *timestamp)
{
	int res = GT_UNKNOWN_ERROR;
	PKCS7_SIGNED *pkcs7_signed;
	ASN1_TYPE *encoded_tst_info;
	const unsigned char *d2ip;

	if (timestamp == NULL || timestamp->token == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	GTTSTInfo_free(timestamp->tst_info);
	timestamp->tst_info = NULL;

	if (!PKCS7_type_is_signed(timestamp->token)) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	if (PKCS7_get_detached(timestamp->token)) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	pkcs7_signed = timestamp->token->d.sign;

	if (OBJ_obj2nid(pkcs7_signed->contents->type) != NID_id_smime_ct_TSTInfo) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	encoded_tst_info = pkcs7_signed->contents->d.other;

	if (encoded_tst_info->type != V_ASN1_OCTET_STRING) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	d2ip = ASN1_STRING_data(encoded_tst_info->value.octet_string);
	ERR_clear_error();
	timestamp->tst_info = d2i_GTTSTInfo(NULL, &d2ip,
			ASN1_STRING_length(encoded_tst_info->value.octet_string));
	if (timestamp->tst_info == NULL) {
		res = GT_isMallocFailure() ? GT_OUT_OF_MEMORY : GT_INVALID_FORMAT;
		goto cleanup;
	}

	res = GT_OK;

cleanup:
	return res;
}

/* Internal helper that updates contents of the time_signature from the token
 * and performs trivial checks to ensure that token is in fact a proper
 * timestamp. */
static int GTTimestamp_updateTimeSignature(GTTimestamp *timestamp)
{
	int res = GT_UNKNOWN_ERROR;
	STACK_OF(PKCS7_SIGNER_INFO) *pkcs7_signer_infos;
	const unsigned char *d2ip;

	/* If OID isn't initialised we don't crash at least unless compiled
	 * in release mode where asserts are no-op. */
	assert(GT_id_gt_time_signature_alg != NULL &&
			GT_id_gt_time_signature_alg_nid != NID_undef);

	if (timestamp == NULL || timestamp->token == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	GTTimeSignature_free(timestamp->time_signature);
	timestamp->signer_info = NULL;
	timestamp->time_signature = NULL;

	if (!PKCS7_type_is_signed(timestamp->token)) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	pkcs7_signer_infos = PKCS7_get_signer_info(timestamp->token);

	/* Exactly one and only one signature must be present in timestamp
	 * according to RFC-3161. */
	if (pkcs7_signer_infos == NULL ||
			sk_PKCS7_SIGNER_INFO_num(pkcs7_signer_infos) != 1) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	timestamp->signer_info = sk_PKCS7_SIGNER_INFO_value(pkcs7_signer_infos, 0);

	if ((OBJ_obj2nid(timestamp->signer_info->digest_enc_alg->algorithm) !=
				GT_id_gt_time_signature_alg_nid) ||
			(timestamp->signer_info->digest_enc_alg->parameter != NULL &&
			 (timestamp->signer_info->digest_enc_alg->parameter->type !=
			  V_ASN1_NULL))) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	d2ip = ASN1_STRING_data(timestamp->signer_info->enc_digest);
	ERR_clear_error();
	timestamp->time_signature = d2i_GTTimeSignature(NULL, &d2ip,
			ASN1_STRING_length(timestamp->signer_info->enc_digest));
	if (timestamp->time_signature == NULL) {
		res = GT_isMallocFailure() ? GT_OUT_OF_MEMORY : GT_INVALID_FORMAT;
		goto cleanup;
	}

	res = GT_OK;

cleanup:
	return res;
}

/**/

static void GTSignedAttributeList_free(int *count, GTSignedAttribute **list)
{
	int i;

	if (*list != NULL) {
		for (i = 0; i < *count; ++i) {
			GT_free((*list)[i].attr_type);
			GT_free((*list)[i].attr_value);
		}

		GT_free(*list);
	}

	*count = 0;
	*list = NULL;
}

/**/

static void GTReferenceList_free(int *count, char ***list)
{
	int i;

	if (*list != NULL) {
		for (i = 0; i < *count; ++i) {
			GT_free((*list)[i]);
		}

		GT_free(*list);
	}

	*count = 0;
	*list = NULL;
}

/**/

void GTVerificationInfo_free(GTVerificationInfo *verification_info)
{
	if (verification_info != NULL) {
		if (verification_info->implicit_data != NULL) {
			GT_free(verification_info->implicit_data->location_name);
			GT_free(verification_info->implicit_data->public_key_fingerprint);
			GT_free(verification_info->implicit_data->publication_string);
			GT_free(verification_info->implicit_data);
		}
		if (verification_info->explicit_data != NULL) {
			GT_free(verification_info->explicit_data->content_type);
			GT_free(verification_info->explicit_data->digest_algorithm_list);
			GT_free(verification_info->explicit_data->encap_content_type);
			GT_free(verification_info->explicit_data->policy);
			GT_free(verification_info->explicit_data->hash_value);
			GT_free(verification_info->explicit_data->serial_number);
			GT_free(verification_info->explicit_data->nonce);
			GT_free(verification_info->explicit_data->issuer_name);
			GT_free(verification_info->explicit_data->certificate);
			GT_free(verification_info->explicit_data->cert_issuer_name);
			GT_free(verification_info->explicit_data->cert_serial_number);
			GTSignedAttributeList_free(
					&verification_info->explicit_data->signed_attr_count,
					&verification_info->explicit_data->signed_attr_list);
			GT_free(verification_info->explicit_data->signature_algorithm);
			GTHashEntryList_free(
					&verification_info->explicit_data->location_count,
					&verification_info->explicit_data->location_list);
			GTHashEntryList_free(
					&verification_info->explicit_data->history_count,
					&verification_info->explicit_data->history_list);
			GT_free(verification_info->explicit_data->publication_hash_value);
			GT_free(verification_info->explicit_data->pki_algorithm);
			GT_free(verification_info->explicit_data->pki_value);
			GTReferenceList_free(
					&verification_info->explicit_data->key_commitment_ref_count,
					&verification_info->explicit_data->key_commitment_ref_list);
			GTReferenceList_free(
					&verification_info->explicit_data->pub_reference_count,
					&verification_info->explicit_data->pub_reference_list);
			GT_free(verification_info->explicit_data);
		}
		GT_free(verification_info);
	}
}

/**/

int GTTimestamp_getDEREncoded(const GTTimestamp *timestamp,
		unsigned char **data, size_t *data_length)
{
	int res = GT_UNKNOWN_ERROR;
	unsigned char* tmp_data = NULL;
	int tmp_length;
	unsigned char *i2dp;

	if (timestamp == NULL || timestamp->token == NULL ||
			data == NULL || data_length == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	tmp_length = i2d_PKCS7(timestamp->token, NULL);
	if (tmp_length < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	tmp_data = GT_malloc(tmp_length);
	if (tmp_data == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	i2dp = tmp_data;
	i2d_PKCS7(timestamp->token, &i2dp);

	*data = tmp_data;
	tmp_data = NULL;
	*data_length = tmp_length;
	res = GT_OK;

cleanup:
	GT_free(tmp_data);

	return res;
}

/**/

int GTTimestamp_DERDecode(const void *data, size_t data_length,
		GTTimestamp **timestamp)
{
	int res = GT_UNKNOWN_ERROR;
	const unsigned char *d2ip;
	GTTimestamp *tmp_timestamp = NULL;
	int tmp_res;

	if (data == NULL || data_length == 0 || timestamp == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	tmp_timestamp = GTTimestamp_new();
	if (tmp_timestamp == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	d2ip = data;
	ERR_clear_error();
	tmp_timestamp->token = d2i_PKCS7(NULL, &d2ip, data_length);
	if (tmp_timestamp->token == NULL) {
		res = GT_isMallocFailure() ? GT_OUT_OF_MEMORY : GT_INVALID_FORMAT;
		goto cleanup;
	}

	tmp_res = GTTimestamp_updateTSTInfo(tmp_timestamp);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GTTimestamp_updateTimeSignature(tmp_timestamp);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	*timestamp = tmp_timestamp;
	tmp_timestamp = NULL;
	res = GT_OK;

cleanup:
	ERR_clear_error();
	GTTimestamp_free(tmp_timestamp);

	return res;
}

/** Helper function for timestamp request creation functions. */
static int makeTimestampRequestHelper(
		const GTDataHash *data_hash, GTTimeStampReq **request)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	GTTimeStampReq *tmp_request = NULL;

	assert(data_hash != NULL && data_hash->digest_length != 0);

	tmp_request = GTTimeStampReq_new();
	if (tmp_request == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	if (!ASN1_INTEGER_set(tmp_request->version, 1)) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	tmp_res = GT_calculateMessageImprint(data_hash->digest,
			data_hash->digest_length, data_hash->algorithm,
			&tmp_request->messageImprint);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	assert(tmp_request->reqPolicy == NULL);
	assert(tmp_request->nonce == NULL);
	assert(tmp_request->extensions == NULL);

	*request = tmp_request;
	tmp_request = NULL;

	res = GT_OK;

cleanup:
	GTTimeStampReq_free(tmp_request);

	return res;
}

/**/

int GTTimestamp_prepareTimestampRequest(const GTDataHash *data_hash,
		unsigned char **request_data, size_t *request_length)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	GTTimeStampReq *request = NULL;
	unsigned char *i2dp;
	unsigned char *tmp_data = NULL;
	int tmp_length;

	if (data_hash == NULL || data_hash->digest_length == 0 ||
			data_hash->digest == NULL || data_hash->context != NULL ||
			request_data == NULL || request_length == NULL ||
			GT_getHashSize(data_hash->algorithm) != data_hash->digest_length) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	tmp_res = makeTimestampRequestHelper(data_hash, &request);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_length = i2d_GTTimeStampReq(request, NULL);
	if (tmp_length < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	tmp_data = GT_malloc(tmp_length);
	if (tmp_data == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	i2dp = tmp_data;
	i2d_GTTimeStampReq(request, &i2dp);

	*request_data = tmp_data;
	tmp_data = NULL;
	*request_length = tmp_length;
	res = GT_OK;

cleanup:
	GTTimeStampReq_free(request);
	GT_free(tmp_data);

	return res;
}

/**/

int GTTimestamp_createTimestamp(const void *response, size_t response_length,
		GTTimestamp **timestamp)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	const unsigned char *d2ip;
	GTTimeStampResp *resp = NULL;
	GTTimestamp *tmp_timestamp = NULL;

	if (response == NULL || response_length == 0 || timestamp == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	d2ip = response;
	ERR_clear_error();
	resp = d2i_GTTimeStampResp(NULL, &d2ip, response_length);
	if (resp == NULL) {
		res = GT_isMallocFailure() ? GT_OUT_OF_MEMORY : GT_INVALID_FORMAT;
		goto cleanup;
	}

	tmp_res = GT_analyseResponseStatus(resp->status);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	if (resp->timeStampToken == NULL) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	/* It's up to verification functions to check version and extension
	 * compatibility. */
	tmp_timestamp = GTTimestamp_new();
	if (tmp_timestamp == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	/* Move timeStampToken value instead of copying for efficiency --- decoded
	 * response is not used anymore anyway. */
	tmp_timestamp->token = resp->timeStampToken;
	resp->timeStampToken = NULL;

	tmp_res = GTTimestamp_updateTSTInfo(tmp_timestamp);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GTTimestamp_updateTimeSignature(tmp_timestamp);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	*timestamp = tmp_timestamp;
	tmp_timestamp = NULL;

	res = GT_OK;

cleanup:
	ERR_clear_error();
	GTTimeStampResp_free(resp);
	GTTimestamp_free(tmp_timestamp);

	return res;
}

/* Helper function for extend request creation functions. */
static int makeExtensionRequest(
		const GTTimeSignature *time_signature, GTCertTokenRequest **request)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	GTCertTokenRequest *tmp_request = NULL;
	ASN1_OCTET_STRING *history_shape = NULL;

	assert(time_signature != NULL);

	tmp_request = GTCertTokenRequest_new();
	if (tmp_request == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	if (!ASN1_INTEGER_set(tmp_request->version, 1)) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	tmp_res = GT_shape(time_signature->history, &history_shape);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	ASN1_INTEGER_free(tmp_request->historyIdentifier);
	tmp_request->historyIdentifier = NULL;
	tmp_res = GT_findHistoryIdentifier(
			time_signature->publishedData->publicationIdentifier,
			history_shape, &tmp_request->historyIdentifier, NULL);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	assert(tmp_request->extensions == NULL);

	*request = tmp_request;
	tmp_request = NULL;

	res = GT_OK;

cleanup:
	GTCertTokenRequest_free(tmp_request);
	ASN1_OCTET_STRING_free(history_shape);

	return res;
}

/**/

int GTTimestamp_prepareExtensionRequest(const GTTimestamp *timestamp,
		unsigned char **request_data, size_t *request_length)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	GTCertTokenRequest *request = NULL;
	unsigned char *i2dp;
	unsigned char *tmp_data = NULL;
	int tmp_length;

	if (timestamp == NULL || timestamp->token == NULL ||
			timestamp->tst_info == NULL || timestamp->time_signature == NULL ||
			request_data == NULL || request_length == NULL) {
		return GT_INVALID_ARGUMENT;
	}

	tmp_res = makeExtensionRequest(timestamp->time_signature, &request);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_length = i2d_GTCertTokenRequest(request, NULL);
	if (tmp_length < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	tmp_data = GT_malloc(tmp_length);
	if (tmp_data == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	i2dp = tmp_data;
	i2d_GTCertTokenRequest(request, &i2dp);

	*request_data = tmp_data;
	tmp_data = NULL;
	*request_length = tmp_length;
	res = GT_OK;

cleanup:
	GTCertTokenRequest_free(request);
	GT_free(tmp_data);

	return res;
}

/**/

int GTTimestamp_createExtendedTimestamp(const GTTimestamp *timestamp,
		const void *response, size_t response_length,
		GTTimestamp **extended_timestamp)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	const unsigned char *d2ip;
	GTCertTokenResponse *resp = NULL;
	GTTimeSignature *extended_time_signature = NULL;
	GTTimestamp *tmp_timestamp = NULL;
	PKCS7_SIGNER_INFO *pkcs7_signer_info;

	if (timestamp == NULL || timestamp->token == NULL ||
			timestamp->tst_info == NULL || timestamp->time_signature == NULL ||
			response == NULL || response_length == 0 ||
			extended_timestamp == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	d2ip = response;
	ERR_clear_error();
	resp = d2i_GTCertTokenResponse(NULL, &d2ip, response_length);
	if (resp == NULL) {
		res = GT_isMallocFailure() ? GT_OUT_OF_MEMORY : GT_INVALID_FORMAT;
		goto cleanup;
	}

	tmp_res = GT_analyseResponseStatus(resp->status);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	if (resp->certToken == NULL) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	if (ASN1_INTEGER_get(resp->certToken->version) != 1) {
		res = GT_UNSUPPORTED_FORMAT;
		goto cleanup;
	}

	tmp_res = GT_checkUnhandledExtensions(resp->certToken->extensions);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	/* It's not any more our problem here to make sure that we dont try to
	 * extend invalid or unsupported short-term timestamp. */

	tmp_res = GT_extendConsistencyCheck(
			timestamp->time_signature, resp->certToken);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GT_extendTimeSignature(
			timestamp->time_signature, resp->certToken, NULL,
			&extended_time_signature);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_timestamp = GTTimestamp_new();
	if (tmp_timestamp == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	assert(tmp_timestamp->token == NULL);

	tmp_timestamp->token = PKCS7_dup(timestamp->token);
	if (tmp_timestamp->token == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	/* These should be already verified by the GTTimestamp_update*()
	 * functions. */
	assert(PKCS7_type_is_signed(tmp_timestamp->token));
	assert(sk_PKCS7_SIGNER_INFO_num(
				PKCS7_get_signer_info(tmp_timestamp->token)) == 1);

	/* Replace time signature in signer info. */
	pkcs7_signer_info = sk_PKCS7_SIGNER_INFO_value(
			PKCS7_get_signer_info(tmp_timestamp->token), 0);
	ERR_clear_error();
	if (ASN1_item_pack(extended_time_signature,
				ASN1_ITEM_rptr(GTTimeSignature),
				&pkcs7_signer_info->enc_digest) == NULL) {
		res = GT_isMallocFailure() ? GT_OUT_OF_MEMORY : GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	/* Remove certificates (not needed for extended timestamp). */
	sk_X509_pop_free(tmp_timestamp->token->d.sign->cert, X509_free);
	tmp_timestamp->token->d.sign->cert = NULL;

	tmp_res = GTTimestamp_updateTSTInfo(tmp_timestamp);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GTTimestamp_updateTimeSignature(tmp_timestamp);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	*extended_timestamp = tmp_timestamp;
	tmp_timestamp = NULL;

	res = GT_OK;

cleanup:
	GTTimeSignature_free(extended_time_signature);
	GTCertTokenResponse_free(resp);
	GTTimestamp_free(tmp_timestamp);

	return res;
}

/**/

int GTTimestamp_getAlgorithm(const GTTimestamp *timestamp, int *algorithm)
{
	int hash_alg;
	const GTMessageImprint *message_imprint;

	if (timestamp == NULL || timestamp->token == NULL ||
			timestamp->tst_info == NULL || algorithm == NULL) {
		return GT_INVALID_ARGUMENT;
	}

	message_imprint = timestamp->tst_info->messageImprint;

	hash_alg = GT_EVPToHashChainID(EVP_get_digestbyobj(
				message_imprint->hashAlgorithm->algorithm));

	if (hash_alg < 0) {
		return GT_UNTRUSTED_HASH_ALGORITHM;
	}

	if (message_imprint->hashAlgorithm->parameter != NULL &&
			(ASN1_TYPE_get(message_imprint->hashAlgorithm->parameter) !=
			 V_ASN1_NULL)) {
		return GT_UNTRUSTED_HASH_ALGORITHM;
	}

	*algorithm = hash_alg;

	return GT_OK;
}

/**/

int GTTimestamp_isExtended(const GTTimestamp *timestamp)
{
	if (timestamp == NULL || timestamp->time_signature == NULL) {
		return GT_INVALID_ARGUMENT;
	}

	if (timestamp->time_signature->pkSignature == NULL) {
		return GT_EXTENDED;
	} else {
		return GT_NOT_EXTENDED;
	}
}

/**/

int GTTimestamp_isEarlierThan(const GTTimestamp *this_timestamp,
		const GTTimestamp *that_timestamp)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	GT_HashDBIndex idx1;
	GT_HashDBIndex idx2;
	ASN1_OCTET_STRING *shape1 = NULL;
	ASN1_OCTET_STRING *shape2 = NULL;

	if (this_timestamp == NULL || this_timestamp->token == NULL ||
			this_timestamp->tst_info == NULL ||
			this_timestamp->time_signature == NULL ||
			that_timestamp == NULL || that_timestamp->token == NULL ||
			that_timestamp->tst_info == NULL ||
			that_timestamp->time_signature == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	tmp_res = GT_shape(this_timestamp->time_signature->history, &shape1);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GT_findHistoryIdentifier(
			(this_timestamp->time_signature->
			 publishedData->publicationIdentifier),
			shape1, NULL, &idx1);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GT_shape(that_timestamp->time_signature->history, &shape2);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GT_findHistoryIdentifier(
			(that_timestamp->time_signature->
			 publishedData->publicationIdentifier),
			shape2, NULL, &idx2);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	if (idx1 < idx2) {
		res = GT_EARLIER;
	} else {
		res = GT_NOT_EARLIER;
	}

cleanup:
	ASN1_OCTET_STRING_free(shape1);
	ASN1_OCTET_STRING_free(shape2);

	return res;
}

/* Converts the last num bits of buf[0..len-1] into an unsigned int.
 * Expects the bits to be listed starting from the least significant. */
static unsigned collectBits(const unsigned char buf[], int *len, int num)
{
	unsigned res = 0;
	assert(len != NULL);
	assert(*len >= num);
	assert(num <= 8 * sizeof(res));
	while (num-- > 0 && *len > 0) {
		res <<= 1;
		res |= buf[--*len];
	}
	return res;
}

/* Checks if the hash step embeds a name tag in the sibling hash.
   If it does, extracts the name and removes the step. */
static void checkName(const unsigned char *steps[], int *len,
		const unsigned char **name, int *name_len)
{
	const size_t hash_len = GT_getHashSize(GT_HASHALG_SHA224);
	const unsigned char *step;
	size_t i;
	assert(len != NULL);
	assert(*len >= 0);
	if (*len <= 0) {
		/* No hash step. */
		return;
	}
	step = steps[*len - 1];
	if (step[1] != 1) {
		/* Sibling not on the right. */
		return;
	}
	if (step[2] != GT_HASHALG_SHA224) {
		/* Sibling not SHA-224. */
		return;
	}
	if (step[3 + 0] != 0) {
		/* First byte of sibling hash value not the tag value 0. */
		return;
	}
	if ((size_t) step[3 + 1] + 2 > hash_len) {
		/* Second byte of sibling hash value not a valid name length. */
		return;
	}
	for (i = 2 + step[3 + 1]; i < hash_len; ++i) {
		if (step[3 + i] != 0) {
			/* Name not properly padded. */
			return;
		}
	}
	*name = step + 3 + 2;
	*name_len = step[3 + 1];
	--*len;
}

/* Verification helper. Extracts location ID and name from the given
 * location hash chain. */
static int extractLocation(const ASN1_OCTET_STRING *hash_chain,
		GT_UInt64 *location_id, unsigned char **location_name)
{
	static const int hasher = 80;
	static const int gdepth_top = 60;
	static const int gdepth_national = 39;
	static const int gdepth_state = 19;

	static const int slot_bits_top = 3;
	static const int ab_bits_top = 3;
	static const int slot_bits_national = 2;
	static const int ab_bits_national = 3;
	static const int slot_bits_state = 2;
	static const int ab_bits_state = 2;

	const int top_level = gdepth_top + (slot_bits_top + ab_bits_top) - 2;
	const int national_level = gdepth_national + (slot_bits_national + ab_bits_national) - 2;
	const int state_level = gdepth_state + (slot_bits_state + ab_bits_state) - 2;

	static const unsigned char *name_sep = " : ";

	const size_t name_sep_len = strlen(name_sep);
	const size_t no_name_len = strlen("[00000]");

	GT_UInt64 tmp_id;
	unsigned char *tmp_name = NULL;

	struct LocationInfo {
		unsigned hasher;
		unsigned national_cluster;
		unsigned national_machine;
		unsigned national_slot;
		const unsigned char *national_name;
		unsigned national_name_len;
		unsigned state_cluster;
		unsigned state_machine;
		unsigned state_slot;
		const unsigned char *state_name;
		unsigned state_name_len;
		unsigned local_cluster;
		unsigned local_machine;
		unsigned local_slot;
		const unsigned char *local_name;
		unsigned local_name_len;
		unsigned client_id;
		const unsigned char *client_name;
		unsigned client_name_len;
	} loc = {0};

	const unsigned char *steps[256];
	unsigned char bits[256];
	int num_bits = 0;

	int res = GT_UNKNOWN_ERROR;
	const unsigned char *p = hash_chain->data;

	unsigned char hash_bit;
	int hash_size;
	int hash_level;
	int last_level = -1;

	for ( ; ; ) {
		steps[num_bits] = p; /* remember the beginning of the step */
		p += 1; /* now we point to the direction byte */
		if (p - hash_chain->data >= hash_chain->length) {
			res = GT_INVALID_LINKING_INFO;
			goto cleanup;
		}
		if (*p != 0 && *p != 1) {
			res = GT_INVALID_LINKING_INFO;
			goto cleanup;
		}
		hash_bit = (1 - *p);
		p += 1; /* now we point to the hash algorithm byte */
		if (p - hash_chain->data >= hash_chain->length) {
			res = GT_INVALID_LINKING_INFO;
			goto cleanup;
		}
		hash_size = GT_getHashSize(*p);
		if (hash_size == 0) {
			res = GT_INVALID_LINKING_INFO;
			goto cleanup;
		}
		p += 1 + hash_size; /* now we point to the level byte */
		if (p - hash_chain->data >= hash_chain->length) {
			res = GT_INVALID_LINKING_INFO;
			goto cleanup;
		}
		hash_level = *p;
		p += 1; /* now we point to the beginning of the next entry */

		if (hash_level > hasher && last_level <= hasher) {
			if (hash_level == 0xff) {
				/* old, 2007-2011 core architecture: exactly two hashers;
				 * direction bit of last hashing step shows, which one */
				loc.hasher = 1 + hash_bit;
			} else {
				/* new, 2011+ core architecture: any number of hashers;
				 * first sufficiently high level value shows, which one;
				 * remaining steps ignored in id extraction */
				loc.hasher = hash_level - hasher;
			}
			loc.national_cluster = collectBits(bits, &num_bits, num_bits);
			break;
		}
		if (hash_level > top_level && last_level <= top_level) {
			loc.national_machine = collectBits(bits, &num_bits, ab_bits_top);
			loc.national_slot = collectBits(bits, &num_bits, slot_bits_top);
			checkName(steps, &num_bits, &loc.national_name, &loc.national_name_len);
			loc.state_cluster = collectBits(bits, &num_bits, num_bits);
		}
		if (hash_level > national_level && last_level <= national_level) {
			loc.state_machine = collectBits(bits, &num_bits, ab_bits_national);
			loc.state_slot = collectBits(bits, &num_bits, slot_bits_national);
			checkName(steps, &num_bits, &loc.state_name, &loc.state_name_len);
			loc.local_cluster = collectBits(bits, &num_bits, num_bits);
		}
		if (hash_level > state_level && last_level <= state_level) {
			loc.local_machine = collectBits(bits, &num_bits, ab_bits_state);
			loc.local_slot = collectBits(bits, &num_bits, slot_bits_state);
			checkName(steps, &num_bits, &loc.local_name, &loc.local_name_len);
			loc.client_id = collectBits(bits, &num_bits, num_bits);
		}
		if (hash_level > 1 && last_level <= 1) {
			checkName(steps, &num_bits, &loc.client_name, &loc.client_name_len);
		}

		last_level = hash_level;
		bits[num_bits++] = hash_bit;
	}
/*
	printf("H%x.N%x/%x:%x.S%x/%x:%x.L%x/%x:%x.T%x\n", loc.hasher,
		loc.national_cluster, loc.national_machine, loc.national_slot,
		loc.state_cluster, loc.state_machine, loc.state_slot,
		loc.local_cluster, loc.local_machine, loc.local_slot,
		loc.client_id);
//*/

	tmp_id = 0;
	tmp_id |= loc.national_cluster;
	tmp_id <<= 16;
	tmp_id |= loc.state_cluster;
	tmp_id <<= 16;
	tmp_id |= loc.local_cluster;
	tmp_id <<= 16;
	tmp_id |= loc.client_id;

	if (loc.national_name_len + loc.state_name_len + loc.local_name_len +
			loc.client_name_len > 0) {
		size_t len;

		len = 0;
		if (loc.national_name_len > 0) {
			len += loc.national_name_len;
		} else {
			len += no_name_len;
		}
		len += name_sep_len;
		if (loc.state_name_len > 0) {
			len += loc.state_name_len;
		} else {
			len += no_name_len;
		}
		len += name_sep_len;
		if (loc.local_name_len > 0) {
			len += loc.local_name_len;
		} else {
			len += no_name_len;
		}
		if (loc.client_name_len > 0) {
			len += name_sep_len;
			len += loc.client_name_len;
		}
		len += 1;

		tmp_name = GT_malloc(len);
		if (tmp_name == NULL) {
			res = GT_OUT_OF_MEMORY;
			goto cleanup;
		}

		len = 0;
		if (loc.national_name_len > 0) {
			memcpy(tmp_name + len, loc.national_name, loc.national_name_len);
			len += loc.national_name_len;
		} else {
			len += sprintf(tmp_name + len, "[%d]", loc.national_cluster);
		}
		memcpy(tmp_name + len, name_sep, name_sep_len);
		len += name_sep_len;
		if (loc.state_name_len > 0) {
			memcpy(tmp_name + len, loc.state_name, loc.state_name_len);
			len += loc.state_name_len;
		} else {
			len += sprintf(tmp_name + len, "[%d]", loc.state_cluster);
		}
		memcpy(tmp_name + len, name_sep, name_sep_len);
		len += name_sep_len;
		if (loc.local_name_len > 0) {
			memcpy(tmp_name + len, loc.local_name, loc.local_name_len);
			len += loc.local_name_len;
		} else {
			len += sprintf(tmp_name + len, "[%d]", loc.local_cluster);
		}
		if (loc.client_name_len > 0) {
			memcpy(tmp_name + len, name_sep, name_sep_len);
			len += name_sep_len;
			memcpy(tmp_name + len, loc.client_name, loc.client_name_len);
			len += loc.client_name_len;
		}
		tmp_name[len] = 0;
	}

	*location_id = tmp_id;
	*location_name = tmp_name;
	tmp_name = NULL;
	res = GT_OK;

cleanup:
	GT_free(tmp_name);

	return res;
}

/* Helper function to convert OID to string. */
static int oidToString(const ASN1_OBJECT *oid, char **oid_str)
{
	int res = GT_UNKNOWN_ERROR;
	int oid_str_len;
	char *tmp_oid_str = NULL;

	assert(oid != NULL);
	assert(oid_str != NULL);

	oid_str_len = OBJ_obj2txt(NULL, 0, oid, 1);
	if (oid_str_len < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	tmp_oid_str = GT_malloc(oid_str_len + 1);
	if (tmp_oid_str == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	OBJ_obj2txt(tmp_oid_str, oid_str_len + 1, oid, 1);

	res = GT_OK;
	*oid_str = tmp_oid_str;
	tmp_oid_str = NULL;

cleanup:
	GT_free(tmp_oid_str);

	return res;
}

/* Verification helper. Sets value of verifiedSignatureInfo to
 * PKISignatureInfo. */
static int setVerifiedPKISignatureInfo(
		const GTTimestamp *timestamp,
		GTVerificationInfo *verification_info)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	GTPublishedData *published_data = NULL;
	X509 *certificate;
	unsigned char *cert_der = NULL;
	int cert_der_len;
	char *tmp_cert = NULL;
	GT_Time_t64 key_pub_time;
	unsigned char *key_der = NULL;
	int key_der_len;

	assert(timestamp != NULL);
	assert(verification_info != NULL);
	assert(verification_info->implicit_data != NULL);
	assert(timestamp->time_signature->pkSignature != NULL);

	certificate = PKCS7_cert_from_signer_info(
			timestamp->token, timestamp->signer_info);
	if (certificate == NULL) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	assert(key_der == NULL);
	key_der_len = i2d_X509_PUBKEY(certificate->cert_info->key, &key_der);
	if (key_der_len < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	published_data = GTPublishedData_new();
	if (published_data == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	/* TODO: We use notBefore field from certificate validity info as
	 * publication date for now. However, this might be incorrect behaviour
	 * and needs to be verified ASAP. */
	tmp_res = GT_ASN1_TIME_get(
			certificate->cert_info->validity->notBefore, &key_pub_time);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	if (!GT_uint64ToASN1Integer(
				published_data->publicationIdentifier, key_pub_time)) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	/* TODO: We use hardcoded SHA256 for calculating of the public key hash
	 * for now. However, this does not seem to be correct solution because
	 * publications file is not limited to SHA256. One possible solution
	 * would be to use the same hash algorithm that was used for signing
	 * of the certificate but this need prior agreement because the current
	 * hardcoded SHA256 works too and is much easier to implement as a
	 * temporary workaround. */
	ASN1_OCTET_STRING_free(published_data->publicationImprint);
	published_data->publicationImprint = NULL;
	tmp_res = GT_calculateDataImprint(
			key_der, key_der_len, GT_HASHALG_SHA256,
			&published_data->publicationImprint);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	assert(verification_info->implicit_data->public_key_fingerprint == NULL);
	tmp_res = GT_publishedDataToBase32(published_data,
			&verification_info->implicit_data->public_key_fingerprint);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	if (verification_info->explicit_data != NULL) {
		assert(verification_info->explicit_data->certificate == NULL);
		assert(cert_der == NULL);

		cert_der_len = i2d_X509(certificate, &cert_der);
		if (cert_der_len < 0) {
			res = GT_CRYPTO_FAILURE;
			goto cleanup;
		}

		tmp_cert = GT_base32Encode(cert_der, cert_der_len, 8);
		if (tmp_cert == NULL) {
			res = GT_OUT_OF_MEMORY;
			goto cleanup;
		}

		/* This string duplication is necessary because we dont want to return
		 * OPENSSL_malloc()-ed data in public API. */
		verification_info->explicit_data->certificate =
			GT_malloc(strlen(tmp_cert) + 1);
		if (verification_info->explicit_data->certificate == NULL) {
			res = GT_OUT_OF_MEMORY;
			goto cleanup;
		}
		strcpy(verification_info->explicit_data->certificate, tmp_cert);

		tmp_res = oidToString(
				(timestamp->time_signature->pkSignature->
				 signatureAlgorithm->algorithm),
				&verification_info->explicit_data->pki_algorithm);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}

		tmp_res = GT_hexEncode(
				ASN1_STRING_data(
					timestamp->time_signature->pkSignature->signatureValue),
				ASN1_STRING_length(
					timestamp->time_signature->pkSignature->signatureValue),
				&verification_info->explicit_data->pki_value);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}
	}

	res = GT_OK;

cleanup:
	GTPublishedData_free(published_data);
	OPENSSL_free(key_der);
	OPENSSL_free(cert_der);
	OPENSSL_free(tmp_cert);

	return res;
}

/* Verification helper. Sets value of verifiedSignatureInfo to
 * PublicationSignatureInfo. */
static int setVerifiedPublicationSignatureInfo(
		const GTTimestamp *timestamp,
		GTVerificationInfo *verification_info)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;

	assert(timestamp != NULL);
	assert(verification_info != NULL);
	assert(verification_info->implicit_data != NULL);
	assert(timestamp->time_signature->pkSignature == NULL);

	assert(verification_info->implicit_data->publication_string == NULL);
	tmp_res = GT_publishedDataToBase32(
			timestamp->time_signature->publishedData,
			&verification_info->implicit_data->publication_string);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	res = GT_OK;

cleanup:
	return res;
}

/**/

static int GTSignedAttributeList_set(
		int *count, GTSignedAttribute **list,
		const STACK_OF(X509_ATTRIBUTE) *attrs)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	int tmp_count = 0;
	GTSignedAttribute *tmp_list = NULL;
	int i;
	const X509_ATTRIBUTE *attr;
	unsigned char *tmp_der = NULL;
	unsigned char *p;
	int tmp_der_len;

	if (count == NULL || list == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	if (attrs == NULL || sk_X509_ATTRIBUTE_num(attrs) == 0) {
		/* Empty attribute list. */
		GTSignedAttributeList_free(count, list);
		res = GT_OK;
		goto cleanup;
	}

	tmp_list =
		GT_malloc(sizeof(GTSignedAttribute) * sk_X509_ATTRIBUTE_num(attrs));
	if (tmp_list == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}
	tmp_count = sk_X509_ATTRIBUTE_num(attrs);

	for (i = 0; i < tmp_count; ++i) {
		tmp_list[i].attr_type = NULL;
		tmp_list[i].attr_value = NULL;
	}

	for (i = 0; i < tmp_count; ++i) {
		attr = sk_X509_ATTRIBUTE_value(attrs, i);

		tmp_res = oidToString(attr->object, &tmp_list[i].attr_type);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}

		/* Note that we make assumptions on the internal structure of the
		 * X509_ATTRIBUTE here. */
		assert(tmp_der == NULL);
		if (attr->single) {
			/* This is actually compatibility case for broken encodings and
			 * should never happen in case of GuardTime timestamps. */
			tmp_der_len = i2d_ASN1_TYPE(attr->value.single, &tmp_der);
		} else {
			/* Oops... i2d_ASN1_SET does not support new convenience method
			 * where i2d_Foo allocates memory itself if initial value of the
			 * given pointer is null. */
			tmp_der_len = i2d_ASN1_SET_OF_ASN1_TYPE(
					attr->value.set, NULL, i2d_ASN1_TYPE,
					V_ASN1_SET, V_ASN1_UNIVERSAL, IS_SET);
			if (tmp_der_len >= 0) {
				tmp_der = OPENSSL_malloc(tmp_der_len);
				if (tmp_der == NULL) {
					res = GT_OUT_OF_MEMORY;
					goto cleanup;
				}
				p = tmp_der;
				tmp_der_len = i2d_ASN1_SET_OF_ASN1_TYPE(
						attr->value.set, &p, i2d_ASN1_TYPE,
						V_ASN1_SET, V_ASN1_UNIVERSAL, IS_SET);
			}
		}
		if (tmp_der_len < 0) {
			res = GT_CRYPTO_FAILURE;
			goto cleanup;
		}

		tmp_res = GT_hexEncode(tmp_der, tmp_der_len, &tmp_list[i].attr_value);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}

		OPENSSL_free(tmp_der);
		tmp_der = NULL;
	}

	GTSignedAttributeList_free(count, list);
	*list = tmp_list;
	*count = tmp_count;
	tmp_list = NULL;
	tmp_count = 0;
	res = GT_OK;

cleanup:
	OPENSSL_free(tmp_der);
	GTSignedAttributeList_free(&tmp_count, &tmp_list);

	return res;
}

/**/

static int GTReferenceList_set(
		int *count, char ***list, const GTReferences *references)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	int tmp_count = 0;
	char **tmp_list = NULL;
	int i;
	ASN1_OCTET_STRING *ref;

	if (count == NULL || list == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	if (references == NULL || sk_ASN1_OCTET_STRING_num(references) == 0) {
		/* Empty reference list. */
		GTReferenceList_free(count, list);
		res = GT_OK;
		goto cleanup;
	}

	tmp_list = GT_malloc(sizeof(char*) * sk_ASN1_OCTET_STRING_num(references));
	if (tmp_list == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}
	tmp_count = sk_ASN1_OCTET_STRING_num(references);

	for (i = 0; i < tmp_count; ++i) {
		tmp_list[i] = NULL;
	}

	for (i = 0; i < tmp_count; ++i) {
		ref = sk_ASN1_OCTET_STRING_value(references, i);

		if (ASN1_STRING_length(ref) < 2 ||
				ASN1_STRING_data(ref)[0] != 0 ||
				ASN1_STRING_data(ref)[1] != 1) {
			/* Unsupported reference type, use just hexdump. */
			tmp_res = GT_hexEncode(
					ASN1_STRING_data(ref), ASN1_STRING_length(ref),
					&tmp_list[i]);
			if (tmp_res != GT_OK) {
				res = tmp_res;
				goto cleanup;
			}
		} else {
			/* UTF-8 encoded reference. */
			tmp_list[i] = GT_malloc(ASN1_STRING_length(ref) - 2 + 1);
			if (tmp_list[i] == NULL) {
				res = GT_OUT_OF_MEMORY;
				goto cleanup;
			}
			memcpy(tmp_list[i],
					ASN1_STRING_data(ref) + 2, ASN1_STRING_length(ref) - 2);
			tmp_list[i][ASN1_STRING_length(ref) - 2] = '\0';
		}
	}

	GTReferenceList_free(count, list);
	*list = tmp_list;
	*count = tmp_count;
	tmp_list = NULL;
	tmp_count = 0;
	res = GT_OK;

cleanup:
	GTReferenceList_free(&tmp_count, &tmp_list);

	return res;
}

/* Verification helper. Adds explicit data structure to the verification info
 * and sets most of the values. */
static int addExplicitVerificationInfo(
		const GTTimestamp *timestamp,
		GTVerificationInfo *verification_info)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	int i;
	GTTimeStampExplicit *explicit_data;
	PKCS7_SIGNED *pkcs7_signed;
	const GTPublishedData *published_data;
	int sec;
	int millis;
	GT_UInt64 tmp_uint64;
	char *tmp_str = NULL;
	BIO *tmp_bio = NULL;
	char *mem_data;
	long mem_len;

	assert(timestamp != NULL);
	assert(verification_info != NULL);
	assert(verification_info->explicit_data == NULL);

	verification_info->explicit_data = GT_malloc(sizeof(GTTimeStampExplicit));
	if (verification_info->explicit_data == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	explicit_data = verification_info->explicit_data;

	explicit_data->content_type = NULL;
	explicit_data->signed_data_version = -1;
	explicit_data->digest_algorithm_count = 0;
	explicit_data->digest_algorithm_list = NULL;
	explicit_data->encap_content_type = NULL;
	explicit_data->tst_info_version = -1;
	explicit_data->policy = NULL;
	explicit_data->hash_algorithm = -1;
	explicit_data->hash_value = NULL;
	explicit_data->serial_number = NULL;
	explicit_data->issuer_request_time = -1;
	explicit_data->issuer_accuracy = -1;
	explicit_data->nonce = NULL;
	explicit_data->issuer_name = NULL;
	explicit_data->certificate = NULL;
	explicit_data->signer_info_version = -1;
	explicit_data->cert_issuer_name = NULL;
	explicit_data->cert_serial_number = NULL;
	explicit_data->digest_algorithm = -1;
	explicit_data->signed_attr_count = 0;
	explicit_data->signed_attr_list = NULL;
	explicit_data->signature_algorithm = NULL;
	explicit_data->location_count = 0;
	explicit_data->location_list = NULL;
	explicit_data->history_count = 0;
	explicit_data->history_list = NULL;
	explicit_data->publication_identifier = -1;
	explicit_data->publication_hash_algorithm = -1;
	explicit_data->publication_hash_value = NULL;
	explicit_data->pki_algorithm = NULL;
	explicit_data->pki_value = NULL;
	explicit_data->key_commitment_ref_count = 0;
	explicit_data->key_commitment_ref_list = NULL;
	explicit_data->pub_reference_count = 0;
	explicit_data->pub_reference_list = NULL;

	tmp_res = oidToString(timestamp->token->type,
			&explicit_data->content_type);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	assert(PKCS7_type_is_signed(timestamp->token));
	pkcs7_signed = timestamp->token->d.sign;

	explicit_data->signed_data_version =
		ASN1_INTEGER_get(pkcs7_signed->version);

	explicit_data->digest_algorithm_count =
		sk_X509_ALGOR_num(pkcs7_signed->md_algs);

	explicit_data->digest_algorithm_list =
		GT_malloc(sizeof(int) * explicit_data->digest_algorithm_count);
	if (explicit_data->digest_algorithm_list == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	for (i = 0; i < explicit_data->digest_algorithm_count; ++i) {
		explicit_data->digest_algorithm_list[i] =
			GT_EVPToHashChainID(
					EVP_get_digestbyobj(
						sk_X509_ALGOR_value(
							pkcs7_signed->md_algs, i)->algorithm));
	}

	tmp_res = oidToString(pkcs7_signed->contents->type,
			&explicit_data->encap_content_type);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	explicit_data->tst_info_version =
		ASN1_INTEGER_get(timestamp->tst_info->version);

	tmp_res = oidToString(timestamp->tst_info->policy,
			&explicit_data->policy);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	explicit_data->hash_algorithm = GT_EVPToHashChainID(
			EVP_get_digestbyobj(timestamp->tst_info->
				messageImprint->hashAlgorithm->algorithm));
	if (explicit_data->hash_algorithm < 0) {
		/* Unsupported hash algorithm is invalid. */
		verification_info->verification_errors |= GT_SYNTACTIC_CHECK_FAILURE;
	}

	tmp_res = GT_hexEncode(
			timestamp->tst_info->messageImprint->hashedMessage->data,
			timestamp->tst_info->messageImprint->hashedMessage->length,
			&explicit_data->hash_value);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	/* Note that the following code relies on the internal representation
	 * of the ASN1_INTEGER structure. */
	if (timestamp->tst_info->serialNumber->type != V_ASN1_INTEGER) {
		/* Negative values are invalid. */
		verification_info->verification_errors |= GT_SYNTACTIC_CHECK_FAILURE;
	}
	tmp_res = GT_hexEncode(
			timestamp->tst_info->serialNumber->data,
			timestamp->tst_info->serialNumber->length,
			&explicit_data->serial_number);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GT_ASN1_TIME_get(
			timestamp->tst_info->genTime,
			&explicit_data->issuer_request_time);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GT_getAccuracy(
			timestamp->tst_info->accuracy, &sec, &millis, NULL);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}
	explicit_data->issuer_accuracy = 1000 * sec + millis;

	if (timestamp->tst_info->nonce != NULL) {
		tmp_res = GT_hexEncode(
				timestamp->tst_info->nonce->data,
				timestamp->tst_info->nonce->length,
				&explicit_data->nonce);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}
	}

	if (timestamp->tst_info->tsa != NULL) {
		tmp_res = GT_getGeneralName(
				timestamp->tst_info->tsa,
				&explicit_data->issuer_name);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}
	}

	explicit_data->signer_info_version =
		ASN1_INTEGER_get(timestamp->signer_info->version);

	assert(tmp_bio == NULL);
	tmp_bio = BIO_new(BIO_s_mem());
	if (tmp_bio == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	if (X509_NAME_print_ex(tmp_bio,
				timestamp->signer_info->issuer_and_serial->issuer,
				0, XN_FLAG_RFC2253) < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	mem_len = BIO_get_mem_data(tmp_bio, &mem_data);

	explicit_data->cert_issuer_name = GT_malloc(mem_len + 1);
	if (explicit_data->cert_issuer_name == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}
	memcpy(explicit_data->cert_issuer_name, mem_data, mem_len);
	explicit_data->cert_issuer_name[mem_len] = '\0';

	BIO_free(tmp_bio);
	tmp_bio = NULL;

	explicit_data->digest_algorithm =
		GT_EVPToHashChainID(
				EVP_get_digestbyobj(
					timestamp->signer_info->digest_alg->algorithm));

	tmp_res = GTSignedAttributeList_set(
			&explicit_data->signed_attr_count,
			&explicit_data->signed_attr_list,
			timestamp->signer_info->auth_attr);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = oidToString(
			timestamp->signer_info->digest_enc_alg->algorithm,
			&explicit_data->signature_algorithm);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GTHashEntryList_set(
			&explicit_data->location_count,
			&explicit_data->location_list,
			timestamp->time_signature->location);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	tmp_res = GTHashEntryList_set(
			&explicit_data->history_count,
			&explicit_data->history_list,
			timestamp->time_signature->history);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	published_data = timestamp->time_signature->publishedData;

	if (!GT_asn1IntegerToUint64(&tmp_uint64,
				published_data->publicationIdentifier)) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	/* The following condition checks for time_t overflows on 32-bit platforms
	 * and should be optimized away if time_t is at least 64 bits long. */
	if (sizeof(time_t) < 8 &&
			((time_t) tmp_uint64 < 0 ||
			 (((GT_UInt64) ((time_t) tmp_uint64)) != tmp_uint64))) {
		/* This error code assumes that no-one uses 32-bit time_t after
		 * the year of 2038, so it is safe to say that file format is
		 * invalid before that. */
		res = GT_INVALID_FORMAT; /* TODO: perhaps SYSTEM_ERROR or smth would be more appropriate? */
		goto cleanup;
	}

	explicit_data->publication_identifier = tmp_uint64;

	if (ASN1_STRING_length(published_data->publicationImprint) < 1) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	explicit_data->publication_hash_algorithm =
		ASN1_STRING_data(published_data->publicationImprint)[0];

	tmp_res = GT_hexEncode(
			ASN1_STRING_data(published_data->publicationImprint) + 1,
			ASN1_STRING_length(published_data->publicationImprint) - 1,
			&explicit_data->publication_hash_value);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	if (timestamp->time_signature->pkSignature != NULL &&
			timestamp->time_signature->pkSignature->keyCommitmentRef != NULL) {
		tmp_res = GTReferenceList_set(
				&explicit_data->key_commitment_ref_count,
				&explicit_data->key_commitment_ref_list,
				timestamp->time_signature->pkSignature->keyCommitmentRef);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}
	}

	if (timestamp->time_signature->pubReference != NULL) {
		tmp_res = GTReferenceList_set(
				&explicit_data->pub_reference_count,
				&explicit_data->pub_reference_list,
				timestamp->time_signature->pubReference);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}
	}

	res = GT_OK;

cleanup:
	GT_free(tmp_str);
	BIO_free(tmp_bio);

	return res;
}

/* Verification helper. Creates new verification info structure and sets
 * most of the values. */
static int createVerificationInfo(
		const GTTimestamp *timestamp,
		GTVerificationInfo **verification_info,
		int parse_data)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	GTVerificationInfo *tmp_info = NULL;
	GT_HashDBIndex history_identifier;
	ASN1_OCTET_STRING *history_shape = NULL;
	GT_UInt64 location_id = 0;
	unsigned char *location_name = NULL;

	assert(timestamp != NULL);
	assert(verification_info != NULL);

	tmp_info = GT_malloc(sizeof(GTVerificationInfo));
	if (tmp_info == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	tmp_info->version = 2;
	tmp_info->verification_errors = GT_NO_FAILURES;
	tmp_info->verification_status = 0;
	tmp_info->implicit_data = NULL;
	tmp_info->explicit_data = NULL;

	tmp_info->implicit_data = GT_malloc(sizeof(GTTimeStampImplicit));
	if (tmp_info->implicit_data == NULL) {
		res = GT_OUT_OF_MEMORY;
		goto cleanup;
	}

	tmp_info->implicit_data->location_name = NULL;
	tmp_info->implicit_data->public_key_fingerprint = NULL;
	tmp_info->implicit_data->publication_string = NULL;

	if (parse_data) {
		tmp_res = addExplicitVerificationInfo(timestamp, tmp_info);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}
	}

	if (timestamp->time_signature->pkSignature != NULL) {
		tmp_info->verification_status |= GT_PUBLIC_KEY_SIGNATURE_PRESENT;
	}

	if (timestamp->time_signature->pubReference != NULL &&
			sk_ASN1_OCTET_STRING_num(timestamp->time_signature->pubReference) > 0) {
		tmp_info->verification_status |= GT_PUBLICATION_REFERENCE_PRESENT;
	}

	tmp_res = GT_shape(timestamp->time_signature->history, &history_shape);
	if (tmp_res == GT_OK) {
		tmp_res = GT_findHistoryIdentifier(
				(timestamp->time_signature->
				 publishedData->publicationIdentifier),
				history_shape, NULL, &history_identifier);
	}
	/* The following condition checks for time_t overflows on 32-bit platforms
	 * and should be optimized away if time_t is at least 64 bits long. */
	if (sizeof(time_t) < 8 && tmp_res == GT_OK &&
			((time_t) history_identifier < 0 ||
 			 (((GT_UInt64) ((time_t) history_identifier)) !=
 			  history_identifier))) {
		/* This error code assumes that no-one uses 32-bit time_t after
		 * the year of 2038, so it is safe to say that file format is
		 * invalid before that. */
		tmp_res = GT_INVALID_FORMAT; /* TODO: perhaps SYSTEM_ERROR or smth would be more appropriate? */
	}
	if (tmp_res != GT_OK) {
		switch (tmp_res) {
		case GT_INVALID_FORMAT:
		case GT_INVALID_LINKING_INFO:
		case GT_UNSUPPORTED_FORMAT:
			tmp_info->verification_errors |= GT_SYNTACTIC_CHECK_FAILURE;
			history_identifier = 0;
			break;
		default:
			res = tmp_res;
			goto cleanup;
		}
	}

	tmp_info->implicit_data->registered_time = history_identifier;

	tmp_res = extractLocation(timestamp->time_signature->location,
			&location_id, &location_name);
	switch (tmp_res) {
	case GT_OK:
		break;
	case GT_INVALID_LINKING_INFO:
		tmp_info->verification_errors |= GT_SYNTACTIC_CHECK_FAILURE;
		break;
	default:
		res = tmp_res;
		goto cleanup;
	}

	tmp_info->implicit_data->location_id = location_id;
	tmp_info->implicit_data->location_name = location_name;
	location_name = NULL;

	if ((tmp_info->verification_status &
				GT_PUBLIC_KEY_SIGNATURE_PRESENT) == 0) {
		tmp_res = setVerifiedPublicationSignatureInfo(timestamp, tmp_info);
	} else {
		tmp_res = setVerifiedPKISignatureInfo(timestamp, tmp_info);
	}
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	*verification_info = tmp_info;
	tmp_info = NULL;
	res = GT_OK;

cleanup:
	GTVerificationInfo_free(tmp_info);
	ASN1_OCTET_STRING_free(history_shape);
	GT_free(location_name);

	return res;
}

/* Helper for performing syntactic check of the timestamp. */
static int checkTimestampSyntax(const GTTimestamp *timestamp)
{
	int tmp_res;
	ASN1_TYPE *attribute_value;

	/* NOTE: Most of the syntactic check is already performed on decoding of
	 * the timestamp (because it is impossible to decode just any random byte
	 * sequence) and there's no point to repeat these tests here again. See
	 * code of the GTTimestamp_update*() functions for more info. */

	/* Check versions. */

	if (ASN1_INTEGER_get(timestamp->token->d.sign->version) != 3) {
		return GT_UNSUPPORTED_FORMAT;
	}

	if (ASN1_INTEGER_get(timestamp->tst_info->version) != 1) {
		return GT_UNSUPPORTED_FORMAT;
	}

	if (ASN1_INTEGER_get(timestamp->signer_info->version) != 1) {
		return GT_UNSUPPORTED_FORMAT;
	}

	/* Check unknown critical extensions. */

	tmp_res = GT_checkUnhandledExtensions(timestamp->tst_info->extensions);
	if (tmp_res != GT_OK) {
		return tmp_res;
	}

	/* Check DataImprint values. */

	tmp_res = GT_checkDataImprint(
			timestamp->time_signature->publishedData->publicationImprint);
	if (tmp_res != GT_OK) {
		return tmp_res;
	}

	/* Check HashChain values. */

	tmp_res = GT_checkHashChain(timestamp->time_signature->location);
	if (tmp_res != GT_OK) {
		return tmp_res;
	}

	tmp_res = GT_checkHashChain(timestamp->time_signature->history);
	if (tmp_res != GT_OK) {
		return tmp_res;
	}

	/* Check length consistency of location. */

	tmp_res = GT_checkHashChainLengthConsistent(
			timestamp->time_signature->location);
	if (tmp_res != GT_OK) {
		return tmp_res;
	}

	/* Check that signed attributes contains proper content type. */

	attribute_value = PKCS7_get_signed_attribute(
			timestamp->signer_info, NID_pkcs9_contentType);
	if (attribute_value == NULL ||
			attribute_value->type != V_ASN1_OBJECT ||
			(OBJ_obj2nid(attribute_value->value.object) !=
			 NID_id_smime_ct_TSTInfo)) {
		return GT_INVALID_FORMAT;
	}

	/* Check that signed attributes contains proper message digest. */

	attribute_value = PKCS7_get_signed_attribute(
			timestamp->signer_info, NID_pkcs9_messageDigest);
	if (attribute_value == NULL ||
			attribute_value->type != V_ASN1_OCTET_STRING) {
		return GT_INVALID_FORMAT;
	}
	/* NOTE: Checking of the digest value will be done in hash chain check. */

	return GT_OK;
}

/* Helper for performing of the hash chain check. */
static int checkHashChain(const GTTimestamp *timestamp)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	int alg_server;
	int alg_client;
	unsigned char *tmp_der = NULL;
	int tmp_der_len;
	ASN1_OCTET_STRING *tmp_imprint = NULL;
	ASN1_TYPE *attribute_value;
	ASN1_OCTET_STRING *input = NULL;
	unsigned char *loc_output = NULL;
	size_t loc_output_len;
	unsigned char *hist_output = NULL;
	size_t hist_output_len;
	ASN1_OCTET_STRING *output = NULL;

	if (ASN1_STRING_length(timestamp->time_signature->
				publishedData->publicationImprint) < 1) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}
	alg_server = ASN1_STRING_data(timestamp->time_signature->
			publishedData->publicationImprint)[0];
	if (!GT_isSupportedHashAlgorithm(alg_server)) {
		res = GT_UNTRUSTED_HASH_ALGORITHM;
		goto cleanup;
	}

	alg_client = GT_EVPToHashChainID(
			EVP_get_digestbyobj(
				timestamp->signer_info->digest_alg->algorithm));
	if (alg_client < 0) {
		res = GT_UNTRUSTED_HASH_ALGORITHM;
		goto cleanup;
	}
	if (timestamp->signer_info->digest_alg->parameter != NULL &&
			(timestamp->signer_info->digest_alg->parameter->type !=
			 V_ASN1_NULL)) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	/* Check that digest value in signed attribute corresponds to the
	 * DER-encoding of the TSTInfo. */
	assert(tmp_der == NULL);
	tmp_der_len = i2d_GTTSTInfo(timestamp->tst_info, &tmp_der);
	if (tmp_der_len < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}
	assert(tmp_imprint == NULL);
	tmp_res = GT_calculateDataImprint(
			tmp_der, tmp_der_len, alg_client, &tmp_imprint);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}
	attribute_value = PKCS7_get_signed_attribute(
			timestamp->signer_info, NID_pkcs9_messageDigest);
	if (attribute_value == NULL ||
			attribute_value->type != V_ASN1_OCTET_STRING) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}
	if ((ASN1_STRING_length(tmp_imprint) !=
				ASN1_STRING_length(attribute_value->value.octet_string) + 1) ||
			memcmp(
				ASN1_STRING_data(attribute_value->value.octet_string),
				ASN1_STRING_data(tmp_imprint) + 1,
				ASN1_STRING_length(tmp_imprint) - 1) != 0) {
		res = GT_WRONG_SIGNED_DATA;
		goto cleanup;
	}

	/* Find input for the hash chain calculation. */
	OPENSSL_free(tmp_der);
	tmp_der = NULL;
	tmp_der_len = ASN1_item_i2d(
			(ASN1_VALUE*) timestamp->signer_info->auth_attr, &tmp_der,
			ASN1_ITEM_rptr(PKCS7_ATTR_SIGN));
	if (tmp_der_len < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}
	assert(input == NULL);
	tmp_res =
		GT_calculateDataImprint(tmp_der, tmp_der_len, alg_client, &input);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	/* Apply location hash chain to the input. */
	assert(loc_output == NULL);
	tmp_res = GT_hashChainCalculate(
			ASN1_STRING_data(timestamp->time_signature->location),
			ASN1_STRING_length(timestamp->time_signature->location),
			ASN1_STRING_data(input),
			ASN1_STRING_length(input),
			&loc_output, &loc_output_len);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	/* Apply history hash chain to the input. */
	assert(hist_output == NULL);
	tmp_res = GT_hashChainCalculateNoDepth(
			ASN1_STRING_data(timestamp->time_signature->history),
			ASN1_STRING_length(timestamp->time_signature->history),
			loc_output, loc_output_len,
			&hist_output, &hist_output_len);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	/* Perform final hashing step. */
	assert(output == NULL);
	tmp_res = GT_calculateDataImprint(
			hist_output, hist_output_len, alg_server, &output);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	/* Compare result with the expected value. */
	if (ASN1_OCTET_STRING_cmp(output,
				(timestamp->time_signature->
				 publishedData->publicationImprint)) != 0) {
		res = GT_INVALID_AGGREGATION;
		goto cleanup;
	}

	res = GT_OK;

cleanup:
	OPENSSL_free(tmp_der);
	ASN1_OCTET_STRING_free(tmp_imprint);
	ASN1_OCTET_STRING_free(input);
	OPENSSL_free(loc_output);
	OPENSSL_free(hist_output);
	ASN1_OCTET_STRING_free(output);

	return res;
}

/* Helper for performing of the public key signature check. */
static int checkPublicKeySignature(
		const GTTimestamp *timestamp, const X509 *certificate)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	unsigned char *published_data_der = NULL;
	int published_data_der_len;
	const X509_ALGOR *algorithm;
	const ASN1_OCTET_STRING *signature;
	EVP_MD_CTX md_ctx;
	EVP_PKEY *pubkey = NULL;
	const EVP_MD *evp_md;

	assert(timestamp != NULL);
	assert(timestamp->time_signature != NULL);
	assert(timestamp->time_signature->pkSignature != NULL);
	assert(certificate != NULL);

	algorithm = timestamp->time_signature->pkSignature->signatureAlgorithm;
	signature = timestamp->time_signature->pkSignature->signatureValue;

	EVP_MD_CTX_init(&md_ctx);

	/* DER-encode published data. */
	assert(published_data_der == NULL);
	published_data_der_len = i2d_GTPublishedData(
			timestamp->time_signature->publishedData, &published_data_der);
	if (published_data_der_len < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	/* Extract public key from the certificate. */
	pubkey = X509_get_pubkey((X509*) certificate);
	if (pubkey == NULL) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	/* Get hash algorithm. */
	evp_md = EVP_get_digestbyobj(algorithm->algorithm);
	if (evp_md == NULL) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}
	if (algorithm->parameter != NULL &&
			algorithm->parameter->type != V_ASN1_NULL) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	/* Check if hash algorithm is supported/trusted. */
	if (GT_EVPToHashChainID(evp_md) < 0) {
		res = GT_UNTRUSTED_SIGNATURE_ALGORITHM;
		goto cleanup;
	}

	/* Verify. */
	if (EVP_VerifyInit(&md_ctx, evp_md) == 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}
	if (EVP_VerifyUpdate(&md_ctx,
				published_data_der, published_data_der_len) == 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}
	tmp_res = EVP_VerifyFinal(
			&md_ctx,
			ASN1_STRING_data((ASN1_OCTET_STRING*) signature),
			ASN1_STRING_length((ASN1_OCTET_STRING*) signature),
			pubkey);
	if (tmp_res < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}
	if (tmp_res == 0) {
		res = GT_INVALID_SIGNATURE;
		goto cleanup;
	}

	res = GT_OK;

cleanup:
	OPENSSL_free(published_data_der);
	EVP_PKEY_free(pubkey);
	EVP_MD_CTX_cleanup(&md_ctx);

	return res;
}

/**/

int GTTimestamp_verify(const GTTimestamp *timestamp,
		int parse_data, GTVerificationInfo **verification_info)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	const X509 *certificate = NULL;
	GTVerificationInfo *tmp_info = NULL;

	if (timestamp == NULL || timestamp->token == NULL ||
			timestamp->tst_info == NULL || timestamp->time_signature == NULL ||
			verification_info == NULL) {
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	/* Create verification info structure with most fields already set to their
	 * final values. */
	tmp_res = createVerificationInfo(timestamp, &tmp_info, parse_data);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	/* Extract public key certificate for convenience. */
	if (tmp_info->verification_status & GT_PUBLIC_KEY_SIGNATURE_PRESENT) {
		certificate = PKCS7_cert_from_signer_info(
				timestamp->token, timestamp->signer_info);
	}

	/* Syntactic Check. */
	tmp_res = checkTimestampSyntax(timestamp);
	if (tmp_res != GT_OK) {
		tmp_info->verification_errors |= GT_SYNTACTIC_CHECK_FAILURE;
	}

	/* Hash Chain Check. */
	tmp_res = checkHashChain(timestamp);
	switch (tmp_res) {
	case GT_OK:
		break;
	case GT_INVALID_FORMAT:
	case GT_UNTRUSTED_HASH_ALGORITHM:
	case GT_WRONG_SIGNED_DATA:
	case GT_INVALID_AGGREGATION:
		tmp_info->verification_errors |= GT_HASHCHAIN_VERIFICATION_FAILURE;
		break;
	default:
		res = tmp_res;
		goto cleanup;
	}

	/* Public Key Signature Check if applicable. */
	if (timestamp->time_signature->pkSignature != NULL) {
		if (certificate == NULL) {
			/* Should not happen but it's better to be paranoid here. */
			tmp_res = GT_INVALID_FORMAT;
		} else {
			tmp_res = checkPublicKeySignature(timestamp, certificate);
		}
		switch (tmp_res) {
		case GT_OK:
			break;
		case GT_INVALID_FORMAT:
		case GT_UNTRUSTED_HASH_ALGORITHM:
		case GT_UNTRUSTED_SIGNATURE_ALGORITHM:
		case GT_WRONG_SIGNED_DATA:
		case GT_INVALID_SIGNATURE:
			tmp_info->verification_errors |= GT_PUBLIC_KEY_SIGNATURE_FAILURE;
			break;
		default:
			res = tmp_res;
			goto cleanup;
		}
	}

	*verification_info = tmp_info;
	tmp_info = NULL;
	res = GT_OK;

cleanup:
	GTVerificationInfo_free(tmp_info);

	return res;
}

/**/

int GTTimestamp_checkDocumentHash(
		const GTTimestamp *timestamp, const GTDataHash *data_hash)
{
	int res = GT_UNKNOWN_ERROR;
	const GTMessageImprint *message_imprint;
	int hash_algorithm;

	if (data_hash == NULL || data_hash->digest_length == 0 ||
			data_hash->digest == NULL || data_hash->context != NULL)
	{
		res = GT_INVALID_ARGUMENT;
		goto cleanup;
	}

	message_imprint = timestamp->tst_info->messageImprint;

	hash_algorithm = GT_EVPToHashChainID(
			EVP_get_digestbyobj(message_imprint->hashAlgorithm->algorithm));
	if (hash_algorithm < 0) {
		res = GT_UNTRUSTED_HASH_ALGORITHM;
		goto cleanup;
	}
	if (message_imprint->hashAlgorithm->parameter != NULL &&
			(ASN1_TYPE_get(message_imprint->hashAlgorithm->parameter) !=
			 V_ASN1_NULL)) {
		res = GT_UNTRUSTED_HASH_ALGORITHM;
		goto cleanup;
	}

	if (hash_algorithm != GT_fixHashAlgorithm(data_hash->algorithm)) {
		res = GT_DIFFERENT_HASH_ALGORITHMS;
		goto cleanup;
	}

	if ((ASN1_STRING_length(message_imprint->hashedMessage) !=
				data_hash->digest_length) ||
			memcmp(
				ASN1_STRING_data(message_imprint->hashedMessage),
				data_hash->digest, data_hash->digest_length) != 0) {
		res = GT_WRONG_DOCUMENT;
		goto cleanup;
	}

	res = GT_OK;

cleanup:
	return res;
}

/**/

int GTTimestamp_checkPublication(
		const GTTimestamp *timestamp,
		const GTPublicationsFile *publications_file)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	GT_HashDBIndex publication_identifier;
	GTPublishedData *published_data = NULL;

	assert(timestamp != NULL);
	assert(publications_file != NULL);

	if (!GT_asn1IntegerToUint64(&publication_identifier,
				(timestamp->time_signature->
				 publishedData->publicationIdentifier))) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	tmp_res = GTPublicationsFile_getPublishedData(
			publications_file, publication_identifier, &published_data);
	if (tmp_res != GT_OK) {
		res = tmp_res;
		goto cleanup;
	}

	if (GTPublishedData_cmp(
				published_data,
				timestamp->time_signature->publishedData) != 0) {
		/* We have published data with the correct publication identifier. So
		 * we can't say that trust point not found anymore, but should say it's
		 * invalid because it's contents are not the same as inside the time
		 * stamp. */
		res = GT_INVALID_TRUST_POINT;
		goto cleanup;
	}

	res = GT_OK;

cleanup:
	GTPublishedData_free(published_data);

	return res;
}

/**/

int GTTimestamp_checkPublicKey(
		const GTTimestamp *timestamp,
		GT_Time_t64 history_identifier,
		const GTPublicationsFile *publications_file)
{
	int res = GT_UNKNOWN_ERROR;
	int tmp_res;
	const X509 *certificate = NULL;
	unsigned char *key_der = NULL;
	int key_der_len;
	unsigned int i;
	const unsigned char *cur_imprint;
	size_t cur_imprint_size;
	ASN1_OCTET_STRING *key_hash = NULL;
	const GTPublicationsFile_KeyHashCell *keycell;

	assert(timestamp != NULL);
	assert(timestamp->time_signature != NULL);
	assert(timestamp->time_signature->pkSignature != NULL);
	assert(publications_file != NULL);

	certificate = PKCS7_cert_from_signer_info(
			timestamp->token, timestamp->signer_info);
	if (certificate == NULL) {
		res = GT_INVALID_FORMAT;
		goto cleanup;
	}

	assert(key_der == NULL);
	key_der_len = i2d_X509_PUBKEY(certificate->cert_info->key, &key_der);
	if (key_der_len < 0) {
		res = GT_CRYPTO_FAILURE;
		goto cleanup;
	}

	res = GT_KEY_NOT_PUBLISHED;

	for (i = 0; i < publications_file->number_of_key_hashes; ++i) {
		tmp_res = GTPublicationsFile_getKeyHash(
				publications_file, i, &cur_imprint, &cur_imprint_size);
		if (tmp_res != GT_OK) {
			res = tmp_res;
			goto cleanup;
		}

		assert(cur_imprint_size > 0);

		if (key_hash == NULL || key_hash->data[0] != cur_imprint[0]) {
			ASN1_OCTET_STRING_free(key_hash);
			key_hash = NULL;

			tmp_res = GT_calculateDataImprint(
					key_der, key_der_len, cur_imprint[0], &key_hash);
			if (tmp_res != GT_OK) {
				/* If we failed to hash the key - we just skip the
				 * current hash. */
				continue;
			}
		}

		assert(key_hash != NULL);

		if (key_hash->length != cur_imprint_size) {
			/* Should never happen unless publications file has an incorrect
			 * format. Just skip this hash. */
			continue;
		}

		if (memcmp(key_hash->data, cur_imprint, cur_imprint_size) == 0) {
			keycell = publications_file->key_hash_cells + i;

			if (keycell->key_publication_time > history_identifier) {
				res = GT_CERT_TICKET_TOO_OLD;
				goto cleanup;
			}

			res = GT_OK;
			break;
		}
	}

cleanup:
	OPENSSL_free(key_der);
	ASN1_OCTET_STRING_free(key_hash);

	return res;
}
