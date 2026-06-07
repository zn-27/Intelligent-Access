#include "crypto-utils.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <cstring>
#include <stdexcept>
#include <ctime>
#include <iostream>

namespace ns3 {

// ============================================================
// generate_128bit_prime
// ============================================================
mpz_class generate_128bit_prime()
{
    // 使用GMP随机数生成器
    gmp_randclass randGen(gmp_randinit_default);

    // 用当前时间 + 地址做种子
    static int seedCounter = 0;
    randGen.seed(static_cast<unsigned long>(std::time(nullptr)) + (++seedCounter) * 7919);

    // 生成128位随机数
    mpz_class candidate = randGen.get_z_bits(128);

    // 确保恰好128位
    mpz_setbit(candidate.get_mpz_t(), 127);

    // 确保是奇数
    mpz_setbit(candidate.get_mpz_t(), 0);

    // 找下一个素数
    mpz_nextprime(candidate.get_mpz_t(), candidate.get_mpz_t());

    return candidate;
}

// ============================================================
// ExtendedChebyshevKeyExchange
// ============================================================
ExtendedChebyshevKeyExchange::ExtendedChebyshevKeyExchange(const mpz_class& modulus)
    : m_modulus(modulus)
{
}

ExtendedChebyshevKeyExchange::~ExtendedChebyshevKeyExchange()
{
}

unsigned char*
ExtendedChebyshevKeyExchange::generate_private_key_bytes(size_t* outLen)
{
    // 生成128位随机私钥
    gmp_randclass randGen(gmp_randinit_default);
    static int seedCounter = 0;
    randGen.seed(static_cast<unsigned long>(std::time(nullptr)) + (++seedCounter) * 6271);

    mpz_class privKey = randGen.get_z_bits(128);
    // 确保恰好128位，且 > 0
    mpz_setbit(privKey.get_mpz_t(), 127);
    if (mpz_cmp_ui(privKey.get_mpz_t(), 0) == 0)
    {
        mpz_set_ui(privKey.get_mpz_t(), 1);
    }

    // 导出为字节数组
    size_t byteLen = (mpz_sizeinbase(privKey.get_mpz_t(), 2) + 7) / 8;
    unsigned char* bytes = new unsigned char[byteLen];
    size_t count = 0;
    mpz_export(bytes, &count, 1, 1, 0, 0, privKey.get_mpz_t());

    // 如果导出字节不足，左侧补零
    if (count < byteLen)
    {
        size_t pad = byteLen - count;
        unsigned char* padded = new unsigned char[byteLen];
        std::memset(padded, 0, pad);
        std::memcpy(padded + pad, bytes, count);
        delete[] bytes;
        bytes = padded;
    }

    *outLen = byteLen;
    return bytes;
}

unsigned char*
ExtendedChebyshevKeyExchange::compute_public_key_bytes(
    const unsigned char* privKeyBytes, size_t privKeyLen,
    const unsigned char* basePointBytes, size_t baseLen,
    size_t* outLen)
{
    // 将字节转换为 mpz_class
    mpz_class privKey;
    mpz_import(privKey.get_mpz_t(), privKeyLen, 1, 1, 0, 0, privKeyBytes);

    mpz_class basePoint;
    mpz_import(basePoint.get_mpz_t(), baseLen, 1, 1, 0, 0, basePointBytes);

    // 计算公钥 = T_privKey(basePoint) mod m_modulus
    mpz_class pubKey = chebyshev_T(privKey, basePoint);

    // 导出为字节数组
    size_t byteLen = (mpz_sizeinbase(pubKey.get_mpz_t(), 2) + 7) / 8;
    if (byteLen == 0) byteLen = 1;
    unsigned char* bytes = new unsigned char[byteLen];
    size_t count = 0;
    mpz_export(bytes, &count, 1, 1, 0, 0, pubKey.get_mpz_t());

    if (count < byteLen)
    {
        size_t pad = byteLen - count;
        unsigned char* padded = new unsigned char[byteLen];
        std::memset(padded, 0, pad);
        std::memcpy(padded + pad, bytes, count);
        delete[] bytes;
        bytes = padded;
    }

    *outLen = byteLen;
    return bytes;
}

unsigned char*
ExtendedChebyshevKeyExchange::compute_shared_secret_bytes(
    const unsigned char* privKeyBytes, size_t privKeyLen,
    const unsigned char* peerPubKeyBytes, size_t peerPubLen,
    size_t* outLen)
{
    // 将字节转换为 mpz_class
    mpz_class privKey;
    mpz_import(privKey.get_mpz_t(), privKeyLen, 1, 1, 0, 0, privKeyBytes);

    mpz_class peerPubKey;
    mpz_import(peerPubKey.get_mpz_t(), peerPubLen, 1, 1, 0, 0, peerPubKeyBytes);

    // 计算共享密钥 = T_privKey(peerPubKey) mod m_modulus
    mpz_class sharedSecret = chebyshev_T(privKey, peerPubKey);

    // 导出为字节数组
    size_t byteLen = (mpz_sizeinbase(sharedSecret.get_mpz_t(), 2) + 7) / 8;
    if (byteLen == 0) byteLen = 1;
    unsigned char* bytes = new unsigned char[byteLen];
    size_t count = 0;
    mpz_export(bytes, &count, 1, 1, 0, 0, sharedSecret.get_mpz_t());

    if (count < byteLen)
    {
        size_t pad = byteLen - count;
        unsigned char* padded = new unsigned char[byteLen];
        std::memset(padded, 0, pad);
        std::memcpy(padded + pad, bytes, count);
        delete[] bytes;
        bytes = padded;
    }

    *outLen = byteLen;
    return bytes;
}

// --- Chebyshev 多项式快速计算 ---
mpz_class
ExtendedChebyshevKeyExchange::chebyshev_T(const mpz_class& n, const mpz_class& x) const
{
    // 处理边界情况
    if (mpz_cmp_ui(n.get_mpz_t(), 0) == 0)
    {
        return mpz_class(1);
    }
    if (mpz_cmp_ui(n.get_mpz_t(), 1) == 0)
    {
        mpz_class result = x % m_modulus;
        if (mpz_cmp_ui(result.get_mpz_t(), 0) < 0)
            mpz_add(result.get_mpz_t(), result.get_mpz_t(), m_modulus.get_mpz_t());
        return result;
    }

    // 使用二进制倍增法: O(log n)
    // 维护 (T_k(x), T_{k+1}(x))
    // 倍增公式:
    //   T_{2k}(x)   = 2 * T_k(x)^2 - 1       (mod p)
    //   T_{2k+1}(x) = 2 * T_k(x) * T_{k+1}(x) - x  (mod p)

    mpz_class Tk = x % m_modulus;           // T_1(x)
    if (mpz_cmp_ui(Tk.get_mpz_t(), 0) < 0)
        mpz_add(Tk.get_mpz_t(), Tk.get_mpz_t(), m_modulus.get_mpz_t());

    // T_2(x) = 2*x^2 - 1  (mod p)
    mpz_class Tkp1 = (2 * Tk * Tk - 1) % m_modulus;
    if (mpz_cmp_ui(Tkp1.get_mpz_t(), 0) < 0)
        mpz_add(Tkp1.get_mpz_t(), Tkp1.get_mpz_t(), m_modulus.get_mpz_t());

    // 获取 n 的二进制位长
    int msb = static_cast<int>(mpz_sizeinbase(n.get_mpz_t(), 2)) - 1;

    // 从次高位开始处理
    mpz_class x_mod = x % m_modulus;
    if (mpz_cmp_ui(x_mod.get_mpz_t(), 0) < 0)
        mpz_add(x_mod.get_mpz_t(), x_mod.get_mpz_t(), m_modulus.get_mpz_t());

    for (int i = msb - 1; i >= 0; i--)
    {
        if (mpz_tstbit(n.get_mpz_t(), i))
        {
            // bit = 1: (T_k, T_{k+1}) → (T_{2k+1}, T_{2k+2})
            // T_{2k+1} = 2 * T_k * T_{k+1} - x
            mpz_class newTk = (2 * Tk * Tkp1 - x_mod) % m_modulus;
            if (mpz_cmp_ui(newTk.get_mpz_t(), 0) < 0)
                mpz_add(newTk.get_mpz_t(), newTk.get_mpz_t(), m_modulus.get_mpz_t());

            // T_{2k+2} = 2 * T_{k+1}^2 - 1
            mpz_class newTkp1 = (2 * Tkp1 * Tkp1 - 1) % m_modulus;
            if (mpz_cmp_ui(newTkp1.get_mpz_t(), 0) < 0)
                mpz_add(newTkp1.get_mpz_t(), newTkp1.get_mpz_t(), m_modulus.get_mpz_t());

            Tk = newTk;
            Tkp1 = newTkp1;
        }
        else
        {
            // bit = 0: (T_k, T_{k+1}) → (T_{2k}, T_{2k+1})
            // T_{2k} = 2 * T_k^2 - 1
            mpz_class newTk = (2 * Tk * Tk - 1) % m_modulus;
            if (mpz_cmp_ui(newTk.get_mpz_t(), 0) < 0)
                mpz_add(newTk.get_mpz_t(), newTk.get_mpz_t(), m_modulus.get_mpz_t());

            // T_{2k+1} = 2 * T_k * T_{k+1} - x
            mpz_class newTkp1 = (2 * Tk * Tkp1 - x_mod) % m_modulus;
            if (mpz_cmp_ui(newTkp1.get_mpz_t(), 0) < 0)
                mpz_add(newTkp1.get_mpz_t(), newTkp1.get_mpz_t(), m_modulus.get_mpz_t());

            Tk = newTk;
            Tkp1 = newTkp1;
        }
    }

    return Tk;
}

// --- 静态工具方法 ---
std::string
ExtendedChebyshevKeyExchange::bytes_to_hex(const unsigned char* bytes, size_t len)
{
    static const char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; i++)
    {
        result.push_back(hexChars[(bytes[i] >> 4) & 0x0F]);
        result.push_back(hexChars[bytes[i] & 0x0F]);
    }
    return result;
}

