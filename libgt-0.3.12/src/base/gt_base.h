/*
 * $Id: gt_base.h 202 2014-01-23 16:51:35Z henri.lakk $
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

/**
 * \file gt_base.h
 *
 * \brief Guardtime client SDK, public header file for base functions.
 *
 * This module offers the basic functions to create, encode, decode and
 * otherwise process the timestamps, but does not address issues like
 * network transport or embedding timestamps into specific document formats.
 *
 * The three main functions are:
 * - creating a timestamp (the resulting PKI-signed timestamp
 * can be verified using the Guardtime public key certificate);
 * - extending a timestamp (the resulting hash-linked timestamp
 * can be verified independently from Guardtime for unlimited time);
 * - verifying a timestamp.
 *
 * <h2>Usage</h2>
 *
 * The cryptographic functions in this SDK rely on OpenSSL. The use of
 * the OpenSSL code is covered by the license available online from
 * <a href="http://www.openssl.org/source/license.html">www.openssl.org/source/license.html</a>
 * and also enclosed in the file \c license.openssl.txt.
 *
 * Multi-platform source code for OpenSSL can be downloaded from
 * <a href="http://www.openssl.org/">www.openssl.org</a>.
 * This version of the SDK has been tested with OpenSSL 0.9.8g and 1.0.1e.
 *
 * <b>Linux</b>
 *
 * Although it's not difficult to compile OpenSSL on Linux, most distributions
 * also provide prebuilt binaries through their normal package management
 * facilities. When installing a prebuilt package, make sure you pick a
 * "developer" version, otherwise you may only get the tools and utilities,
 * but no libraries or headers.
 *
 * To compile a program that uses the Guardtime base API, use a command along
 * the lines of
 * \code
 *    gcc example.c -o example -I/usr/local/gt/include \
 *    -L/usr/local/gt/lib -L/usr/local/ssl/lib \
 *    -lgtbase -lcrypto -lrt
 * \endcode
 * (either with the backslashes or all on a single line) replacing
 * \c /usr/local/gt and \c /usr/local/ssl with
 * the directories where you unpacked the Guardtime and OpenSSL libraries.
 *
 * <b>Windows</b>
 *
 * OpenSSL can be complied on Windows, but it's easier to use the prebuilt
 * binaries distributed with the SDK.
 *
 * To compile a program that uses the Guardtime base API, use a command along
 * the lines of
 * \code
 *    cl.exe /MT example.c /I C:\gt\include
 *    /link /libpath:C:\gt\lib /libpath:C:\openssl\lib
 *    libgtbaseMT.lib libeay32MT.lib
 *    user32.lib gdi32.lib advapi32.lib crypt32.lib
 * \endcode
 * (all on a single line) replacing
 * \c C:\\gt and \c C:\\openssl with
 * the directories where you unpacked the Guardtime and OpenSSL libraries.
 *
 * When compiling, keep in mind that mixing code compiled with different
 * \c /Mxx settings is dangerous. It's best to always use the
 * Guardtime and OpenSSL libraries that match the \c /Mxx
 * setting you specified for compiling your own source code.
 */

#ifndef GT_BASE_H_INCLUDED
#define GT_BASE_H_INCLUDED

#include <stddef.h>
#include <stdio.h>
#include <time.h>

/**
 * \ingroup common
 *
 * Version number of the API, as a 4-byte integer, with the major number
 * in the highest, minor number in the second highest and build number
 * in the two lowest bytes.
 * The preprocessor macro is included to enable conditional compilation.
 *
 * \see GT_getVersion
 */
#define GT_VERSION (0 << 24 | 3 << 16 | 12)

#ifdef _WIN32
	/**
	 * \ingroup common
	 *
	 * Generic 64-bit signed integer.
	 */
	typedef __int64 GT_Int64;
	/**
	 * \ingroup common
	 *
	 * Generic 64-bit unsigned integer.
	 */
	typedef unsigned __int64 GT_UInt64;
#else /* not _WIN32 */
#include <stdint.h>
	/**
	 * \ingroup common
	 *
	 * Generic 64-bit signed integer.
	 */
	typedef int64_t GT_Int64;
	/**
	 * \ingroup common
	 *
	 * Generic 64-bit unsigned integer.
	 */
	typedef uint64_t GT_UInt64;
#endif /* not _WIN32 */

/**
 * \ingroup common
 *
 * This type is used as a 64-bit \c time_t.
 *
 * \note Your system's standard \c time_t may be 32- or 64-bit, depending
 * on the operating system, compiler, and in some cases even on the compiler
 * settings.
 *
 * \note Even when the value contained in a \c GT_Time_t64 variable is within
 * the range of \c time_t, care must be taken to avoid using \c localtime(),
 * \c gmtime(), etc in multithreaded programs, as these functions may rely on
 * internal static buffers shared among all threads in an application.
 */
typedef GT_Int64 GT_Time_t64;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \ingroup timestamps
 *
 * This opaque structure represents a timestamp.
 * Use the #GTTimestamp_verify(), #GTTimestamp_getAlgorithm(),
 * #GTTimestamp_isExtended(), #GTTimestamp_isEarlierThan()
 * functions to extract details from it.
 */
typedef struct GTTimestamp_st GTTimestamp;

/**
 * \ingroup publications
 *
 * This opaque structure represents a publications file.
 * Use the #GTPublicationsFile_verify(), #GTPublicationsFile_getByIndex(),
 * #GTPublicationsFile_getKeyHashByIndex() functions to extract details from it.
 */
typedef struct GTPublicationsFile_st GTPublicationsFile;

/**
 * \ingroup common
 * \brief This structure represents hashed data.
 *
 * A Guardtime hash sum object, used as the seed to create
 * a #GTTimestamp and again when verifying that timestamp
 * to create a #GTVerificationInfo.
 *
 * When calculating the hash sum of some data to verify a
 * timestamp, make sure to use the same hash algorithm as
 * when the timestamp was created.
 */
