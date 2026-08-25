#ifndef MCBL_CRYPTO_H
#define MCBL_CRYPTO_H
/*
 * McBL# crypto.* — Hashing and Encoding
 * ========================================
 * crypto.md5(data)          → hex string MD5
 * crypto.sha256(data)       → hex string SHA-256
 * crypto.sha1(data)         → hex string SHA-1
 * crypto.fnv1a(data)        → uint64 FNV-1a hash
 * crypto.crc32(data)        → uint32 CRC-32
 * crypto.base64_enc(data)   → base64 string
 * crypto.base64_dec(str)    → decoded bytes
 * crypto.hex_enc(data)      → hex string
 * crypto.hex_dec(hex)       → bytes
 * crypto.url_enc(str)       → URL-encoded string
 * crypto.url_dec(str)       → URL-decoded string
 * crypto.rand_bytes(n)      → n random bytes
 * crypto.uuid4()            → UUID v4 string
 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

char    *mcbl_crypto_md5     (const void *data, size_t len);
char    *mcbl_crypto_sha256  (const void *data, size_t len);
char    *mcbl_crypto_sha1    (const void *data, size_t len);
uint64_t mcbl_crypto_fnv1a   (const void *data, size_t len);
uint32_t mcbl_crypto_crc32   (const void *data, size_t len);
char    *mcbl_crypto_b64_enc (const void *data, size_t len);
void    *mcbl_crypto_b64_dec (const char *str,  size_t *out_len);
char    *mcbl_crypto_hex_enc (const void *data, size_t len);
void    *mcbl_crypto_hex_dec (const char *hex,  size_t *out_len);
char    *mcbl_crypto_url_enc (const char *str);
char    *mcbl_crypto_url_dec (const char *str);
void    *mcbl_crypto_rand_bytes(size_t n);
char    *mcbl_crypto_uuid4   (void);

#ifdef __cplusplus
}
#endif
#endif /* MCBL_CRYPTO_H */