unsigned char*
ExtendedChebyshevKeyExchange::hex_to_bytes(const std::string& hex, size_t* outLen)
{
    if (hex.empty() || hex.length() % 2 != 0)
    {
        *outLen = 0;
        return nullptr;
    }

    size_t byteLen = hex.length() / 2;
    unsigned char* bytes = new unsigned char[byteLen];

    for (size_t i = 0; i < byteLen; i++)
    {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];

        auto hexVal = [](char c) -> unsigned char {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };

        bytes[i] = (hexVal(high) << 4) | hexVal(low);
    }

    *outLen = byteLen;
    return bytes;
}

void
ExtendedChebyshevKeyExchange::free_bytes(unsigned char* bytes)
{
    delete[] bytes;
}

// ============================================================
// CryptoUtils
// ============================================================
unsigned char*
CryptoUtils::hmacSha256First64Bits(
    const char* data, size_t dataLen,
    const unsigned char* key, size_t keyLen)
{
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int resultLen = 0;

    HMAC(EVP_sha256(),
         key, static_cast<int>(keyLen),
         reinterpret_cast<const unsigned char*>(data), dataLen,
         result, &resultLen);

    // resultLen 应该是32字节(SHA-256)，取前8字节（64位）
    unsigned char* out = new unsigned char[8];
    std::memcpy(out, result, 8);
    return out;
}

std::string
CryptoUtils::bytesToHex(const unsigned char* bytes, size_t len)
{
    static const char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; i++)
    {
        result.push_back(hexChars[(bytes[i] >> 4) & 0x0F]);
        result.push_back(hexChars[bytes[i] & 0x0F]);
    }
    return result;
}

void
CryptoUtils::freeBytes(unsigned char* bytes)
{
    delete[] bytes;
}

} // namespace ns3
