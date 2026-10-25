/*
 * Block cipher of the Blood Omen 2 installer.
 *
 * The key schedule, the 256-byte S-box shuffle and the 32-round block
 * function are translated from install2.dat (cipher at 0x405110, inverse
 * at 0x405300, setup at 0x405490, key mix at 0x405280). See
 * docs/FORMATS/dat.md. The S-box and password are supplied by the caller:
 * they belong to the user's installer and are not stored here.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t key[4];
static uint8_t sbox[256];
static uint32_t crc_table[256];

static uint32_t rol32(uint32_t value, unsigned count)
{
    return (value << count) | (value >> (32 - count));
}

static uint32_t ror32(uint32_t value, unsigned count)
{
    return (value >> count) | (value << (32 - count));
}

static uint32_t sub_bytes(uint32_t value)
{
    return ((uint32_t)sbox[(value >> 24) & 0xff] << 24)
         | ((uint32_t)sbox[(value >> 16) & 0xff] << 16)
         | ((uint32_t)sbox[(value >> 8) & 0xff] << 8)
         | (uint32_t)sbox[value & 0xff];
}

static void make_crc_table(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; bit++)
            c = (c & 1) ? (c >> 1) ^ 0xedb88320u : c >> 1;
        crc_table[i] = c;
    }
}

static void mix_key(const uint8_t *block)
{
    for (int group = 0; group < 4; group++) {
        key[0] ^= crc_table[block[4 * group + 0]];
        key[1] ^= crc_table[block[4 * group + 1]];
        key[2] ^= crc_table[block[4 * group + 2]];
        key[3] ^= crc_table[block[4 * group + 3]];
    }
}

static void encrypt_block(uint32_t *words)
{
    uint32_t u1 = words[2] ^ key[2];
    uint32_t u2 = words[3] ^ key[3];
    uint32_t left0 = words[0] ^ key[0];
    uint32_t left1 = words[1] ^ key[1];
    uint32_t u4 = 0;
    uint32_t u5 = 0;

    for (uint32_t round = 0; round < 32; round++) {
        u5 = u2;
        u4 = u1;
        u1 = (rol32(u5, 11) + u4) ^ key[round & 3];
        u1 = sub_bytes(u1) ^ left0;
        u2 = (ror32(u4, 15) ^ u5) + key[round & 3];
        u2 = sub_bytes(u2) ^ left1;
        left0 = u4;
        left1 = u5;
    }

    words[0] = key[0] ^ u1;
    words[1] = key[1] ^ u2;
    words[2] = key[2] ^ u4;
    words[3] = key[3] ^ u5;
    mix_key((const uint8_t *)words);
}

static void decrypt_block(uint32_t *words)
{
    uint32_t original[4] = {words[0], words[1], words[2], words[3]};
    uint32_t u2 = words[0] ^ key[0];
    uint32_t left1 = words[1] ^ key[1];
    uint32_t u1 = words[2] ^ key[2];
    uint32_t u3 = words[3] ^ key[3];
    uint32_t u4 = 0;
    uint32_t u5 = 0;

    for (int round = 31; round >= 0; round--) {
        u5 = u3;
        u4 = u1;
        u1 = (rol32(u5, 11) + u4) ^ key[round & 3];
        u1 = sub_bytes(u1) ^ u2;
        u2 = (ror32(u4, 15) ^ u5) + key[round & 3];
        u3 = sub_bytes(u2) ^ left1;
        u2 = u4;
        left1 = u5;
    }

    words[0] = key[0] ^ u1;
    words[1] = key[1] ^ u3;
    words[2] = key[2] ^ u4;
    words[3] = key[3] ^ u5;
    mix_key((const uint8_t *)original);
}

static void setup_key(const uint8_t *sbox_in, const uint8_t *password,
                      size_t password_length)
{
    uint8_t buffer[256];
    uint8_t table_bytes[256];

    key[0] = 0xd3a3b879u;
    key[1] = 0x3f6d12f7u;
    key[2] = 0x7515a235u;
    key[3] = 0xa4e7f123u;

    memcpy(sbox, sbox_in, 256);
    make_crc_table();
    for (int i = 0; i < 256; i++)
        table_bytes[i] = (uint8_t)(crc_table[i] & 0xff);

    memset(buffer, 0, sizeof buffer);
    if (password_length > sizeof buffer)
        password_length = sizeof buffer;
    memcpy(buffer, password, password_length);

    for (uint32_t round = 0; round < 256; round++) {
        size_t index = 0;
        while (index < password_length) {
            uint8_t stop = table_bytes[(uint8_t)(buffer[index + 1] + round)];
            uint8_t current = table_bytes[(uint8_t)(buffer[index] - round)];
            uint8_t step = 1;
            while (current != stop) {
                uint8_t other = (uint8_t)(current + index + step);
                uint8_t swapped = sbox[current];
                sbox[current] = sbox[other];
                sbox[other] = swapped;
                step++;
                current++;
            }
            index += 2;
        }
    }

    for (size_t offset = 0; offset < password_length; offset += 16) {
        uint32_t words[4];
        memcpy(words, buffer + offset, 16);
        encrypt_block(words);
        memcpy(buffer + offset, words, 16);
    }
}

void bo2_xor(uint8_t *data, size_t length, const uint8_t *key,
             size_t key_length)
{
    if (key_length == 0)
        return;
    for (size_t offset = 0; offset < length; offset++)
        data[offset] ^= key[offset % key_length];
}

void bo2_decrypt(uint8_t *data, size_t length, const uint8_t *sbox_in,
                 const uint8_t *password, size_t password_length)
{
    size_t offset = 0;

    setup_key(sbox_in, password, password_length);
    for (; offset + 16 <= length; offset += 16) {
        uint32_t words[4];
        memcpy(words, data + offset, 16);
        decrypt_block(words);
        memcpy(data + offset, words, 16);
    }
    if (offset < length) {
        uint32_t words[4] = {0, 0, 0, 0};
        memcpy(words, data + offset, length - offset);
        decrypt_block(words);
        memcpy(data + offset, words, length - offset);
    }
}
