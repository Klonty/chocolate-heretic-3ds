//
// mbedtls configuration for the 3DS port: just enough of a TLS 1.2 client
// to download a file from GitHub (port3ds/p3ds_getwad.c).  Selected with
// -DMBEDTLS_CONFIG_FILE in the Makefile, replacing mbedtls/config.h.
//
// The download is checked against a pinned SHA-256 afterwards, so the
// certificate chain is not verified (the 3DS has no usable CA store); TLS
// is used because GitHub only serves HTTPS.
//

#ifndef MBEDTLS_CONFIG_3DS_H
#define MBEDTLS_CONFIG_3DS_H

// System support.  Randomness comes from the 3DS's PS service, added as
// an entropy source at run time.
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_NO_PLATFORM_ENTROPY

// TLS 1.2 client.
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE

#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED

// Ciphers: AES-GCM and ChaCha20-Poly1305 (CBC as a fallback).
#define MBEDTLS_CIPHER_C
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_CIPHER_MODE_CBC

// Hashes.  SHA-1 is left out: its source file name clashes with Chocolate
// Doom's sha1.c, and nothing here needs it.
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C

// Public key crypto.
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM

// Certificates (parsed, not verified).
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

// Random numbers.
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

#include "mbedtls/check_config.h"

#endif