typedef struct GTMessageDigest_st {
	/**
	 * Data digest.
	 */
	unsigned char *digest;
	/**
	 * Length of the digest.
	 */
	size_t digest_length;
	/**
	 * The hash algorithm used to produce the digest.
	 * See #GTHashAlgorithm for possible values.
	 */
	int algorithm;
	/**
	 * The context of hash computation. Only used while
	 * incremental hashing is in progress.
	 */
	void *context;
} GTDataHash;

/**
 * \ingroup verification
 * \brief This structure represents the implicit data computed from a timestamp.
 */
typedef struct GTTimeStampImplicit_st {
	/**
	 * Timestamp issuer address within the Guardtime network.
	 * Extracted from the shape of the location hash chain.
	 * Internally, the address is a concatenation of four
	 * 16-bit fields and it would be reasonable to divide
	 * it up accordingly for displaying to the users.
	 */
	GT_UInt64 location_id;
	/**
	 * Timestamp issuer name within the Guardtime network.
	 * Extracted from the aggregator names embedded in the
	 * location hash chain.
	 */
	char *location_name;
	/**
	 * The time when the Guardtime core registered the timestamp.
	 * Extracted from the shape of the history hash chain.
	 */
	GT_Time_t64 registered_time;
	/**
	 * Public key fingerprint, in base 32.
	 * Present only if #GT_PUBLIC_KEY_SIGNATURE_PRESENT is set
	 * in #GTVerificationInfo::verification_status.
	 */
	char *public_key_fingerprint;
	/**
	 * Control string for verifying the timestamp using a hardcopy
	 * publication, the value is base32(time+alg+hash+crc32).
	 */
	char *publication_string;
} GTTimeStampImplicit;

/**
 * \ingroup verification
 * \brief This is a helper structure to represent hash chains.
 */
typedef struct GTHashEntry_st {
	/**
	 * The algorithm used to perform the hash step.
	 * See #GTHashAlgorithm for possible values.
	 */
	int hash_algorithm;
	/**
	 * The hash chain shape indicator.
	 * If \c direction is 0, then \c sibling_hash_value is concatenated
	 * to the left of the hash of the data from the previous step.
	 * If \c direction is 1, then \c sibling_hash_value is concatenated
	 * to the right of the hash of the data from the previous step.
	 * No other values are permitted.
	 */
	int direction;
	/**
	 * The algorithm used to compute the sibling hash value.
	 * See #GTHashAlgorithm for possible values.
	 */
	int sibling_hash_algorithm;
	/**
	 * The hash value from the sibling in the tree, in base 16.
	 */
	char *sibling_hash_value;
	/**
	 * Indicates how many steps are allowed to precede the current step.
	 */
	int level;
} GTHashEntry;

/**
 * \ingroup verification
 * \brief This is a helper structure to represent a signed attribute and its value.
 */
typedef struct GTSignedAttribute_st {
	/**
	 * The attribute OID.
	 * A Guardtime timestamp has to contain at least two signed attributes:
	 * "1.2.840.113549.1.9.3" (contentType),
	 * "1.2.840.113549.1.9.4" (messageDigest).
	 */
	char *attr_type;
	/**
	 * The value of the attribute, in base 16.
	 */
	char *attr_value;
} GTSignedAttribute;

/**
 * \ingroup verification
 * \brief This structure represents the explicit data extracted from a timestamp.
 */
typedef struct GTTimeStampExplicit_st {
	/**
	 * CMS \c ContentInfo content type.
	 * Currently always "1.2.840.113549.1.7.2",
	 * for PKCS#7 CMS \c SignedData.
	 */
	char *content_type;
	/**
	 * Version of the CMS \c SignedData structure.
	 * Currently always 3.
	 */
	int signed_data_version;
	/**
	 * Count of entries in the list of digest algorithms.
	 */
	int digest_algorithm_count;
	/**
	 * List of digest algorithms used in the CMS message.
	 * See #GTHashAlgorithm for possible values.
	 */
	int *digest_algorithm_list;
	/**
	 * CMS \c EncapsulatedContentInfo content type.
	 * Currently always "1.2.840.113549.1.9.16.1.4",
	 * for PKCS#9 TSP \c TSTInfo.
	 */
	char *encap_content_type;
	/**
	 * Version of the CMS \c TSTInfo structure.
	 * Currently always 1.
	 */
	int tst_info_version;
	/**
	 * Guardtime timestamping policy ID.
	 */
	char *policy;
	/**
	 * The algorithm used to produce the data hash.
	 * See #GTHashAlgorithm for possible values.
	 */
	int hash_algorithm;
	/**
	 * The hash value submitted to be timestamped, in base 16.
	 */
	char *hash_value;
	/**
	 * Timestamp serial number, in base 16.
	 */
	char *serial_number;
	/**
	 * The time when gateway received the request for the timestamp.
	 */
	GT_Time_t64 issuer_request_time;
	/**
	 * Precision of the gateway clock, in milliseconds.
	 */
	int issuer_accuracy;
	/**
	 * The nonce from the timestamping request, in base 16.
	 * \c NULL if not present in the timestamp.
	 */
	char *nonce;
	/**
	 * Timestamp issuer service name.
	 * \c NULL if not present in the timestamp.
	 */
	char *issuer_name;
	/**
	 * Public key certificate, in base 32.
	 * Normally \c NULL for long-term hash-linked timestamps.
	 */
	char *certificate;
	/**
	 * Version of the CMS \c SignerInfo structure.
	 * Currently always 1.
	 */
	int signer_info_version;
	/**
	 * Certificate issuer name.
	 */
	char *cert_issuer_name;
	/**
	 * Certificate serial number, in base 16.
	 */
	char *cert_serial_number;
	/**
	 * Digest algorithm used for the signature.
	 * See #GTHashAlgorithm for possible values.
	 */
	int digest_algorithm;
	/**
	 * Count of entries in the list of signed attributes.
	 */
	int signed_attr_count;
	/**
	 * List of signed attributes.
	 */
	GTSignedAttribute *signed_attr_list;
	/**
	 * Guardtime timestamping algorithm ID.
	 * Currently always "1.3.6.1.4.1.27868.4.1",
	 * for Guardtime TimeSignature algorithm.
	 */
	char *signature_algorithm;
	/**
	 * Count of entries in the hash chain that describes
	 * the location of the timesource within the Guardtime network.
	 */
	int location_count;
	/**
	 * The hash chain that describes the location
	 * of the timesource within the Guardtime network.
	 */
	GTHashEntry *location_list;
	/**
	 * Count of entries in the hash chain that describes
	 * the provenience path from the aggregation round to publication.
	 */
	int history_count;
	/**
	 * The hash chain that describes the provenience path
	 * from the aggregation round to publication.
	 */
	GTHashEntry *history_list;
	/**
	 * Publication ID.
	 */
	GT_Time_t64 publication_identifier;
	/**
	 * The algorithm used to compute the publication hash value.
	 * See #GTHashAlgorithm for possible values.
	 */
	int publication_hash_algorithm;
	/**
	 * Publication hash value, in base 16.
	 */
	char *publication_hash_value;
	/**
	 * PKI signature algorithm ID.
	 * Either both or neither of pki_algorithm and pki_value are present.
	 * Currently, if present, always "1.2.840.113549.1.1.11",
	 * for id-sha256WithRSAEncryption.
	 */
	char *pki_algorithm;
	/**
	 * PKI signature, in base 16.
	 * Either both or neither of pki_algorithm and pki_value are present.
	 */
	char *pki_value;
	/**
	 * Count of entries in the list of key commitment references.
	 */
	int key_commitment_ref_count;
	/**
	 * List of key commitment references, in UTF-8.
	 */
	char **key_commitment_ref_list;
	/**
	 * Count of entries in the list of publication references.
	 */
	int pub_reference_count;
	/**
	 * List of publication references, in UTF-8.
	 */
	char **pub_reference_list;
} GTTimeStampExplicit;

