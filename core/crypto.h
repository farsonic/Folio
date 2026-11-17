/*
 * crypto.h - the ciphers and hashes of the PDF Standard Security Handler: MD5, RC4, AES (CBC, 128/192/256-bit keys)
 * and SHA-256/384/512 (for revision 6's password hash).
 * Portable C99 (the native tests and vbcc for the Amiga's 68040).
 */
#ifndef FOLIO_CRYPTO_H
#define FOLIO_CRYPTO_H

#include "util.h"

/* MD5, in one go or incrementally */
typedef struct { u32 s[4]; u32 lo, hi; u8 buf[64]; } Md5;
void md5_init(Md5 *m);
void md5_update(Md5 *m, const u8 *in, size_t n);
void md5_final(Md5 *m, u8 out[16]);
void md5(const u8 *in, size_t n, u8 out[16]);

/* RC4 (keys of 1-256 bytes) */
typedef struct { u8 s[256]; u8 i, j; } Rc4;
void rc4_init(Rc4 *r, const u8 *key, int keylen);
void rc4_crypt(Rc4 *r, const u8 *in, u8 *out, size_t n);       /* in == out allowed */

/* AES-CBC decryption as PDF lays it out: the first 16 bytes of in are the IV. key is 16 or 32 bytes (24 also works).
   PKCS#5 padding is removed (when it is invalid, the bytes are kept). Returns malloc'd plaintext (at least 1 byte
   allocated), NULL if n < 16, the key length is bad, or out of memory. A partial final block is ignored */
u8 *aes_cbc_decrypt(const u8 *key, int keylen, const u8 *in, size_t n, size_t *outlen);
/* raw single blocks and CBC without padding (n a multiple of 16; a partial final block is left out): for the R6
   hash and for encrypting when writing. With a bad key length the block helpers write zeros and the CBC one
   returns NULL, as it does when out of memory */
void aes_encrypt_block(const u8 *key, int keylen, const u8 in[16], u8 out[16]);
u8  *aes_cbc_encrypt_nopad(const u8 *key, int keylen, const u8 iv[16], const u8 *in, size_t n);
void aes_ecb_decrypt_block(const u8 *key, int keylen, const u8 in[16], u8 out[16]);

/* the SHA-2 hashes */
void sha256(const u8 *in, size_t n, u8 out[32]);
void sha384(const u8 *in, size_t n, u8 out[48]);
void sha512(const u8 *in, size_t n, u8 out[64]);

#endif
