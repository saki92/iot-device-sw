#ifndef AES_BASE_H
#define AES_BASE_H
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdint.h>
#include <stdio.h>

#define AES_KEY "SdP5X5BVhwyT50rvAE2qK6sy2UL66KAi"
#define AES_KEY_LENGTH_BYTE 32
#define AES_IV_LENGTH_BYTE 16
#define AES_MSG_SIZE 128

void encryptAES(const uint8_t *input, size_t input_len, const uint8_t *key,
                const uint8_t *iv, uint8_t *output);

void decryptAES(const uint8_t *ciphertext, size_t ciphertext_len,
                const uint8_t *key, const uint8_t *iv, uint8_t *output);
#endif
