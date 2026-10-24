#ifndef MU_AES_H
#define MU_AES_H

#include <stddef.h>

/* AES-128 ECB decrypt in place-safe form. len must be a multiple of 16. */
void mu_aes128_ecb_decrypt(const unsigned char key[16], const unsigned char *in,
                           unsigned char *out, size_t len);

/* AES-128 ECB encrypt, used only by the self-test. */
void mu_aes128_ecb_encrypt(const unsigned char key[16], const unsigned char *in,
                           unsigned char *out, size_t len);

#endif