/**
 * \ingroup verification
 * \brief This structure represents verification info of a timestamp.
 */
typedef struct GTVerificationInfo_st {
	/**
	 * Version of the \c VerificationInfo structure.
	 * Currently always 2.
	 */
	int version;
	/**
	 * Bitmap of errors found in the timestamp during verification.
	 * See #GTVerificationError for meaning of the bits.
	 */
	int verification_errors;
	/**
	 * Bitmap of conditions discovered during verification.
	 * See #GTVerificationStatus for meaning of the bits.
	 */
	int verification_status;
	/**
	 * Implicit values computed from the timestamp.
	 * Always present.
	 */
	GTTimeStampImplicit *implicit_data;
	/**
	 * Explicit values extracted from the timestamp.
	 * Only present if parsing of the timestamp was
	 * requested in the call to #GTTimestamp_verify().
	 */
	GTTimeStampExplicit *explicit_data;
} GTVerificationInfo;

/**
 * \ingroup publications
 * \brief This structure represents verification info of a publications file.
 */
typedef struct GTPubFileVerificationInfo_st {
	/**
	 * Time recorded for the first publication in file.
	 */
	GT_Time_t64 first_publication_time;
	/**
	 * Time recorded for the last publication in file.
	 */
	GT_Time_t64 last_publication_time;
	/**
	 * Number of publications.
	 */
	unsigned int publications_count;
	/**
	 * Number of key hashes.
	 */
	unsigned int key_hash_count;
	/**
	 * Public key certificate, in base 32.
	 */
	char *certificate;
} GTPubFileVerificationInfo;

/**
 * \ingroup common
 *
 * Guardtime status codes.
 */
