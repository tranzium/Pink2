#ifndef OPENSSL_PTR_H
#define OPENSSL_PTR_H

#include <memory>
#include <openssl/evp.h>
#include <openssl/ec.h>

struct EVP_MD_CTX_Deleter     { void operator()(EVP_MD_CTX* p)     const { EVP_MD_CTX_free(p); } };
struct EVP_CIPHER_CTX_Deleter { void operator()(EVP_CIPHER_CTX* p) const { EVP_CIPHER_CTX_free(p); } };
struct EVP_MAC_Deleter        { void operator()(EVP_MAC* p)        const { EVP_MAC_free(p); } };
struct EVP_MAC_CTX_Deleter    { void operator()(EVP_MAC_CTX* p)    const { EVP_MAC_CTX_free(p); } };
// EC_KEY is deprecated in OpenSSL 3.0 but needed for the fallback DER decoder
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
struct EC_KEY_Deleter         { void operator()(EC_KEY* p)         const { EC_KEY_free(p); } };
#pragma GCC diagnostic pop

using EVP_MD_CTX_ptr     = std::unique_ptr<EVP_MD_CTX,     EVP_MD_CTX_Deleter>;
using EVP_CIPHER_CTX_ptr = std::unique_ptr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_Deleter>;
using EVP_MAC_ptr        = std::unique_ptr<EVP_MAC,        EVP_MAC_Deleter>;
using EVP_MAC_CTX_ptr    = std::unique_ptr<EVP_MAC_CTX,    EVP_MAC_CTX_Deleter>;
using EC_KEY_ptr         = std::unique_ptr<EC_KEY,          EC_KEY_Deleter>;

#endif // OPENSSL_PTR_H
