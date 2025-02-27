/*
 * Copyright (c) 2020, the SerenityOS developers.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/Random.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/EventLoop.h>
#include <LibCore/File.h>
#include <LibCrypto/Authentication/HMAC.h>
#include <LibCrypto/BigInt/UnsignedBigInteger.h>
#include <LibCrypto/Cipher/AES.h>
#include <LibCrypto/Hash/MD5.h>
#include <LibCrypto/Hash/SHA1.h>
#include <LibCrypto/Hash/SHA2.h>
#include <LibCrypto/PK/RSA.h>
#include <LibLine/Editor.h>
#include <LibTLS/TLSv12.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

static const char* secret_key = "WellHelloFreinds";
static const char* suite = nullptr;
static const char* filename = nullptr;
static const char* server = nullptr;
static int key_bits = 128;
static bool binary = false;
static bool interactive = false;
static bool run_tests = false;
static int port = 443;

static struct timeval start_time {
    0, 0
};
static struct timezone tz;
static bool encrypting = true;

constexpr const char* DEFAULT_DIGEST_SUITE { "HMAC-SHA256" };
constexpr const char* DEFAULT_HASH_SUITE { "SHA256" };
constexpr const char* DEFAULT_CIPHER_SUITE { "AES_CBC" };
constexpr const char* DEFAULT_SERVER { "www.google.com" };

// listAllTests
// Cipher
int aes_cbc_tests();

// Hash
int md5_tests();
int sha1_tests();
int sha256_tests();
int sha512_tests();

// Authentication
int hmac_md5_tests();
int hmac_sha256_tests();
int hmac_sha512_tests();

// Public-Key
int rsa_tests();

// TLS
int tls_tests();

// Big Integer
int bigint_tests();

// stop listing tests

void print_buffer(const ByteBuffer& buffer, int split)
{
    for (size_t i = 0; i < buffer.size(); ++i) {
        if (split > 0) {
            if (i % split == 0 && i) {
                printf("    ");
                for (size_t j = i - split; j < i; ++j) {
                    auto ch = buffer[j];
                    printf("%c", ch >= 32 && ch <= 127 ? ch : '.'); // silly hack
                }
                puts("");
            }
        }
        printf("%02x ", buffer[i]);
    }
    puts("");
}

Core::EventLoop loop;
int run(Function<void(const char*, size_t)> fn)
{
    if (interactive) {
        auto editor = Line::Editor::construct();
        editor->initialize();
        for (;;) {
            auto line_result = editor->get_line("> ");

            if (line_result.is_error())
                break;
            auto& line = line_result.value();

            if (line == ".wait") {
                loop.exec();
            } else {
                fn(line.characters(), line.length());
                loop.pump();
            }
        }
    } else {
        if (filename == nullptr) {
            puts("must specify a file name");
            return 1;
        }
        if (!Core::File::exists(filename)) {
            puts("File does not exist");
            return 1;
        }
        auto file = Core::File::open(filename, Core::IODevice::OpenMode::ReadOnly);
        if (file.is_error()) {
            printf("That's a weird file man...\n");
            return 1;
        }
        auto buffer = file.value()->read_all();
        fn((const char*)buffer.data(), buffer.size());
        loop.exec();
    }
    return 0;
}

void tls(const char* message, size_t len)
{
    static RefPtr<TLS::TLSv12> tls;
    static ByteBuffer write {};
    if (!tls) {
        tls = TLS::TLSv12::construct(nullptr);
        tls->connect(server ?: DEFAULT_SERVER, port);
        tls->on_tls_ready_to_read = [](auto& tls) {
            auto buffer = tls.read();
            if (buffer.has_value())
                fprintf(stdout, "%.*s", (int)buffer.value().size(), buffer.value().data());
        };
        tls->on_tls_ready_to_write = [&](auto&) {
            if (write.size()) {
                tls->write(write);
                write.clear();
            }
        };
        tls->on_tls_error = [&](auto) {
            loop.quit(1);
        };
        tls->on_tls_finished = [&]() {
            loop.quit(0);
        };
    }
    write.append(message, len);
    write.append("\r\n", 2);
}

void aes_cbc(const char* message, size_t len)
{
    auto buffer = ByteBuffer::wrap(message, len);
    // FIXME: Take iv as an optional parameter
    auto iv = ByteBuffer::create_zeroed(Crypto::Cipher::AESCipher::block_size());

    if (encrypting) {
        Crypto::Cipher::AESCipher::CBCMode cipher(ByteBuffer::wrap(secret_key, strlen(secret_key)), key_bits, Crypto::Cipher::Intent::Encryption);

        auto enc = cipher.create_aligned_buffer(buffer.size());
        cipher.encrypt(buffer, enc, iv);

        if (binary)
            printf("%.*s", (int)enc.size(), enc.data());
        else
            print_buffer(enc, Crypto::Cipher::AESCipher::block_size());
    } else {
        Crypto::Cipher::AESCipher::CBCMode cipher(ByteBuffer::wrap(secret_key, strlen(secret_key)), key_bits, Crypto::Cipher::Intent::Decryption);
        auto dec = cipher.create_aligned_buffer(buffer.size());
        cipher.decrypt(buffer, dec, iv);
        printf("%.*s\n", (int)dec.size(), dec.data());
    }
}

void md5(const char* message, size_t len)
{
    auto digest = Crypto::Hash::MD5::hash((const u8*)message, len);
    if (binary)
        printf("%.*s", (int)Crypto::Hash::MD5::digest_size(), digest.data);
    else
        print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::MD5::digest_size()), -1);
}

void hmac_md5(const char* message, size_t len)
{
    Crypto::Authentication::HMAC<Crypto::Hash::MD5> hmac(secret_key);
    auto mac = hmac.process((const u8*)message, len);
    if (binary)
        printf("%.*s", (int)hmac.digest_size(), mac.data);
    else
        print_buffer(ByteBuffer::wrap(mac.data, hmac.digest_size()), -1);
}

void sha1(const char* message, size_t len)
{
    auto digest = Crypto::Hash::SHA1::hash((const u8*)message, len);
    if (binary)
        printf("%.*s", (int)Crypto::Hash::SHA1::digest_size(), digest.data);
    else
        print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA1::digest_size()), -1);
}

void sha256(const char* message, size_t len)
{
    auto digest = Crypto::Hash::SHA256::hash((const u8*)message, len);
    if (binary)
        printf("%.*s", (int)Crypto::Hash::SHA256::digest_size(), digest.data);
    else
        print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA256::digest_size()), -1);
}

void hmac_sha256(const char* message, size_t len)
{
    Crypto::Authentication::HMAC<Crypto::Hash::SHA256> hmac(secret_key);
    auto mac = hmac.process((const u8*)message, len);
    if (binary)
        printf("%.*s", (int)hmac.digest_size(), mac.data);
    else
        print_buffer(ByteBuffer::wrap(mac.data, hmac.digest_size()), -1);
}

void sha512(const char* message, size_t len)
{
    auto digest = Crypto::Hash::SHA512::hash((const u8*)message, len);
    if (binary)
        printf("%.*s", (int)Crypto::Hash::SHA512::digest_size(), digest.data);
    else
        print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA512::digest_size()), -1);
}

void hmac_sha512(const char* message, size_t len)
{
    Crypto::Authentication::HMAC<Crypto::Hash::SHA512> hmac(secret_key);
    auto mac = hmac.process((const u8*)message, len);
    if (binary)
        printf("%.*s", (int)hmac.digest_size(), mac.data);
    else
        print_buffer(ByteBuffer::wrap(mac.data, hmac.digest_size()), -1);
}

auto main(int argc, char** argv) -> int
{
    const char* mode = nullptr;
    Core::ArgsParser parser;
    parser.add_positional_argument(mode, "mode to operate in ('list' to see modes and descriptions)", "mode");

    parser.add_option(secret_key, "Set the secret key (default key is 'WellHelloFriends')", "secret-key", 'k', "secret key");
    parser.add_option(key_bits, "Size of the key", "key-bits", 'b', "key-bits");
    parser.add_option(filename, "Read from file", "file", 'f', "from file");
    parser.add_option(binary, "Force binary output", "force-binary", 0);
    parser.add_option(interactive, "REPL mode", "interactive", 'i');
    parser.add_option(run_tests, "Run tests for the specified suite", "tests", 't');
    parser.add_option(suite, "Set the suite used", "suite-name", 'n', "suite name");
    parser.add_option(server, "Set the server to talk to (only for `tls')", "server-address", 's', "server-address");
    parser.add_option(port, "Set the port to talk to (only for `tls')", "port", 'p', "port");
    parser.parse(argc, argv);

    StringView mode_sv { mode };
    if (mode_sv == "list") {
        puts("test-crypto modes");
        puts("\tdigest - Access digest (authentication) functions");
        puts("\thash - Access hash functions");
        puts("\tencrypt -- Access encryption functions");
        puts("\tdecrypt -- Access decryption functions");
        puts("\ttls -- Connect to a peer over TLS 1.2");
        puts("\tlist -- List all known modes");
        puts("these modes only contain tests");
        puts("\ttest -- Run every test suite");
        puts("\tbigint -- Run big integer test suite");
        puts("\tpk -- Run Public-key system tests");
        return 0;
    }

    if (mode_sv == "hash") {
        if (suite == nullptr)
            suite = DEFAULT_HASH_SUITE;
        StringView suite_sv { suite };

        if (suite_sv == "MD5") {
            if (run_tests)
                return md5_tests();
            return run(md5);
        }
        if (suite_sv == "SHA1") {
            if (run_tests)
                return sha1_tests();
            return run(sha1);
        }
        if (suite_sv == "SHA256") {
            if (run_tests)
                return sha256_tests();
            return run(sha256);
        }
        if (suite_sv == "SHA512") {
            if (run_tests)
                return sha512_tests();
            return run(sha512);
        }
        printf("unknown hash function '%s'\n", suite);
        return 1;
    }
    if (mode_sv == "digest") {
        if (suite == nullptr)
            suite = DEFAULT_DIGEST_SUITE;
        StringView suite_sv { suite };

        if (suite_sv == "HMAC-MD5") {
            if (run_tests)
                return hmac_md5_tests();
            return run(hmac_md5);
        }
        if (suite_sv == "HMAC-SHA256") {
            if (run_tests)
                return hmac_sha256_tests();
            return run(hmac_sha256);
        }
        if (suite_sv == "HMAC-SHA512") {
            if (run_tests)
                return hmac_sha512_tests();
            return run(hmac_sha512);
        }
        printf("unknown hash function '%s'\n", suite);
        return 1;
    }
    if (mode_sv == "pk") {
        return rsa_tests();
    }
    if (mode_sv == "bigint") {
        return bigint_tests();
    }
    if (mode_sv == "tls") {
        if (run_tests)
            return tls_tests();
        return run(tls);
    }
    if (mode_sv == "test") {
        encrypting = true;
        aes_cbc_tests();

        encrypting = false;
        aes_cbc_tests();

        md5_tests();
        sha1_tests();
        sha256_tests();
        sha512_tests();

        hmac_md5_tests();
        hmac_sha256_tests();
        hmac_sha512_tests();

        rsa_tests();

        tls_tests();

        bigint_tests();

        return 0;
    }
    encrypting = mode_sv == "encrypt";
    if (encrypting || mode_sv == "decrypt") {
        if (suite == nullptr)
            suite = DEFAULT_CIPHER_SUITE;
        StringView suite_sv { suite };

        if (StringView(suite) == "AES_CBC") {
            if (run_tests)
                return aes_cbc_tests();

            if (!Crypto::Cipher::AESCipher::KeyType::is_valid_key_size(key_bits)) {
                printf("Invalid key size for AES: %d\n", key_bits);
                return 1;
            }
            if (strlen(secret_key) != (size_t)key_bits / 8) {
                printf("Key must be exactly %d bytes long\n", key_bits / 8);
                return 1;
            }
            return run(aes_cbc);
        } else {
            printf("Unknown cipher suite '%s'\n", suite);
            return 1;
        }
    }
    printf("Unknown mode '%s', check out the list of modes\n", mode);
    return 1;
}

#define I_TEST(thing)                     \
    {                                     \
        printf("Testing " #thing "... "); \
        fflush(stdout);                   \
        gettimeofday(&start_time, &tz);   \
    }
#define PASS                                                                          \
    {                                                                                 \
        struct timeval end_time {                                                     \
            0, 0                                                                      \
        };                                                                            \
        gettimeofday(&end_time, &tz);                                                 \
        time_t interval_s = end_time.tv_sec - start_time.tv_sec;                      \
        suseconds_t interval_us = end_time.tv_usec;                                   \
        if (interval_us < start_time.tv_usec) {                                       \
            interval_s -= 1;                                                          \
            interval_us += 1000000;                                                   \
        }                                                                             \
        interval_us -= start_time.tv_usec;                                            \
        printf("PASS %llds %lldus\n", (long long)interval_s, (long long)interval_us); \
    }
#define FAIL(reason) printf("FAIL: " #reason "\n")

ByteBuffer operator""_b(const char* string, size_t length)
{
    dbg() << "Create byte buffer of size " << length;
    return ByteBuffer::copy(string, length);
}

// tests go after here
// please be reasonable with orders kthx
void aes_cbc_test_name();
void aes_cbc_test_encrypt();
void aes_cbc_test_decrypt();

void md5_test_name();
void md5_test_hash();
void md5_test_consecutive_updates();

void sha1_test_name();
void sha1_test_hash();

void sha256_test_name();
void sha256_test_hash();

void sha512_test_name();
void sha512_test_hash();

void hmac_md5_test_name();
void hmac_md5_test_process();

void hmac_sha256_test_name();
void hmac_sha256_test_process();

void hmac_sha512_test_name();
void hmac_sha512_test_process();

void rsa_test_encrypt();
void rsa_test_der_parse();
void rsa_test_encrypt_decrypt();
void rsa_emsa_pss_test_create();
void bigint_test_number_theory(); // FIXME: we should really move these num theory stuff out

void tls_test_client_hello();

void bigint_test_fibo500();
void bigint_addition_edgecases();
void bigint_subtraction();
void bigint_multiplication();
void bigint_division();
void bigint_base10();
void bigint_import_export();

int aes_cbc_tests()
{
    aes_cbc_test_name();
    if (encrypting) {
        aes_cbc_test_encrypt();
    } else {
        aes_cbc_test_decrypt();
    }

    return 0;
}

void aes_cbc_test_name()
{
    I_TEST((AES CBC class name));
    Crypto::Cipher::AESCipher::CBCMode cipher("WellHelloFriends"_b, 128, Crypto::Cipher::Intent::Encryption);
    if (cipher.class_name() != "AES_CBC")
        FAIL(Invalid class name);
    else
        PASS;
}

void aes_cbc_test_encrypt()
{
    auto test_it = [](auto& cipher, auto& result) {
        auto in = "This is a test! This is another test!"_b;
        auto out = cipher.create_aligned_buffer(in.size());
        auto iv = ByteBuffer::create_zeroed(Crypto::Cipher::AESCipher::block_size());
        cipher.encrypt(in, out, iv);
        if (out.size() != sizeof(result))
            FAIL(size mismatch);
        else if (memcmp(out.data(), result, out.size()) != 0) {
            FAIL(invalid data);
            print_buffer(out, Crypto::Cipher::AESCipher::block_size());
        } else
            PASS;
    };
    {
        I_TEST((AES CBC with 128 bit key | Encrypt))
        u8 result[] {
            0xb8, 0x06, 0x7c, 0xf2, 0xa9, 0x56, 0x63, 0x58, 0x2d, 0x5c, 0xa1, 0x4b, 0xc5, 0xe3, 0x08,
            0xcf, 0xb5, 0x93, 0xfb, 0x67, 0xb6, 0xf7, 0xaf, 0x45, 0x34, 0x64, 0x70, 0x9e, 0xc9, 0x1a,
            0x8b, 0xd3, 0x70, 0x45, 0xf0, 0x79, 0x65, 0xca, 0xb9, 0x03, 0x88, 0x72, 0x1c, 0xdd, 0xab,
            0x45, 0x6b, 0x1c
        };
        Crypto::Cipher::AESCipher::CBCMode cipher("WellHelloFriends"_b, 128, Crypto::Cipher::Intent::Encryption);
        test_it(cipher, result);
    }
    {
        I_TEST((AES CBC with 192 bit key | Encrypt))
        u8 result[] {
            0xae, 0xd2, 0x70, 0xc4, 0x9c, 0xaa, 0x83, 0x33, 0xd3, 0xd3, 0xac, 0x11, 0x65, 0x35, 0xf7,
            0x19, 0x48, 0x7c, 0x7a, 0x8a, 0x95, 0x64, 0xe7, 0xc6, 0x0a, 0xdf, 0x10, 0x06, 0xdc, 0x90,
            0x68, 0x51, 0x09, 0xd7, 0x3b, 0x48, 0x1b, 0x8a, 0xd3, 0x50, 0x09, 0xba, 0xfc, 0xde, 0x11,
            0xe0, 0x3f, 0xcb
        };
        Crypto::Cipher::AESCipher::CBCMode cipher("Well Hello Friends! whf!"_b, 192, Crypto::Cipher::Intent::Encryption);
        test_it(cipher, result);
    }
    {
        I_TEST((AES CBC with 256 bit key | Encrypt))
        u8 result[] {
            0x0a, 0x44, 0x4d, 0x62, 0x9e, 0x8b, 0xd8, 0x11, 0x80, 0x48, 0x2a, 0x32, 0x53, 0x61, 0xe7,
            0x59, 0x62, 0x55, 0x9e, 0xf4, 0xe6, 0xad, 0xea, 0xc5, 0x0b, 0xf6, 0xbc, 0x6a, 0xcb, 0x9c,
            0x47, 0x9f, 0xc2, 0x21, 0xe6, 0x19, 0x62, 0xc3, 0x75, 0xca, 0xab, 0x2d, 0x18, 0xa1, 0x54,
            0xd1, 0x41, 0xe6
        };
        Crypto::Cipher::AESCipher::CBCMode cipher("WellHelloFriendsWellHelloFriends"_b, 256, Crypto::Cipher::Intent::Encryption);
        test_it(cipher, result);
    }
    {
        I_TEST((AES CBC with 256 bit key | Encrypt with unsigned key))
        u8 result[] {
            0x18, 0x71, 0x80, 0x4c, 0x28, 0x07, 0x55, 0x3c, 0x05, 0x33, 0x36, 0x3f, 0x19, 0x38, 0x5c,
            0xbe, 0xf8, 0xb8, 0x0e, 0x0e, 0x66, 0x67, 0x63, 0x9c, 0xbf, 0x73, 0xcd, 0x82, 0xf9, 0xcb,
            0x9d, 0x81, 0x56, 0xc6, 0x75, 0x14, 0x8b, 0x79, 0x60, 0xb0, 0xdf, 0xaa, 0x2c, 0x2b, 0xd4,
            0xd6, 0xa0, 0x46
        };
        u8 key[] { 0x0a, 0x8c, 0x5b, 0x0d, 0x8a, 0x68, 0x43, 0xf7, 0xaf, 0xc0, 0xe3, 0x4e, 0x4b, 0x43, 0xaa, 0x28, 0x69, 0x9b, 0x6f, 0xe7, 0x24, 0x82, 0x1c, 0x71, 0x86, 0xf6, 0x2b, 0x87, 0xd6, 0x8b, 0x8f, 0xf1 };
        Crypto::Cipher::AESCipher::CBCMode cipher(ByteBuffer::wrap(key, 32), 256, Crypto::Cipher::Intent::Encryption);
        test_it(cipher, result);
    }
    // TODO: Test non-CMS padding options
}
void aes_cbc_test_decrypt()
{
    auto test_it = [](auto& cipher, auto& result, auto result_len) {
        auto true_value = "This is a test! This is another test!";
        auto in = ByteBuffer::copy(result, result_len);
        auto out = cipher.create_aligned_buffer(in.size());
        auto iv = ByteBuffer::create_zeroed(Crypto::Cipher::AESCipher::block_size());
        cipher.decrypt(in, out, iv);
        if (out.size() != strlen(true_value)) {
            FAIL(size mismatch);
            printf("Expected %zu bytes but got %zu\n", strlen(true_value), out.size());
        } else if (memcmp(out.data(), true_value, strlen(true_value)) != 0) {
            FAIL(invalid data);
            print_buffer(out, Crypto::Cipher::AESCipher::block_size());
        } else
            PASS;
    };
    {
        I_TEST((AES CBC with 128 bit key | Decrypt))
        u8 result[] {
            0xb8, 0x06, 0x7c, 0xf2, 0xa9, 0x56, 0x63, 0x58, 0x2d, 0x5c, 0xa1, 0x4b, 0xc5, 0xe3, 0x08,
            0xcf, 0xb5, 0x93, 0xfb, 0x67, 0xb6, 0xf7, 0xaf, 0x45, 0x34, 0x64, 0x70, 0x9e, 0xc9, 0x1a,
            0x8b, 0xd3, 0x70, 0x45, 0xf0, 0x79, 0x65, 0xca, 0xb9, 0x03, 0x88, 0x72, 0x1c, 0xdd, 0xab,
            0x45, 0x6b, 0x1c
        };
        Crypto::Cipher::AESCipher::CBCMode cipher("WellHelloFriends"_b, 128, Crypto::Cipher::Intent::Decryption);
        test_it(cipher, result, 48);
    }
    {
        I_TEST((AES CBC with 192 bit key | Decrypt))
        u8 result[] {
            0xae, 0xd2, 0x70, 0xc4, 0x9c, 0xaa, 0x83, 0x33, 0xd3, 0xd3, 0xac, 0x11, 0x65, 0x35, 0xf7,
            0x19, 0x48, 0x7c, 0x7a, 0x8a, 0x95, 0x64, 0xe7, 0xc6, 0x0a, 0xdf, 0x10, 0x06, 0xdc, 0x90,
            0x68, 0x51, 0x09, 0xd7, 0x3b, 0x48, 0x1b, 0x8a, 0xd3, 0x50, 0x09, 0xba, 0xfc, 0xde, 0x11,
            0xe0, 0x3f, 0xcb
        };
        Crypto::Cipher::AESCipher::CBCMode cipher("Well Hello Friends! whf!"_b, 192, Crypto::Cipher::Intent::Decryption);
        test_it(cipher, result, 48);
    }
    {
        I_TEST((AES CBC with 256 bit key | Decrypt))
        u8 result[] {
            0x0a, 0x44, 0x4d, 0x62, 0x9e, 0x8b, 0xd8, 0x11, 0x80, 0x48, 0x2a, 0x32, 0x53, 0x61, 0xe7,
            0x59, 0x62, 0x55, 0x9e, 0xf4, 0xe6, 0xad, 0xea, 0xc5, 0x0b, 0xf6, 0xbc, 0x6a, 0xcb, 0x9c,
            0x47, 0x9f, 0xc2, 0x21, 0xe6, 0x19, 0x62, 0xc3, 0x75, 0xca, 0xab, 0x2d, 0x18, 0xa1, 0x54,
            0xd1, 0x41, 0xe6
        };
        Crypto::Cipher::AESCipher::CBCMode cipher("WellHelloFriendsWellHelloFriends"_b, 256, Crypto::Cipher::Intent::Decryption);
        test_it(cipher, result, 48);
    }
    // TODO: Test non-CMS padding options
}

int md5_tests()
{
    md5_test_name();
    md5_test_hash();
    md5_test_consecutive_updates();
    return 0;
}

void md5_test_name()
{
    I_TEST((MD5 class name));
    Crypto::Hash::MD5 md5;
    if (md5.class_name() != "MD5")
        FAIL(Invalid class name);
    else
        PASS;
}

void md5_test_hash()
{
    {
        I_TEST((MD5 Hashing | "Well hello friends"));
        u8 result[] {
            0xaf, 0x04, 0x3a, 0x08, 0x94, 0x38, 0x6e, 0x7f, 0xbf, 0x73, 0xe4, 0xaa, 0xf0, 0x8e, 0xee, 0x4c
        };
        auto digest = Crypto::Hash::MD5::hash("Well hello friends");

        if (memcmp(result, digest.data, Crypto::Hash::MD5::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::MD5::digest_size()), -1);
        } else {
            PASS;
        }
    }
    // RFC tests
    {
        I_TEST((MD5 Hashing | ""));
        u8 result[] {
            0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04, 0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e
        };
        auto digest = Crypto::Hash::MD5::hash("");

        if (memcmp(result, digest.data, Crypto::Hash::MD5::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::MD5::digest_size()), -1);
        } else {
            PASS;
        }
    }
    {
        I_TEST((MD5 Hashing | "a"));
        u8 result[] {
            0x0c, 0xc1, 0x75, 0xb9, 0xc0, 0xf1, 0xb6, 0xa8, 0x31, 0xc3, 0x99, 0xe2, 0x69, 0x77, 0x26, 0x61
        };
        auto digest = Crypto::Hash::MD5::hash("a");

        if (memcmp(result, digest.data, Crypto::Hash::MD5::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::MD5::digest_size()), -1);
        } else {
            PASS;
        }
    }
    {
        I_TEST((MD5 Hashing | "abcdefghijklmnopqrstuvwxyz"));
        u8 result[] {
            0xc3, 0xfc, 0xd3, 0xd7, 0x61, 0x92, 0xe4, 0x00, 0x7d, 0xfb, 0x49, 0x6c, 0xca, 0x67, 0xe1, 0x3b
        };
        auto digest = Crypto::Hash::MD5::hash("abcdefghijklmnopqrstuvwxyz");

        if (memcmp(result, digest.data, Crypto::Hash::MD5::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::MD5::digest_size()), -1);
        } else {
            PASS;
        }
    }
    {
        I_TEST((MD5 Hashing | Long Sequence));
        u8 result[] {
            0x57, 0xed, 0xf4, 0xa2, 0x2b, 0xe3, 0xc9, 0x55, 0xac, 0x49, 0xda, 0x2e, 0x21, 0x07, 0xb6, 0x7a
        };
        auto digest = Crypto::Hash::MD5::hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890");

        if (memcmp(result, digest.data, Crypto::Hash::MD5::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::MD5::digest_size()), -1);
        } else {
            PASS;
        }
    }
}

void md5_test_consecutive_updates()
{
    {
        I_TEST((MD5 Hashing | Multiple Updates));
        u8 result[] {
            0xaf, 0x04, 0x3a, 0x08, 0x94, 0x38, 0x6e, 0x7f, 0xbf, 0x73, 0xe4, 0xaa, 0xf0, 0x8e, 0xee, 0x4c
        };
        Crypto::Hash::MD5 md5;

        md5.update("Well");
        md5.update(" hello ");
        md5.update("friends");
        auto digest = md5.digest();

        if (memcmp(result, digest.data, Crypto::Hash::MD5::digest_size()) != 0)
            FAIL(Invalid hash);
        else
            PASS;
    }
    {
        I_TEST((MD5 Hashing | Reuse));
        Crypto::Hash::MD5 md5;

        md5.update("Well");
        md5.update(" hello ");
        md5.update("friends");
        auto digest0 = md5.digest();

        md5.update("Well");
        md5.update(" hello ");
        md5.update("friends");
        auto digest1 = md5.digest();

        if (memcmp(digest0.data, digest1.data, Crypto::Hash::MD5::digest_size()) != 0)
            FAIL(Cannot reuse);
        else
            PASS;
    }
}

int hmac_md5_tests()
{
    hmac_md5_test_name();
    hmac_md5_test_process();
    return 0;
}

int hmac_sha256_tests()
{
    hmac_sha256_test_name();
    hmac_sha256_test_process();
    return 0;
}

int hmac_sha512_tests()
{
    hmac_sha512_test_name();
    hmac_sha512_test_process();
    return 0;
}

void hmac_md5_test_name()
{
    I_TEST((HMAC - MD5 | Class name));
    Crypto::Authentication::HMAC<Crypto::Hash::MD5> hmac("Well Hello Friends");
    if (hmac.class_name() != "HMAC-MD5")
        FAIL(Invalid class name);
    else
        PASS;
}

void hmac_md5_test_process()
{
    {
        I_TEST((HMAC - MD5 | Basic));
        Crypto::Authentication::HMAC<Crypto::Hash::MD5> hmac("Well Hello Friends");
        u8 result[] {
            0x3b, 0x5b, 0xde, 0x30, 0x3a, 0x54, 0x7b, 0xbb, 0x09, 0xfe, 0x78, 0x89, 0xbc, 0x9f, 0x22, 0xa3
        };
        auto mac = hmac.process("Some bogus data");
        if (memcmp(result, mac.data, hmac.digest_size()) != 0) {
            FAIL(Invalid mac);
            print_buffer(ByteBuffer::wrap(mac.data, hmac.digest_size()), -1);
        } else
            PASS;
    }
    {
        I_TEST((HMAC - MD5 | Reuse));
        Crypto::Authentication::HMAC<Crypto::Hash::MD5> hmac("Well Hello Friends");

        auto mac_0 = hmac.process("Some bogus data");
        auto mac_1 = hmac.process("Some bogus data");

        if (memcmp(mac_0.data, mac_1.data, hmac.digest_size()) != 0) {
            FAIL(Cannot reuse);
        } else
            PASS;
    }
}

int sha1_tests()
{
    sha1_test_name();
    sha1_test_hash();
    return 0;
}

void sha1_test_name()
{
    I_TEST((SHA1 class name));
    Crypto::Hash::SHA1 sha;
    if (sha.class_name() != "SHA1") {
        FAIL(Invalid class name);
        printf("%s\n", sha.class_name().characters());
    } else
        PASS;
}

void sha1_test_hash()
{
    {
        I_TEST((SHA256 Hashing | ""));
        u8 result[] {
            0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55, 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09
        };
        auto digest = Crypto::Hash::SHA1::hash("");
        if (memcmp(result, digest.data, Crypto::Hash::SHA1::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA1::digest_size()), -1);
        } else
            PASS;
    }
    {
        I_TEST((SHA256 Hashing | Long String));
        u8 result[] {
            0x12, 0x15, 0x1f, 0xb1, 0x04, 0x44, 0x93, 0xcc, 0xed, 0x54, 0xa6, 0xb8, 0x7e, 0x93, 0x37, 0x7b, 0xb2, 0x13, 0x39, 0xdb
        };
        auto digest = Crypto::Hash::SHA1::hash("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        if (memcmp(result, digest.data, Crypto::Hash::SHA1::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA1::digest_size()), -1);
        } else
            PASS;
    }
    {
        I_TEST((SHA256 Hashing | Successive Updates));
        u8 result[] {
            0xd6, 0x6e, 0xce, 0xd1, 0xf4, 0x08, 0xc6, 0xd8, 0x35, 0xab, 0xf0, 0xc9, 0x05, 0x26, 0xa4, 0xb2, 0xb8, 0xa3, 0x7c, 0xd3
        };
        auto hasher = Crypto::Hash::SHA1 {};
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaaaaaaaa");
        hasher.update("aaaaaaaaa");
        auto digest = hasher.digest();
        if (memcmp(result, digest.data, Crypto::Hash::SHA1::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA1::digest_size()), -1);
        } else
            PASS;
    }
}

int sha256_tests()
{
    sha256_test_name();
    sha256_test_hash();
    return 0;
}

void sha256_test_name()
{
    I_TEST((SHA256 class name));
    Crypto::Hash::SHA256 sha;
    if (sha.class_name() != "SHA256") {
        FAIL(Invalid class name);
        printf("%s\n", sha.class_name().characters());
    } else
        PASS;
}

void sha256_test_hash()
{
    {
        I_TEST((SHA256 Hashing | "Well hello friends"));
        u8 result[] {
            0x9a, 0xcd, 0x50, 0xf9, 0xa2, 0xaf, 0x37, 0xe4, 0x71, 0xf7, 0x61, 0xc3, 0xfe, 0x7b, 0x8d, 0xea, 0x56, 0x17, 0xe5, 0x1d, 0xac, 0x80, 0x2f, 0xe6, 0xc1, 0x77, 0xb7, 0x4a, 0xbf, 0x0a, 0xbb, 0x5a
        };
        auto digest = Crypto::Hash::SHA256::hash("Well hello friends");
        if (memcmp(result, digest.data, Crypto::Hash::SHA256::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA256::digest_size()), -1);
        } else
            PASS;
    }
    {
        I_TEST((SHA256 Hashing | ""));
        u8 result[] {
            0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
        };
        auto digest = Crypto::Hash::SHA256::hash("");
        if (memcmp(result, digest.data, Crypto::Hash::SHA256::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA256::digest_size()), -1);
        } else
            PASS;
    }
}

void hmac_sha256_test_name()
{
    I_TEST((HMAC - SHA256 | Class name));
    Crypto::Authentication::HMAC<Crypto::Hash::SHA256> hmac("Well Hello Friends");
    if (hmac.class_name() != "HMAC-SHA256")
        FAIL(Invalid class name);
    else
        PASS;
}

void hmac_sha256_test_process()
{
    {
        I_TEST((HMAC - SHA256 | Basic));
        Crypto::Authentication::HMAC<Crypto::Hash::SHA256> hmac("Well Hello Friends");
        u8 result[] {
            0x1a, 0xf2, 0x20, 0x62, 0xde, 0x3b, 0x84, 0x65, 0xc1, 0x25, 0x23, 0x99, 0x76, 0x15, 0x1b, 0xec, 0x15, 0x21, 0x82, 0x1f, 0x23, 0xca, 0x11, 0x66, 0xdd, 0x8c, 0x6e, 0xf1, 0x81, 0x3b, 0x7f, 0x1b
        };
        auto mac = hmac.process("Some bogus data");
        if (memcmp(result, mac.data, hmac.digest_size()) != 0) {
            FAIL(Invalid mac);
            print_buffer(ByteBuffer::wrap(mac.data, hmac.digest_size()), -1);
        } else
            PASS;
    }
    {
        I_TEST((HMAC - SHA256 | Reuse));
        Crypto::Authentication::HMAC<Crypto::Hash::SHA256> hmac("Well Hello Friends");

        auto mac_0 = hmac.process("Some bogus data");
        auto mac_1 = hmac.process("Some bogus data");

        if (memcmp(mac_0.data, mac_1.data, hmac.digest_size()) != 0) {
            FAIL(Cannot reuse);
        } else
            PASS;
    }
}

int sha512_tests()
{
    sha512_test_name();
    sha512_test_hash();
    return 0;
}

void sha512_test_name()
{
    I_TEST((SHA512 class name));
    Crypto::Hash::SHA512 sha;
    if (sha.class_name() != "SHA512") {
        FAIL(Invalid class name);
        printf("%s\n", sha.class_name().characters());
    } else
        PASS;
}

void sha512_test_hash()
{
    {
        I_TEST((SHA512 Hashing | "Well hello friends"));
        u8 result[] {
            0x00, 0xfe, 0x68, 0x09, 0x71, 0x0e, 0xcb, 0x2b, 0xe9, 0x58, 0x00, 0x13, 0x69, 0x6a, 0x9e, 0x9e, 0xbd, 0x09, 0x1b, 0xfe, 0x14, 0xc9, 0x13, 0x82, 0xc7, 0x40, 0x34, 0xfe, 0xca, 0xe6, 0x87, 0xcb, 0x26, 0x36, 0x92, 0xe6, 0x34, 0x94, 0x3a, 0x11, 0xe5, 0xbb, 0xb5, 0xeb, 0x8e, 0x70, 0xef, 0x64, 0xca, 0xf7, 0x21, 0xb1, 0xde, 0xf2, 0x34, 0x85, 0x6f, 0xa8, 0x56, 0xd8, 0x23, 0xa1, 0x3b, 0x29
        };
        auto digest = Crypto::Hash::SHA512::hash("Well hello friends");
        if (memcmp(result, digest.data, Crypto::Hash::SHA512::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA512::digest_size()), -1);
        } else
            PASS;
    }
    {
        I_TEST((SHA512 Hashing | ""));
        u8 result[] {
            0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd, 0xf1, 0x54, 0x28, 0x50, 0xd6, 0x6d, 0x80, 0x07, 0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc, 0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce, 0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, 0xb0, 0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f, 0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81, 0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e
        };
        auto digest = Crypto::Hash::SHA512::hash("");
        if (memcmp(result, digest.data, Crypto::Hash::SHA512::digest_size()) != 0) {
            FAIL(Invalid hash);
            print_buffer(ByteBuffer::wrap(digest.data, Crypto::Hash::SHA512::digest_size()), -1);
        } else
            PASS;
    }
}

void hmac_sha512_test_name()
{
    I_TEST((HMAC - SHA512 | Class name));
    Crypto::Authentication::HMAC<Crypto::Hash::SHA512> hmac("Well Hello Friends");
    if (hmac.class_name() != "HMAC-SHA512")
        FAIL(Invalid class name);
    else
        PASS;
}

void hmac_sha512_test_process()
{
    {
        I_TEST((HMAC - SHA512 | Basic));
        Crypto::Authentication::HMAC<Crypto::Hash::SHA512> hmac("Well Hello Friends");
        u8 result[] {
            0xeb, 0xa8, 0x34, 0x11, 0xfd, 0x5b, 0x46, 0x5b, 0xef, 0xbb, 0x67, 0x5e, 0x7d, 0xc2, 0x7c, 0x2c, 0x6b, 0xe1, 0xcf, 0xe6, 0xc7, 0xe4, 0x7d, 0xeb, 0xca, 0x97, 0xb7, 0x4c, 0xd3, 0x4d, 0x6f, 0x08, 0x9f, 0x0d, 0x3a, 0xf1, 0xcb, 0x00, 0x79, 0x78, 0x2f, 0x05, 0x8e, 0xeb, 0x94, 0x48, 0x0d, 0x50, 0x64, 0x3b, 0xca, 0x70, 0xe2, 0x69, 0x38, 0x4f, 0xe4, 0xb0, 0x49, 0x0f, 0xc5, 0x4c, 0x7a, 0xa7
        };
        auto mac = hmac.process("Some bogus data");
        if (memcmp(result, mac.data, hmac.digest_size()) != 0) {
            FAIL(Invalid mac);
            print_buffer(ByteBuffer::wrap(mac.data, hmac.digest_size()), -1);
        } else
            PASS;
    }
    {
        I_TEST((HMAC - SHA512 | Reuse));
        Crypto::Authentication::HMAC<Crypto::Hash::SHA512> hmac("Well Hello Friends");

        auto mac_0 = hmac.process("Some bogus data");
        auto mac_1 = hmac.process("Some bogus data");

        if (memcmp(mac_0.data, mac_1.data, hmac.digest_size()) != 0) {
            FAIL(Cannot reuse);
        } else
            PASS;
    }
}

int rsa_tests()
{
    rsa_test_encrypt();
    rsa_test_der_parse();
    bigint_test_number_theory();
    rsa_test_encrypt_decrypt();
    rsa_emsa_pss_test_create();
    return 0;
}

void rsa_test_encrypt()
{
    {
        I_TEST((RSA RAW | Encryption));
        ByteBuffer data { "hellohellohellohellohellohellohellohellohellohellohellohello123-"_b };
        u8 result[] { 0x6f, 0x7b, 0xe2, 0xd3, 0x95, 0xf8, 0x8d, 0x87, 0x6d, 0x10, 0x5e, 0xc3, 0xcd, 0xf7, 0xbb, 0xa6, 0x62, 0x8e, 0x45, 0xa0, 0xf1, 0xe5, 0x0f, 0xdf, 0x69, 0xcb, 0xb6, 0xd5, 0x42, 0x06, 0x7d, 0x72, 0xa9, 0x5e, 0xae, 0xbf, 0xbf, 0x0f, 0xe0, 0xeb, 0x31, 0x31, 0xca, 0x8a, 0x81, 0x1e, 0xb9, 0xec, 0x6d, 0xcc, 0xb8, 0xa4, 0xac, 0xa3, 0x31, 0x05, 0xa9, 0xac, 0xc9, 0xd3, 0xe6, 0x2a, 0x18, 0xfe };
        Crypto::PK::RSA rsa(
            "8126832723025844890518845777858816391166654950553329127845898924164623511718747856014227624997335860970996746552094406240834082304784428582653994490504519"_bigint,
            "4234603516465654167360850580101327813936403862038934287300450163438938741499875303761385527882335478349599685406941909381269804396099893549838642251053393"_bigint,
            "65537"_bigint);
        u8 buffer[rsa.output_size()];
        auto buf = ByteBuffer::wrap(buffer, sizeof(buffer));
        rsa.encrypt(data, buf);
        if (memcmp(result, buf.data(), buf.size())) {
            FAIL(Invalid encryption result);
            print_buffer(buf, 16);
        } else {
            PASS;
        }
    }
    {
        I_TEST((RSA PKCS #1 1.5 | Encryption));
        ByteBuffer data { "hellohellohellohellohellohellohellohellohello123-"_b };
        Crypto::PK::RSA_PKCS1_EME rsa(
            "8126832723025844890518845777858816391166654950553329127845898924164623511718747856014227624997335860970996746552094406240834082304784428582653994490504519"_bigint,
            "4234603516465654167360850580101327813936403862038934287300450163438938741499875303761385527882335478349599685406941909381269804396099893549838642251053393"_bigint,
            "65537"_bigint);
        u8 buffer[rsa.output_size()];
        auto buf = ByteBuffer::wrap(buffer, sizeof(buffer));
        rsa.encrypt(data, buf);
        rsa.decrypt(buf, buf);

        if (memcmp(buf.data(), "hellohellohellohellohellohellohellohellohello123-", 49))
            FAIL(Invalid encryption);
        else {
            dbg() << "out size " << buf.size() << " values: " << StringView { (char*)buf.data(), buf.size() };

            PASS;
        }
    }
}

void bigint_test_number_theory()
{
    {
        I_TEST((Number Theory | Modular Inverse));
        if (Crypto::NumberTheory::ModularInverse(7, 87) == 25) {
            PASS;
        } else {
            FAIL(Invalid result);
        }
    }
    {
        I_TEST((Number Theory | Modular Power));
        auto exp = Crypto::NumberTheory::ModularPower(
            Crypto::UnsignedBigInteger::from_base10("2988348162058574136915891421498819466320163312926952423791023078876139"),
            Crypto::UnsignedBigInteger::from_base10("2351399303373464486466122544523690094744975233415544072992656881240319"),
            10000);

        if (exp == 3059) {
            PASS;
        } else {
            FAIL(Invalid result);
            puts(exp.to_base10().characters());
        }
    }
}

void rsa_emsa_pss_test_create()
{
    {
        // This is a template validity test
        I_TEST((RSA EMSA_PSS | Construction));
        Crypto::PK::RSA rsa;
        Crypto::PK::RSA_EMSA_PSS<Crypto::Hash::SHA256> rsa_esma_pss(rsa);
        PASS;
    }
}

void rsa_test_der_parse()
{
    I_TEST((RSA | ASN1 DER / PEM encoded Key import));
    auto privkey = R"(-----BEGIN RSA PRIVATE KEY-----
MIIBOgIBAAJBAJsrIYHxs1YL9tpfodaWs1lJoMdF4kgFisUFSj6nvBhJUlmBh607AlgTaX0E
DGPYycXYGZ2n6rqmms5lpDXBpUcCAwEAAQJAUNpPkmtEHDENxsoQBUXvXDYeXdePSiIBJhpU
joNOYoR5R9z5oX2cpcyykQ58FC2vKKg+x8N6xczG7qO95tw5UQIhAN354CP/FA+uTeJ6KJ+i
zCBCl58CjNCzO0s5HTc56el5AiEAsvPKXo5/9gS/S4UzDRP6abq7GreixTfjR8LXidk3FL8C
IQCTjYI861Y+hjMnlORkGSdvWlTHUj6gjEOh4TlWeJzQoQIgAxMZOQKtxCZUuxFwzRq4xLRG
nrDlBQpuxz7bwSyQO7UCIHrYMnDohgNbwtA5ZpW3H1cKKQQvueWm6sxW9P5sUrZ3
-----END RSA PRIVATE KEY-----)";

    Crypto::PK::RSA rsa(privkey);
    if (rsa.public_key().public_exponent() == 65537) {
        if (rsa.private_key().private_exponent() == "4234603516465654167360850580101327813936403862038934287300450163438938741499875303761385527882335478349599685406941909381269804396099893549838642251053393"_bigint) {
            PASS;
        } else
            FAIL(Invalid private exponent);
    } else {
        FAIL(Invalid public exponent);
    }
}

void rsa_test_encrypt_decrypt()
{
    I_TEST((RSA | Encrypt));
    dbg() << " creating rsa object";
    Crypto::PK::RSA rsa(
        "9527497237087650398000977129550904920919162360737979403539302312977329868395261515707123424679295515888026193056908173564681660256268221509339074678416049"_bigint,
        "39542231845947188736992321577701849924317746648774438832456325878966594812143638244746284968851807975097653255909707366086606867657273809465195392910913"_bigint,
        "65537"_bigint);
    dbg() << "Output size: " << rsa.output_size();
    auto dec = ByteBuffer::create_zeroed(rsa.output_size());
    auto enc = ByteBuffer::create_zeroed(rsa.output_size());
    enc.overwrite(0, "WellHelloFriendsWellHelloFriendsWellHelloFriendsWellHelloFriends", 64);

    rsa.encrypt(enc, dec);
    rsa.decrypt(dec, enc);

    dbg() << "enc size " << enc.size() << " dec size " << dec.size();

    if (memcmp(enc.data(), "WellHelloFriendsWellHelloFriendsWellHelloFriendsWellHelloFriends", 64) != 0) {
        FAIL(Could not encrypt then decrypt);
    } else {
        PASS;
    }
}

int tls_tests()
{
    tls_test_client_hello();
    return 0;
}

void tls_test_client_hello()
{
    I_TEST((TLS | Connect and Data Transfer));
    Core::EventLoop loop;
    RefPtr<TLS::TLSv12> tls = TLS::TLSv12::construct(nullptr);
    bool sent_request = false;
    ByteBuffer contents = ByteBuffer::create_uninitialized(0);
    tls->on_tls_ready_to_write = [&](TLS::TLSv12& tls) {
        if (sent_request)
            return;
        sent_request = true;
        if (!tls.write("GET / HTTP/1.1\r\nHost: github.com\r\nConnection: close\r\n\r\n"_b)) {
            FAIL(write() failed);
            loop.quit(0);
        }
    };
    tls->on_tls_ready_to_read = [&](TLS::TLSv12& tls) {
        auto data = tls.read();
        if (!data.has_value()) {
            FAIL(No data received);
            loop.quit(1);
        } else {
            //            print_buffer(data.value(), 16);
            contents.append(data.value().data(), data.value().size());
        }
    };
    tls->on_tls_finished = [&] {
        PASS;
        auto file = Core::File::open("foo.response", Core::IODevice::WriteOnly);
        if (file.is_error()) {
            printf("Can't write there, %s\n", file.error().characters());
            loop.quit(2);
            return;
        }
        file.value()->write(contents);
        file.value()->close();
        loop.quit(0);
    };
    tls->on_tls_error = [&](TLS::AlertDescription) {
        FAIL(Connection failure);
        loop.quit(1);
    };
    if (!tls->connect("github.com", 443)) {
        FAIL(connect() failed);
        return;
    }
    loop.exec();
}

int bigint_tests()
{
    bigint_test_fibo500();
    bigint_addition_edgecases();
    bigint_subtraction();
    bigint_multiplication();
    bigint_division();
    bigint_base10();
    bigint_import_export();
    return 0;
}

Crypto::UnsignedBigInteger bigint_fibonacci(size_t n)
{
    Crypto::UnsignedBigInteger num1(0);
    Crypto::UnsignedBigInteger num2(1);
    for (size_t i = 0; i < n; ++i) {
        Crypto::UnsignedBigInteger t = num1.plus(num2);
        num2 = num1;
        num1 = t;
    }
    return num1;
}
void bigint_test_fibo500()
{
    {
        I_TEST((BigInteger | Fibonacci500));
        bool pass = (bigint_fibonacci(500).words() == AK::Vector<u32> { 315178285, 505575602, 1883328078, 125027121, 3649625763, 347570207, 74535262, 3832543808, 2472133297, 1600064941, 65273441 });

        if (pass) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
}

void bigint_addition_edgecases()
{
    {
        I_TEST((BigInteger | Edge Cases));
        Crypto::UnsignedBigInteger num1;
        Crypto::UnsignedBigInteger num2(70);
        Crypto::UnsignedBigInteger num3 = num1.plus(num2);
        bool pass = (num3 == num2);
        pass &= (num1 == Crypto::UnsignedBigInteger(0));

        if (pass) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Borrow with zero));
        Crypto::UnsignedBigInteger num1({ UINT32_MAX - 3, UINT32_MAX });
        Crypto::UnsignedBigInteger num2({ UINT32_MAX - 2, 0 });
        if (num1.plus(num2).words() == Vector<u32> { 4294967289, 0, 1 }) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
}

void bigint_subtraction()
{
    {
        I_TEST((BigInteger | Simple Subtraction 1));
        Crypto::UnsignedBigInteger num1(80);
        Crypto::UnsignedBigInteger num2(70);

        if (num1.minus(num2) == Crypto::UnsignedBigInteger(10)) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Simple Subtraction 2));
        Crypto::UnsignedBigInteger num1(50);
        Crypto::UnsignedBigInteger num2(70);

        if (num1.minus(num2).is_invalid()) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Subtraction with borrow));
        Crypto::UnsignedBigInteger num1(UINT32_MAX);
        Crypto::UnsignedBigInteger num2(1);
        Crypto::UnsignedBigInteger num3 = num1.plus(num2);
        Crypto::UnsignedBigInteger result = num3.minus(num2);
        if (result == num1) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Subtraction with large numbers));
        Crypto::UnsignedBigInteger num1 = bigint_fibonacci(343);
        Crypto::UnsignedBigInteger num2 = bigint_fibonacci(218);
        Crypto::UnsignedBigInteger result = num1.minus(num2);
        if ((result.plus(num2) == num1)
            && (result.words() == Vector<u32> { 811430588, 2958904896, 1130908877, 2830569969, 3243275482, 3047460725, 774025231, 7990 })) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Subtraction with large numbers 2));
        Crypto::UnsignedBigInteger num1(Vector<u32> { 1483061863, 446680044, 1123294122, 191895498, 3347106536, 16, 0, 0, 0 });
        Crypto::UnsignedBigInteger num2(Vector<u32> { 4196414175, 1117247942, 1123294122, 191895498, 3347106536, 16 });
        Crypto::UnsignedBigInteger result = num1.minus(num2);
        // this test only verifies that we don't crash on an assertion
        PASS;
    }
    {
        I_TEST((BigInteger | Subtraction Regression 1));
        auto num = Crypto::UnsignedBigInteger { 1 }.shift_left(256);
        if (num.minus(1).words() == Vector<u32> { 4294967295, 4294967295, 4294967295, 4294967295, 4294967295, 4294967295, 4294967295, 4294967295, 0 }) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
}

void bigint_multiplication()
{
    {
        I_TEST((BigInteger | Simple Multiplication));
        Crypto::UnsignedBigInteger num1(8);
        Crypto::UnsignedBigInteger num2(251);
        Crypto::UnsignedBigInteger result = num1.multiplied_by(num2);
        if (result.words() == Vector<u32> { 2008 }) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Multiplications with big numbers 1));
        Crypto::UnsignedBigInteger num1 = bigint_fibonacci(200);
        Crypto::UnsignedBigInteger num2(12345678);
        Crypto::UnsignedBigInteger result = num1.multiplied_by(num2);
        if (result.words() == Vector<u32> { 669961318, 143970113, 4028714974, 3164551305, 1589380278, 2 }) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Multiplications with big numbers 2));
        Crypto::UnsignedBigInteger num1 = bigint_fibonacci(200);
        Crypto::UnsignedBigInteger num2 = bigint_fibonacci(341);
        Crypto::UnsignedBigInteger result = num1.multiplied_by(num2);
        if (result.words() == Vector<u32> { 3017415433, 2741793511, 1957755698, 3731653885, 3154681877, 785762127, 3200178098, 4260616581, 529754471, 3632684436, 1073347813, 2516430 }) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
}
void bigint_division()
{
    {
        I_TEST((BigInteger | Simple Division));
        Crypto::UnsignedBigInteger num1(27194);
        Crypto::UnsignedBigInteger num2(251);
        auto result = num1.divided_by(num2);
        Crypto::UnsignedDivisionResult expected = { Crypto::UnsignedBigInteger(108), Crypto::UnsignedBigInteger(86) };
        if (result.quotient == expected.quotient && result.remainder == expected.remainder) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Division with big numbers));
        Crypto::UnsignedBigInteger num1 = bigint_fibonacci(386);
        Crypto::UnsignedBigInteger num2 = bigint_fibonacci(238);
        auto result = num1.divided_by(num2);
        Crypto::UnsignedDivisionResult expected = {
            Crypto::UnsignedBigInteger(Vector<u32> { 2300984486, 2637503534, 2022805584, 107 }),
            Crypto::UnsignedBigInteger(Vector<u32> { 1483061863, 446680044, 1123294122, 191895498, 3347106536, 16, 0, 0, 0 })
        };
        if (result.quotient == expected.quotient && result.remainder == expected.remainder) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | Combined test));
        auto num1 = bigint_fibonacci(497);
        auto num2 = bigint_fibonacci(238);
        auto div_result = num1.divided_by(num2);
        if (div_result.quotient.multiplied_by(num2).plus(div_result.remainder) == num1) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
}

void bigint_base10()
{
    {
        I_TEST((BigInteger | From String));
        auto result = Crypto::UnsignedBigInteger::from_base10("57195071295721390579057195715793");
        if (result.words() == Vector<u32> { 3806301393, 954919431, 3879607298, 721 }) {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
    {
        I_TEST((BigInteger | To String));
        auto result = Crypto::UnsignedBigInteger { Vector<u32> { 3806301393, 954919431, 3879607298, 721 } }.to_base10();
        if (result == "57195071295721390579057195715793") {
            PASS;
        } else {
            FAIL(Incorrect Result);
        }
    }
}

void bigint_import_export()
{
    {
        I_TEST((BigInteger | BigEndian Decode / Encode roundtrip));
        u8 random_bytes[128];
        u8 target_buffer[128];
        AK::fill_with_random(random_bytes, 128);
        auto encoded = Crypto::UnsignedBigInteger::import_data(random_bytes, 128);
        encoded.export_data(target_buffer, 128);
        if (memcmp(target_buffer, random_bytes, 128) != 0)
            FAIL(Could not roundtrip);
        else
            PASS;
    }
    {
        I_TEST((BigInteger | BigEndian Encode / Decode roundtrip));
        u8 target_buffer[128];
        auto encoded = "12345678901234567890"_bigint;
        auto size = encoded.export_data(target_buffer, 128);
        auto decoded = Crypto::UnsignedBigInteger::import_data(target_buffer, size);
        if (encoded != decoded)
            FAIL(Could not roundtrip);
        else
            PASS;
    }
    {
        I_TEST((BigInteger | BigEndian Import));
        auto number = Crypto::UnsignedBigInteger::import_data("hello");
        if (number == "448378203247"_bigint) {
            PASS;
        } else {
            FAIL(Invalid value);
        }
    }
    {
        I_TEST((BigInteger | BigEndian Export));
        auto number = "448378203247"_bigint;
        char exported[8] { 0 };
        auto exported_length = number.export_data((u8*)exported, 8);
        if (exported_length == 5 && memcmp(exported + 3, "hello", 5) == 0) {
            PASS;
        } else {
            FAIL(Invalid value);
            print_buffer(ByteBuffer::wrap(exported - exported_length + 8, exported_length), -1);
        }
    }
}
