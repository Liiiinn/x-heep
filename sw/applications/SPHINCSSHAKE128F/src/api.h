#ifndef PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_API_H
#define PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_API_H

#include <stddef.h>
#include <stdint.h>

#define PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_ALGNAME "SPHINCS+-shake-128f-simple"

#define PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_SECRETKEYBYTES 64
#define PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_PUBLICKEYBYTES 32
#define PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_BYTES          17088

#define PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_SEEDBYTES      48

/*
 * Returns the length of a secret key, in bytes
 */
size_t PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_secretkeybytes(void);

/*
 * Returns the length of a public key, in bytes
 */
size_t PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_publickeybytes(void);

/*
 * Returns the length of a signature, in bytes
 */
size_t PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_bytes(void);

/*
 * Returns the length of the seed required to generate a key pair, in bytes
 */
size_t PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_seedbytes(void);

/*
 * Generates a SPHINCS+ key pair given a seed.
 * Format sk: [SK_SEED || SK_PRF || PUB_SEED || root]
 * Format pk: [root || PUB_SEED]
 */
int PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_seed_keypair(uint8_t *pk, uint8_t *sk,
        const uint8_t *seed);

/*
 * Generates a SPHINCS+ key pair.
 * Format sk: [SK_SEED || SK_PRF || PUB_SEED || root]
 * Format pk: [root || PUB_SEED]
 */
int PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_keypair(uint8_t *pk, uint8_t *sk);

/**
 * Returns an array containing a detached signature.
 */
int PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_signature(uint8_t *sig, size_t *siglen,
        const uint8_t *m, size_t mlen,
        const uint8_t *sk);

/**
 * Verifies a detached signature and message under a given public key.
 */
int PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_verify(const uint8_t *sig, size_t siglen,
        const uint8_t *m, size_t mlen,
        const uint8_t *pk);

/**
 * Returns an array containing the signature followed by the message.
 */
int PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign(uint8_t *sm, size_t *smlen,
        const uint8_t *m, size_t mlen,
        const uint8_t *sk);

/**
 * Verifies a given signature-message pair under a given public key.
 */
int PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_open(uint8_t *m, size_t *mlen,
        const uint8_t *sm, size_t smlen,
        const uint8_t *pk);

#define PQ_APP_ALGNAME PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_ALGNAME
#define PQ_APP_PUBLICKEYBYTES PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_PUBLICKEYBYTES
#define PQ_APP_SECRETKEYBYTES PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_SECRETKEYBYTES
#define PQ_APP_SIGNATUREBYTES PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_CRYPTO_BYTES

#define pq_app_sign_keypair PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_keypair
#define pq_app_sign_signature PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_signature
#define pq_app_sign_verify PQCLEAN_SPHINCSSHAKE128FSIMPLE_CLEAN_crypto_sign_verify
#endif