enum GTStatusCode {
/* RANGE RESERVATIONS: */
	/**
	 * The lowest result code of the range reserved for the base module.
	 * See #GTStatusCode for definition of codes within this range.
	 */
	GTBASE_LOWEST = 0x00000000,
	/**
	 * The highest result code of the range reserved for the base module.
	 * See #GTStatusCode for definition of codes within this range.
	 */
	GTBASE_HIGHEST = 0x0000ffff,
	/**
	 * The lowest result code of the range reserved for the HTTP transport.
	 * See #GTHTTPStatusCode for definition of codes within this range.
	 * Note, however, that the HTTP functions can also relay errors from
	 * the base module.
	 */
	GTHTTP_LOWEST = 0x00010000,
	/**
	 * The highest result code of the range reserved for the HTTP transport.
	 * See #GTHTTPStatusCode for definition of codes within this range.
	 */
	GTHTTP_HIGHEST = 0x0001ffff,
	/**
	 * The lowest result code of the range reserved for the PNG integration.
	 * See #GTPNGStatusCode for definition of codes within this range.
	 * Note, however, that the PNG functions can also relay errors from the
	 * base module and HTTP transport.
	 */
	GTPNG_LOWEST = 0x00020000,
	/**
	 * The highest result code of the range reserved for the PNG integration.
	 * See #GTPNGStatusCode for definition of codes within this range.
	 */
	GTPNG_HIGHEST = 0x0002ffff,
/* RETURN CODES WHICH ARE NOT ERRORS: */
	/**
	 * The operation completed successfully.
	 */
	GT_OK = 0x00000000,
	/**
	 * When comparing timestamps, one timestamp was found to be earlier
	 * than another.
	 */
	GT_EARLIER,
	/**
	 * It could not be determined, whether one timestamp is earlier
	 * than another.
	 */
	GT_NOT_EARLIER,
	/**
	 * A timestamp was found to be extended.
	 */
	GT_EXTENDED,
	/**
	 * A timestamp was found to be not extended.
	 */
	GT_NOT_EXTENDED,
/* SYNTAX ERRORS: */
	/**
	 * Argument to function was invalid. Mostly this indicates \c NULL
	 * pointer.
	 */
	GT_INVALID_ARGUMENT = 0x00000100,
	/**
	 * Either arguments to function or responses from timestamping server had
	 * invalid format.
	 */
	GT_INVALID_FORMAT,
	/**
	 * Timestamp contained hash algorithm that is considered untrustworthy by
	 * the verification policy.
	 */
	GT_UNTRUSTED_HASH_ALGORITHM,
	/**
	 * Timestamp contained signature algorithm that is considered
	 * untrustworthy by the verification policy.
	 */
	GT_UNTRUSTED_SIGNATURE_ALGORITHM,
	/**
	 * Hash chain containing linking info is missing or invalid.
	 */
	GT_INVALID_LINKING_INFO,
	/**
	 * Unsupported data format (that is, data has valid format but unsupported
	 * version or contains an unrecognized critical extension).
	 */
	GT_UNSUPPORTED_FORMAT,
	/**
	 * Compared hashes are created using different hash algorithms.
	 */
	GT_DIFFERENT_HASH_ALGORITHMS,
	/**
	 * Unrecognized or unsupported hash algorithm.
	 */
	GT_PKI_BAD_ALG,
	/**
	 * Bad request.
	 */
	GT_PKI_BAD_REQUEST,
	/**
	 * Bad data format.
	 */
	GT_PKI_BAD_DATA_FORMAT,
	/**
	 * Unsupported extension(s) found in request.
	 */
	GT_PROTOCOL_MISMATCH,
	/**
	 * Try to extend later.
	 * Non-standard error code from extender server.
	 */
	GT_NONSTD_EXTEND_LATER,
	/**
	 * Timestamp cannot be extended anymore.
	 * Non-standard error code from extender server.
	 */
	GT_NONSTD_EXTENSION_OVERDUE,
	/**
	 * Unaccepted policy.
	 */
	GT_UNACCEPTED_POLICY,
/* SEMANTIC ERRORS: */
	/**
	 * The digest contained in the stamp does not match the document.
	 */
	GT_WRONG_DOCUMENT = 0x00000200,
	/**
	 * The number of history imprints was wrong.
	 */
	GT_WRONG_SIZE_OF_HISTORY,
	/**
	 * The hash chains for request and time have different shapes.
	 */
	GT_REQUEST_TIME_MISMATCH,
	/**
	 * Level restriction bytes in the location hash chain steps are not
	 * strictly increasing. See the timestamp format specification for
	 * more details.
	 */
	GT_INVALID_LENGTH_BYTES,
	/**
	 * The application of the hash chain containing aggregation data
	 * does not give the expected result. A possible cause for this error
	 * is that data submitted to verification process is not the same
	 * data used for timestamping.
	 */
	GT_INVALID_AGGREGATION,
	/**
	 * Signature value in timestamp is invalid.
	 */
	GT_INVALID_SIGNATURE,
	/**
	 * The value of the \c MessageDigest signed attribute is not equal
	 * to the digest of the \c TSTInfo structure.
	 */
	GT_WRONG_SIGNED_DATA,
	/**
	 * Could not find published data or trusted TSA certificate for
	 * verifying the timestamp.
	 */
	GT_TRUST_POINT_NOT_FOUND,
	/**
	 * Published data with the given ID has different digest(s).
	 */
	GT_INVALID_TRUST_POINT,
	/**
	 * Timestamp cannot be extended because the extension response contains
	 * data items presumably from the past that are not part of the
	 * short-term stamp.
	 */
	GT_CANNOT_EXTEND,
	/**
	 * Timestamp is already extended.
	 */
	GT_ALREADY_EXTENDED,
	/**
	 * The signing key is not found among published ones.
	 */
	GT_KEY_NOT_PUBLISHED,
	/**
	 * The signing key seems to have been used before it was published.
	 */
	GT_CERT_TICKET_TOO_OLD,
	/**
	 * The publications file signing key could not be traced to a trusted CA root.
	 */
	GT_CERT_NOT_TRUSTED,
/* SYSTEM ERRORS: */
	/**
	 * The operation couldn't be performed because of lack of memory.
	 */
	GT_OUT_OF_MEMORY = 0x00000300,
	/**
	 * I/O error, check \c errno for more details.
	 * But check it soon, before someone resets it.
	 */
	GT_IO_ERROR,
	/**
	 * A time value is outside the range of \c time_t.
	 */
	GT_TIME_OVERFLOW,
	/**
	 * Cryptographic operation could not be performed. Likely causes are
	 * unsupported cryptographic algorithms, invalid keys and lack of
	 * resources.
	 */
	GT_CRYPTO_FAILURE,
	/**
	 * Internal error.
	 */
	GT_PKI_SYSTEM_FAILURE,
	/**
	 * Unexpected error condition.
	 */
	GT_UNKNOWN_ERROR
};

/**
 * \ingroup verification
 *
 * Timestamp verification error codes.
 *
 * \note Values other than \c GT_NO_FAILURES are bit flags so that
 * a single \c int can contain any combination of them.
 */
enum GTVerificationError {
	/**
	 * The verification completed successfully.
	 */
	GT_NO_FAILURES = 0,
	/**
	 * The level bytes inside the hash chains are improperly ordered.
	 */
	GT_SYNTACTIC_CHECK_FAILURE = 1,
	/**
	 * The hash chain computation result does not match the publication
	 * imprint.
	 */
	GT_HASHCHAIN_VERIFICATION_FAILURE = 2,
	/**
	 * The \c signed_data structure is incorrectly composed, i.e. wrong data
	 * is signed or the signature does not match with the public key in the
	 * timestamp.
	 */
	GT_PUBLIC_KEY_SIGNATURE_FAILURE = 16,
	/**
	 * Public key of signed timestamp is not found among published ones.
	 */
	GT_NOT_VALID_PUBLIC_KEY_FAILURE = 64,
	/**
	 * Timestamp does not match with the document it is claimed to belong to.
	 */
	GT_WRONG_DOCUMENT_FAILURE = 128,
	/**
	 * The publications file is inconsistent with the corresponding data in
	 * timestamp - publication identifiers do not match or published hash
	 * values do not match.
	 */
	GT_NOT_VALID_PUBLICATION = 256
};

/**
 * \ingroup verification
 *
 * Timestamp verification status codes.
 *
 * \note The values are bit flags so that
 * a single \c int can contain any combination of them.
 */
enum GTVerificationStatus {
	/**
	 * The PKI signature was present in the timestamp.
	 */
	GT_PUBLIC_KEY_SIGNATURE_PRESENT = 1,
	/**
	 * A publication reference was present in the timestamp.
	 */
	GT_PUBLICATION_REFERENCE_PRESENT = 2,
	/**
	 * The timestamp was checked against the document hash.
	 */
	GT_DOCUMENT_HASH_CHECKED = 16,
	/**
	 * The timestamp was checked against the publication data.
	 */
	GT_PUBLICATION_CHECKED = 32
};

