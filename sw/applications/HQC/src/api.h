#ifndef PQCLEAN_HQC128_CLEAN_API_H
#define PQCLEAN_HQC128_CLEAN_API_H
/**
 * @file api.h
 * @brief NIST KEM API used by the HQC_KEM IND-CCA2 scheme
 */

#include <stdint.h>

#define PQCLEAN_HQC128_CLEAN_CRYPTO_ALGNAME                      "HQC-128"

#define PQCLEAN_HQC128_CLEAN_CRYPTO_SECRETKEYBYTES               2305
#define PQCLEAN_HQC128_CLEAN_CRYPTO_PUBLICKEYBYTES               2249
#define PQCLEAN_HQC128_CLEAN_CRYPTO_BYTES                        64
#define PQCLEAN_HQC128_CLEAN_CRYPTO_CIPHERTEXTBYTES              4433

// As a technicality, the public key is appended to the secret key in order to respect the NIST API.
// Without this constraint, PQCLEAN_HQC128_CLEAN_CRYPTO_SECRETKEYBYTES would be defined as 32

int PQCLEAN_HQC128_CLEAN_crypto_kem_keypair(uint8_t *pk, uint8_t *sk);

int PQCLEAN_HQC128_CLEAN_crypto_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);

int PQCLEAN_HQC128_CLEAN_crypto_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

#define HQC_CRYPTO_ALGNAME PQCLEAN_HQC128_CLEAN_CRYPTO_ALGNAME
#define HQC_CRYPTO_SECRETKEYBYTES PQCLEAN_HQC128_CLEAN_CRYPTO_SECRETKEYBYTES
#define HQC_CRYPTO_PUBLICKEYBYTES PQCLEAN_HQC128_CLEAN_CRYPTO_PUBLICKEYBYTES
#define HQC_CRYPTO_BYTES PQCLEAN_HQC128_CLEAN_CRYPTO_BYTES
#define HQC_CRYPTO_CIPHERTEXTBYTES PQCLEAN_HQC128_CLEAN_CRYPTO_CIPHERTEXTBYTES

#define hqc_crypto_kem_keypair PQCLEAN_HQC128_CLEAN_crypto_kem_keypair
#define hqc_crypto_kem_enc PQCLEAN_HQC128_CLEAN_crypto_kem_enc
#define hqc_crypto_kem_dec PQCLEAN_HQC128_CLEAN_crypto_kem_dec

#endif
