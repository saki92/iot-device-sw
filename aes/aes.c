#include <aes.h>
#include <stdint.h>
#include <stdio.h>

void encryptAES(const uint8_t *input, size_t input_len, const uint8_t *key,
                const uint8_t *iv, uint8_t *output) {
  EVP_CIPHER_CTX *ctx;
  int ciphertext_len;

  ctx = EVP_CIPHER_CTX_new();

  // Initialize AES encryption
  EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);

  // Perform encryption
  EVP_EncryptUpdate(ctx, output, &ciphertext_len, input, input_len);

  // Finalize the encryption
  EVP_EncryptFinal_ex(ctx, output + ciphertext_len, &ciphertext_len);

  EVP_CIPHER_CTX_free(ctx);
}

void decryptAES(const uint8_t *ciphertext, size_t ciphertext_len,
                const uint8_t *key, const uint8_t *iv, uint8_t *output) {
  EVP_CIPHER_CTX *ctx;
  int plaintext_len;

  ctx = EVP_CIPHER_CTX_new();

  // Initialize AES decryption
  EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);

  // Perform decryption
  EVP_DecryptUpdate(ctx, output, &plaintext_len, ciphertext, ciphertext_len);

  // Finalize the decryption
  EVP_DecryptFinal_ex(ctx, output + plaintext_len, &plaintext_len);

  EVP_CIPHER_CTX_free(ctx);
}