/**
 * \ingroup common
 *
 * The Guardtime representation of hash algorithms, necessary to calculate
 * instances of #GTDataHash.
 *
 * The currently supported algorithms are:
 * <table>
 * <tr><th>Name</th><th>OID</th><th>GT ID</th><th>digest size (bytes)</th></tr>
 * <tr><td>SHA1</td><td>1.3.14.3.2.26</td><td>0</td><td>20</td></tr>
 * <tr><td>SHA224</td><td>2.16.840.1.101.3.4.2.4</td><td>3</td><td>28</td></tr>
 * <tr><td>SHA256</td><td>2.16.840.1.101.3.4.2.1</td><td>1</td><td>32</td></tr>
 * <tr><td>SHA384</td><td>2.16.840.1.101.3.4.2.2</td><td>4</td><td>48</td></tr>
 * <tr><td>SHA512</td><td>2.16.840.1.101.3.4.2.3</td><td>5</td><td>64</td></tr>
 * <tr><td>RIPEMD160</td><td>1.3.36.3.2.1</td><td>2</td><td>20</td></tr>
 * </table>
 *
 * Names are as in the ASN.1 OID registry as defined in ITU-T Rec. X.660 / ISO/IEC 9834 series.
 */
enum GTHashAlgorithm {
	/** The SHA-1 algorithm. */
	GT_HASHALG_SHA1 = 0,
	/** The SHA-256 algorithm. */
	GT_HASHALG_SHA256,
	/** The RIPEMD-160 algorithm. */
	GT_HASHALG_RIPEMD160,
	/** The SHA-224 algorithm. */
	GT_HASHALG_SHA224,
	/** The SHA-384 algorithm. */
	GT_HASHALG_SHA384,
	/** The SHA-512 algorithm. */
	GT_HASHALG_SHA512,
	/** Use default algorithm. */
	GT_HASHALG_DEFAULT = -1
};

/**
 * \ingroup timestamps
 *
 * Encodes a timestamp into byte string.
 *
 * \param timestamp \c (in) - Timestamp that is to be encoded.
 * \param data \c (out) - Pointer that will receive pointer to the
 * DER-encoded timestamp.
 * \param data_length \c (out) - Pointer that will receive length of
 * DER-encoded timestamp
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTTimestamp_getDEREncoded(const GTTimestamp *timestamp,
		unsigned char **data, size_t *data_length);

/**
 * \ingroup timestamps
 *
 * Decodes a timestamp from given byte string.
 *
 * \param data \c (in) - Pointer to buffer containing DER-encoded timestamp.
 * \param data_length \c (in) - Size of buffer pointed by \p data.
 * \param timestamp \c (out) - Pointer that will receive pointer to decoded
 * timestamp.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTTimestamp_DERDecode(const void *data, size_t data_length,
		GTTimestamp **timestamp);

/**
 * \ingroup timestamps
 *
 * Prepares an encoded timestamp request.
 *
 * \param data_hash \c (in) - Pointer to the data hash. The hash computation
 * must be closed before the hash object can be passed to this function.
 * \param request_data \c (out) - Pointer that will receive pointer to the
 * DER-encoded timestamp request.
 * \param request_length \c (out) - Pointer to the variable that receives
 * length of DER-encoded timestamp request.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTTimestamp_prepareTimestampRequest(const GTDataHash *data_hash,
		unsigned char **request_data, size_t *request_length);

/**
 * \ingroup timestamps
 *
 * Creates a timestamp from response data.
 *
 * \param response \c (in) - Pointer to the buffer containing encoded response.
 * \param response_length \c (in) - Size of the buffer pointed by \p response.
 * \param timestamp \c (out) - Pointer that will receive pointer to the
 * created timestamp.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTTimestamp_createTimestamp(const void *response, size_t response_length,
		GTTimestamp **timestamp);

/**
 * \ingroup timestamps
 *
 * Prepares an encoded timestamp extension request that can be used to extend
 * given timestamp to another timestamp.
 *
 * \param timestamp \c (in) - Timestamp that is to be extended.
 * \param request_data \c (out) - Pointer that will receive pointer to the
 * DER-encoded timestamp extension request.
 * \param request_length \c (out) - Pointer that will receive length of
 * DER-encoded timestamp extension request.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTTimestamp_prepareExtensionRequest(const GTTimestamp *timestamp,
		unsigned char **request_data, size_t *request_length);

/**
 * \ingroup timestamps
 *
 * Creates an extended timestamp based on timestamp and extension response.
 *
 * \param timestamp \c (in) - Timestamp that is to be extended.
 * \param response \c (in) - Pointer to the buffer containing encoded
 *  extendion response.
 * \param response_length \c (in) - Size of the buffer pointed by \p response.
 * \param extended_timestamp \c (out) - Pointer that will receive pointer to
 * the extended timestamp.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code). Semantic errors can be:
 * - \c GT_CANNOT_EXTEND - The \c historyImprint field of \c timestamp is
 * inconsistent with the \c history.dataChain field of response.
 */
int GTTimestamp_createExtendedTimestamp(const GTTimestamp *timestamp,
		const void *response, size_t response_length,
		GTTimestamp **extended_timestamp);

/**
 * \ingroup timestamps
 *
 * Extracts the hash algorithm from the given timestamp.
 *
 * \param timestamp \c (in) - Pointer to timestamp.
 * \param algorithm \c (out) - Pointer to the integer value receiving
 * hash algorithm used to hash the data.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTTimestamp_getAlgorithm(const GTTimestamp *timestamp, int *algorithm);

/**
 * \ingroup timestamps
 *
 * Checks if the given timestamp is extended.
 *
 * \param timestamp \c (in) - Pointer to timestamp.
 * \return
 * - \c GT_EXTENDED, if \p timestamp is a hash-linked one.
 * - \c GT_NOT_EXTENDED, if \p timestamp is an PKI-signed one.
 * - error code, if there was an error when performing the check.
 */
