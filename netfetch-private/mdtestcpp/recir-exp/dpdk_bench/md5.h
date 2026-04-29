#pragma once
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buffer[64];
} MD5_CTX;

void MD5Init(MD5_CTX *ctx);
void MD5Update(MD5_CTX *ctx, const uint8_t *data, uint32_t len);
void MD5Final(uint8_t digest[16], MD5_CTX *ctx);
