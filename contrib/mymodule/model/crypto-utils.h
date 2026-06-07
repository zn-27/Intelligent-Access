#ifndef CRYPTO_UTILS_H
#define CRYPTO_UTILS_H

#include <gmpxx.h>
#include <string>

namespace ns3 {

/**
 * \brief 生成128位随机素数，用作Chebyshev密钥交换的模数
 */
mpz_class generate_128bit_prime();

/**
 * \brief 扩展Chebyshev多项式密钥交换
 *
 * 利用Chebyshev多项式的半群性质 T_a(T_b(x)) = T_{ab}(x) = T_b(T_a(x))
 * 在有限域 F_p 上实现密钥协商。
 */
class ExtendedChebyshevKeyExchange
{
public:
    /**
     * \param modulus Chebyshev多项式计算的模数（素数p）
     */
    explicit ExtendedChebyshevKeyExchange(const mpz_class& modulus);

    ~ExtendedChebyshevKeyExchange();

    /**
     * \brief 生成随机私钥
     * \param outLen [out] 私钥字节长度
     * \return 新分配的私钥字节数组（调用者负责释放，使用 free_bytes）
     */
    unsigned char* generate_private_key_bytes(size_t* outLen);

    /**
     * \brief 计算公钥 = T_privateKey(basePoint) mod modulus
     * \param privKeyBytes 私钥字节数组
     * \param privKeyLen 私钥字节长度
     * \param basePointBytes 基点字节数组（通常 x=2）
     * \param baseLen 基点字节长度
     * \param outLen [out] 公钥字节长度
     * \return 新分配的公钥字节数组
     */
    unsigned char* compute_public_key_bytes(
        const unsigned char* privKeyBytes, size_t privKeyLen,
        const unsigned char* basePointBytes, size_t baseLen,
        size_t* outLen);

    /**
     * \brief 计算共享密钥 = T_myPrivateKey(peerPublicKey) mod modulus
     * \param privKeyBytes 本端私钥
     * \param privKeyLen 本端私钥长度
     * \param peerPubKeyBytes 对端公钥
     * \param peerPubLen 对端公钥长度
     * \param outLen [out] 共享密钥字节长度
     * \return 新分配的共享密钥字节数组
     */
    unsigned char* compute_shared_secret_bytes(
        const unsigned char* privKeyBytes, size_t privKeyLen,
        const unsigned char* peerPubKeyBytes, size_t peerPubLen,
        size_t* outLen);

    // --- 静态工具方法 ---

    /** 字节数组 → hex字符串 */
    static std::string bytes_to_hex(const unsigned char* bytes, size_t len);

    /** hex字符串 → 字节数组 */
    static unsigned char* hex_to_bytes(const std::string& hex, size_t* outLen);

    /** 释放由本类方法分配的字节数组 */
    static void free_bytes(unsigned char* bytes);

private:
    mpz_class m_modulus;

    /**
     * \brief 使用二进制倍增法计算 Chebyshev 多项式 T_n(x) mod p
     *
     * 算法: O(log n) 时间
     *   T_0(x) = 1
     *   T_1(x) = x
     *   T_{2k}(x)   = 2·T_k(x)² - 1     (mod p)
     *   T_{2k+1}(x) = 2·T_k(x)·T_{k+1}(x) - x  (mod p)
     */
    mpz_class chebyshev_T(const mpz_class& n, const mpz_class& x) const;
};

/**
 * \brief 加密工具类（HMAC-SHA256等）
 */
class CryptoUtils
{
public:
    /**
     * \brief 计算 HMAC-SHA256 的前64位（8字节）
     * \param data 消息数据
     * \param dataLen 消息长度
     * \param key HMAC密钥
     * \param keyLen 密钥长度
     * \return 新分配的8字节HMAC结果（调用者使用 freeBytes 释放）
     */
    static unsigned char* hmacSha256First64Bits(
        const char* data, size_t dataLen,
        const unsigned char* key, size_t keyLen);

    /**
     * \brief 字节数组 → hex字符串
     */
    static std::string bytesToHex(const unsigned char* bytes, size_t len);

    /**
     * \brief 释放由本类方法分配的字节数组
     */
    static void freeBytes(unsigned char* bytes);
};

} // namespace ns3

#endif // CRYPTO_UTILS_H