int GTTimestamp_isExtended(const GTTimestamp *timestamp);

/**
 * \ingroup timestamps
 *
 * Compares temporal order of two timestamps, trying to determine if
 * \p this_timestamp was issued earlier \p that_timestamp. Note that
 * if \p this_timestamp is not earlier than \p that_timestamp, then
 * this does not necessarily mean that \p that_timestamp is earlier
 * than \p this_timestamp. It is possible that the temporal order of
 * two timestamps cannot be determined.
 *
 * Both timestamps must be successfully verified if result of this
 * comparison is to have any meaningful value.
 * \return
 * - \c GT_EARLIER, if \p this_timestamp is earlier than
 *   \p that_timestamp.
 * - \c GT_NOT_EARLIER, if \p this_timestamp cannot be proved to be
 *   earlier than \p that_timestamp.
 * - error code, if there was an error when performing the comparison.
 */
int GTTimestamp_isEarlierThan(const GTTimestamp *this_timestamp,
		const GTTimestamp *that_timestamp);

/**
 * \ingroup verification
 *
 * Checks whether the timestamp is internally consistent.
 * This is used by the verification methods in the next layer.
 *
 * \param timestamp \c (in) - Pointer to the timestamp that is to be verified.
 * \param parse_data \c (in) - The \c explicit_data field of the verification
 * info will be filled if this is non-zero.
 * \param verification_info \c (out) - Pointer that will receive pointer to
 * verification info.
 *
 * \return status code \c GT_OK, when operation succeeded, otherwise an
 * error code.
 * \note On success \c verification_errors should still be checked.
 */
int GTTimestamp_verify(const GTTimestamp *timestamp,
		int parse_data, GTVerificationInfo **verification_info);

/**
 * \ingroup verification
 *
 * Compares the document hash extracted from the timestamp to the given one.
 * This is used by the verification methods in the next layer.
 *
 * \param timestamp \c (in) - Pointer to the timestamp.
 * \param data_hash \c (in) - Pointer to the data hash. The hash computation
 * must be closed before the hash object can be passed to this function.
 *
 * \return status code \c GT_OK, when operation succeeded, otherwise an
 * error code.
 */
int GTTimestamp_checkDocumentHash(
		const GTTimestamp *timestamp, const GTDataHash *data_hash);

/**
 * \ingroup verification
 *
 * Checks that the publication extracted from the timestamp is listed
 * in the given publications file.
 * This is used by the verification methods in the next layer.
 *
 * \param timestamp \c (in) - Pointer to the timestamp.
 * \param publications_file \c (in) - Pointer to the publications file.
 *
 * \return status code \c GT_OK, when operation succeeded, otherwise an
 * error code.
 */
int GTTimestamp_checkPublication(
		const GTTimestamp *timestamp,
		const GTPublicationsFile *publications_file);

/**
 * \ingroup verification
 *
 * Checks that the key used to sign the the timestamp is listed
 * in the given publications file and was valid when the timestamp
 * was issued.
 * This is used by the verification methods in the next layer.
 *
 * \param timestamp \c (in) - Pointer to the timestamp.
 * \param history_identifier \c (in) - Timestamp registration time.
 * \param publications_file \c (in) - Pointer to the publications file.
 *
 * \return status code \c GT_OK, when operation succeeded, otherwise an
 * error code.
 */
int GTTimestamp_checkPublicKey(
		const GTTimestamp *timestamp,
		GT_Time_t64 history_identifier,
		const GTPublicationsFile *publications_file);

/**
 * \ingroup timestamps
 *
 * Frees memory used by timestamp.
 *
 * \param timestamp \c (in) - \c GTTimestamp object that is to be freed.
 *
 * \see #GT_free()
 */
void GTTimestamp_free(GTTimestamp *timestamp);

/**
 * \ingroup common
 *
 * Calculates a hash of given data in one shot.
 * \see #GTDataHash_open, #GTDataHash_add, #GTDataHash_close
 *
 * \param hash_algorithm \c (in) - Identifier of the hash algorithm.
 * See #GTHashAlgorithm for possible values.
 * \param data \c (in) - Pointer to the data to be hashed.
 * \param data_length \c (in) - Length of the hashed data.
 * \param data_hash \c (out) - Pointer that will receive pointer to the
 * hashed data.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTDataHash_create(int hash_algorithm,
		const unsigned char* data, size_t data_length, GTDataHash **data_hash);

/**
 * \ingroup common
 *
 * Starts a hash computation.
 * \see #GTDataHash_add, #GTDataHash_close
 *
 * \param hash_algorithm \c (in) - Identifier of the hash algorithm.
 * See #GTHashAlgorithm for possible values.
 * \param data_hash \c (out) - Pointer that will receive pointer to the
 * hash object.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTDataHash_open(int hash_algorithm, GTDataHash **data_hash);

/**
 * \ingroup common
 *
 * Adds data to an open hash computation.
 * \see #GTDataHash_open, #GTDataHash_close
 *
 * \param data_hash \c (in/out) - Pointer to the hash object.
 * \param data \c (in) - Pointer to the data to be hashed.
 * \param data_length \c (in) - Length of the hashed data.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTDataHash_add(GTDataHash *data_hash,
		const unsigned char* data, size_t data_length);

/**
 * \ingroup common
 *
 * Finalizes a hash computation.
 * \see #GTDataHash_open, #GTDataHash_add
 *
 * \param data_hash \c (in/out) - Pointer to the hash object.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTDataHash_close(GTDataHash *data_hash);

/**
 * \ingroup common
 *
 * Frees memory used by hashed data.
 *
 * \param data_hash \c (in) - \c GTDataHash object that is to be freed.
 *
 * \see #GT_free()
 */
void GTDataHash_free(GTDataHash *data_hash);

/**
 * \ingroup common
 *
 * Returns the ASN.1 OID for the given hash algorithm, in the dotted decimal format.
 * See #GTHashAlgorithm for list of supported algorithms and their OID's.
 *
 * \param hash_algorithm \c (in) - The hash algorithm whose OID is requested.
 * \return pointer to a static string (no need to free it), or NULL if the
 * input value does not represent a supported hash algorithm.
 */
