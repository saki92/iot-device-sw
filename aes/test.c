#include <aes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MSG_SIZE 16
#define IV_VEC "XuCAC83n2miCWNFq"

/*int main() {
  const uint8_t key[32] =
      AES_KEY; // Replace with your own key
  const uint8_t iv[16] =
      IV_VEC; // Replace with your own IV

  const char *original_message = "Hello, AES Encryption!";
  size_t message_len = strlen(original_message);

  // Ensure the ciphertext buffer is large enough to hold the encrypted data
  size_t ciphertext_len = message_len + 16; // Sufficient buffer size

  uint8_t ciphertext[ciphertext_len];
  uint8_t decrypted_message[ciphertext_len]; // Same size as ciphertext for
                                             // simplicity

  // Encrypt the message
  encryptAES((const uint8_t *)original_message, message_len, key, iv,
             ciphertext);

  // Decrypt the ciphertext
  decryptAES(ciphertext, ciphertext_len, key, iv, decrypted_message);

  // Print the original and decrypted messages
  printf("Original Message: %s\n", original_message);
  printf("Decrypted Message: %s\n", decrypted_message);

  return 0;
}*/

int main(void) {
  uint8_t decData[MSG_SIZE] = {0};
  uint8_t key[AES_KEY_LENGTH_BYTE] = AES_KEY;
  for (int i=0; i<MSG_SIZE; i++) {
    decData[i] = i;
  }
  uint8_t iv[AES_KEY_LENGTH_BYTE] = IV_VEC;
  uint8_t out_buffer[AES_MSG_SIZE] = {0};
  encryptAES(decData, MSG_SIZE, (uint8_t *)key, (uint8_t *)iv, out_buffer);
  uint8_t decDataOut[MSG_SIZE] = {0};
  decryptAES(out_buffer, MSG_SIZE+MSG_SIZE, (uint8_t *)key, (uint8_t *)iv, decDataOut);

  for (int i=0; i<MSG_SIZE; i++) {
    printf("out %d: %d\n", i, decDataOut[i]);
    if (decDataOut[i] != decData[i]) {
      printf("Failed\n");
      return -1;
    }
  }

  printf("Pass\n");
  return 0;
}