const char *GTHash_oid(int hash_algorithm);

/**
 * \ingroup publications
 *
 * Decodes publications file from given byte string.
 *
 * \param data \c (in) - Pointer to buffer containing DER-encoded publications
 * file.
 * \param data_length \c (in) - Size of buffer pointed by \p data.
 * \param publications_file \c (out) - Pointer that will receive pointer to
 * decoded publications file.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTPublicationsFile_DERDecode(const void *data, size_t data_length,
		GTPublicationsFile **publications_file);

/**
 * \ingroup publications
 *
 * Extracts publication with the given index from the publications file.
 *
 * \param publications_file \c (in) - Publications file to extract from.
 * \param publication_index \c (in) - Index of the publication to extract.
 * \param publication \c (out) - Pointer to the pointer receiving extracted
 * human readable publication value as an ordinary C-string on success. This
 * value must be freed with #GT_free() later when not needed anymore.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTPublicationsFile_getByIndex(const GTPublicationsFile *publications_file,
		unsigned int publication_index, char **publication);

/**
 * \ingroup publications
 *
 * This function returns published key hash with index \p key_hash_index.
 *
 * \param publications_file \c (in) - Pointer to publications file.
 * \param key_hash_index \c (in) - Index of published key hash.
 * \param key_hash \c (out) - The output string of published key hash,
 * must be disposed of after use with #GT_free().
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTPublicationsFile_getKeyHashByIndex(
		const GTPublicationsFile *publications_file,
		unsigned int key_hash_index, char **key_hash);

/**
 * \ingroup publications
 *
 * Verifies signature of the published file.
 *
 * \param publications_file \c (in) - Publications file that is to be verified.
 * \param verification_info \c (out) - Pointer that will receive pointer to
 * publications file verification info.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTPublicationsFile_verify(const GTPublicationsFile *publications_file,
		GTPubFileVerificationInfo **verification_info);

/**
 * \ingroup publications
 *
 * Frees memory used by the publications file structure.
 *
 * \param publications_file \c (in) - \c GTPublicationsFile object that is
 * to be freed.
 *
 * \see #GT_free()
 */
void GTPublicationsFile_free(GTPublicationsFile *publications_file);

/**
 * \ingroup publications
 *
 * Extracts publication time from human readable publication string.
 *
 * \param publication \c (in) Pointer to human readable publication value.
 * \param publication_time \c (out) Pointer to the variable that receives
 * publication time.
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GTPublicationsFile_extractTimeFromRawPublication(const char *publication,
		GT_Time_t64 *publication_time);

/**
 * \ingroup verification
 *
 * Convenience function to print contents of the GTVerificationInfo
 * structure to the given output file.
 *
 * \param f \c (in) - Pointer to the output stream.
 * \param indent \c (in) - Number of spaces to put before each output line.
 * \param vinfo \c (in) - Pointer to the verification info structure.
 */
void GTVerificationInfo_print(
		FILE *f, int indent, const GTVerificationInfo *vinfo);

/**
 * \ingroup verification
 *
 * Frees memory used by verification info.
 *
 * \param verification_info \c (in) - \c GTVerificationInfo object that is
 * to be freed.
 *
 * \see #GT_free()
 */
void GTVerificationInfo_free(GTVerificationInfo *verification_info);

/**
 * \ingroup publications
 *
 * Frees memory used by publications file verification info.
 *
 * \param verification_info \c (in) - \c GTPubFileVerificationInfo object
 * that is to be freed.
 *
 * \see #GT_free()
 */
void GTPubFileVerificationInfo_free(
		GTPubFileVerificationInfo *verification_info);

/**
 * \ingroup common
 *
 * Gets human readable error string in English.
 *
 * \param error \c (in) - Guardtime status code.
 * \return error string (it is static, don't try to free it).
 *
 * \note This function only knows about the status codes defined in the base
 * module of the SDK. For descriptions of codes defined in other modules, see
 * the \c getErrorString() methods in the respective modules.
 */
const char *GT_getErrorString(int error);

/**
 * \ingroup common
 *
 * Returns the version number of the library.
 *
 * \return version number of the API, as a 4-byte integer, with the major
 * number in the highest, minor number in the second highest and build
 * number in the two lowest bytes.
 *
 * \see GT_VERSION
 */
int GT_getVersion(void);

/**
 * \ingroup common
 *
 * Initializes timestamping library. Must be called once before any
 * other API functions.
 *
 * \return status code (\c GT_OK, when operation succeeded, otherwise an
 * error code).
 */
int GT_init(void);

/**
 * \ingroup common
 *
 * Frees resources used by timestamping library. Must be called once after
 * any other API functions. No Guardtime API functions can be used after this
 * function is called.
 */
void GT_finalize(void);

/**
 * \ingroup common
 *
 * Tries to allocate memory from Guardtime library's heap.
 * This function present mainly for the benefit of other
 * modules of the Guardtime SDK.
 *
 * \param s \c (in) - The requested size of the block to allocate.
 * \return pointer to the allocated block, or \c NULL.
 *
 * \note In certain configurations (most notably when using DLLs in Windows)
 * different parts of a program may be using different memory heaps. Therefore,
 * always release memory allocated by #GT_malloc() with #GT_free().
 */
void *GT_malloc(size_t s);

/**
 * \ingroup common
 *
 * Tries to allocate memory from Guardtime library's heap.
 * This function present mainly for the benefit of other
 * modules of the Guardtime SDK.
 *
 * \param n \c (in) - The requested number of items to allocate.
 * \param s \c (in) - The size of each item.
 * \return pointer to the allocated block, or \c NULL.
 *
 * \note In certain configurations (most notably when using DLLs in Windows)
 * different parts of a program may be using different memory heaps. Therefore,
 * always release memory allocated by #GT_calloc() with #GT_free().
 */
void *GT_calloc(size_t n, size_t s);

/**
 * \ingroup common
 *
 * Tries to extend a memory block allocated from Guardtime library's heap.
 * This function present mainly for the benefit of other
 * modules of the Guardtime SDK.
 *
 * \param p \c (in) - Pointer to the original memory block.
 * \param s \c (in) - The requested new size of the block.
 * \return pointer to the allocated block, or \c NULL.
 *
 * \note In certain configurations (most notably when using DLLs in Windows)
 * different parts of a program may be using different memory heaps. Therefore,
 * always release memory allocated by #GT_realloc() with #GT_free().
 */
void *GT_realloc(void *p, size_t s);

/**
 * \ingroup common
 *
 * Frees memory (character strings, byte arrays, etc) that was allocated
 * by the Guardtime library and returned to the user. It is safe to pass null
 * pointer to this function.
 *
 * Since all Guardtime functions leave \c (out) and \c (in/out) parameters
 * intact on any errors, the following idiom could be used to ensure memory
 * clean-up in all cases:
 * <pre>
 *    void *p = NULL;
 *    ...
 *    if (GT_function(&p) == GT_OK) {
 *       ...
 *       use *p here
 *       ...
 *    }
 *    ...
 *    GT_free(p);
 * </pre>
 * Alternatively, if you want to bail out immediately on any errors to avoid
 * deep nesting of <code>if</code> statements, you could use:
 * <pre>
 *    void *p = NULL;
 *    ...
 *    if (GT_function(&p) != GT_OK) {
 *       goto cleanup;
 *    }
 *    ...
 *    use *p here
 *    ...
 * cleanup:
 *    ...
 *    GT_free(p);
 * </pre>
 *
 * \param p \c (in) - Pointer that is to be freed.
 *
 * \note In certain configurations (most notably when using DLLs in Windows)
 * different parts of a program may be using different memory heaps. Therefore,
 * only use #GT_free() to relase memory allocated with #GT_malloc(), #GT_calloc(),
 * and #GT_realloc(), or returned to you from a Guardtime SDK function.
 */
void GT_free(void *p);

/**
 * \ingroup common
 *
 * Reads contents of the given file into memory.
 *
 * \param path \c (in) Name of the file to read.
 * \param out_data \c (out) Pointer to the variable that receives pointer
 * to the output data on success. This data must be freed with #GT_free()
 * when not needed anymore.
 * \param out_size \c (out) Pointer to the variable that receives length of
 * the output data on success.
 *
 * \return \c GT_OK on success, otherwise an error code; use \c errno to get
 * more specific reason when \c GT_IO_ERROR is returned.
 *
 * \note For convenience of handling in case of text files, a zero byte is
 * always appended to the loaded data. But the size returned in \c out_size
 * is still the actual size of the file without the extra byte.
 */
int GT_loadFile(const char *path, unsigned char **out_data, size_t *out_size);

/**
 * \ingroup common
 *
 * Hashes contents of the given file.
 *
 * \param path \c (in) Name of the file to read.
 * \param hash_algorithm \c (in) - Identifier of the hash algorithm.
 * See #GTHashAlgorithm for possible values.
 * \param data_hash \c (out) - Pointer that will receive pointer to the
 * hashed data.
 *
 * \return \c GT_OK on success, otherwise an error code; use \c errno to get
 * more specific reason when \c GT_IO_ERROR is returned.
 */
int GT_hashFile(const char *path, int hash_algorithm, GTDataHash **data_hash);

/**
 * \ingroup common
 *
 * Saves the given contents into file.
 *
 * \param path \c (in) Name of the file to save.
 * \param in_data \c (in) Pointer to the data to be saved.
 * \param in_size \c (in) Size of the data to be saved.
 *
 * \return \c GT_OK on success, otherwise an error code; use \c errno to get
 * more specific reason when \c GT_IO_ERROR is returned.
 */
int GT_saveFile(const char *path, const void *in_data, size_t in_size);

/**
 * \ingroup publications
 *
 * Initializes the OpenSSL trust store.
 *
 * \param set_defaults \c (in) If non-zero, the default bundle and/or
 * directory (as defined or autodetected when the API was compiled)
 * will be added to the store lookup; otherwise the store will be
 * completely empty.
 *
 * \return \c GT_OK on success, error code otherwise.
 *
 * \note On Windows the native crypto API is used for verification of the
 * publications file signatures by default. This function overrides it and
 * OpenSSL will be used instead until #GTTruststore_finalize() is called.
 *
 * \note On platforms other than Windows, this is called automatically
 * internally only when needed.
 *
 * \note This function has no effect when called while a truststore has
 * been initialized. To reset the truststore use #GTTruststore_reset().
 */
int GTTruststore_init(int set_defaults);

/**
 * \ingroup publications
 *
 * Releases the resources used by the trust store.
 *
 * \note On Windows reverts to the native crypto API for verification of
 * the publications file signatures.
 *
 * \note This function is called automatically from #GT_finalize().
 */
void GTTruststore_finalize(void);

/**
 * \ingroup publications
 *
 * Adds a certificate bundle to the trust store lookup list.
 *
 * \param path \c (in) Path of the file to add.
 *
 * \return \c GT_OK on success, error code otherwise.
 *
 */
int GTTruststore_addLookupFile(const char *path);

/**
 * \ingroup publications
 *
 * Adds a directory to the trust store lookup list.
 *
 * \param path \c (in) Path of the directory to add.
 *
 * \return \c GT_OK on success, error code otherwise.
 *
 */
int GTTruststore_addLookupDir(const char *path);

/**
 * \ingroup publications
 *
 * Adds a PEM encoded certificate to the truststore.
 *
 * \return \c GT_OK on success, error code otherwise.
 */
int GTTruststore_addCert(const char *pem);

/**
 * \ingroup publications
 *
 * Resets the trusted CA certificate lookup locations.
 *
 * \param keep_defaults \c (in) If non-zero, the default bundle and/or
 * directory (as defined or autodetected when the API was compiled)
 * will be kept; otherwise those will be removed, too.
 *
 * \return \c GT_OK on success, error code otherwise.
 *
 */
int GTTruststore_reset(int keep_defaults);

#ifdef __cplusplus
}
#endif

#endif /* not GT_BASE_H_INCLUDED */
