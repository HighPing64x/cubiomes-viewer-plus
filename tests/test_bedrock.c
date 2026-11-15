// Standalone verification of the Bedrock structure placement against an
// independent reimplementation of the documented algorithm.
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../cubiomes/finders.h"
#include "../src/bedrock.h"

// The cubiomes library is built with STRUCT_CONFIG_OVERRIDE, which expects the
// application to provide this hook. For the test it simply forwards.
int getStructureConfig_override(int stype, int mc, StructureConfig *sconf)
{
    return getStructureConfig(stype, mc, sconf);
}

// Mirror of the reference implementation, included verbatim in behaviour.
#define MT_SIZE 624
#define MT_M 397
struct RefMT { uint32_t array[MT_SIZE]; uint16_t index; };

static void refTwist(struct RefMT* mt) {
    static const uint32_t mag01[2] = {0, 0x9908b0dfU};
    int i;
    for (i = 0; i < MT_SIZE - MT_M; i++) {
        uint32_t y = (mt->array[i] & 0x80000000U) | (mt->array[i+1] & 0x7fffffffU);
        mt->array[i] = mt->array[i+MT_M] ^ (y >> 1) ^ mag01[y & 1];
    }
    for (; i < MT_SIZE - 1; i++) {
        uint32_t y = (mt->array[i] & 0x80000000U) | (mt->array[i+1] & 0x7fffffffU);
        mt->array[i] = mt->array[i-(MT_SIZE-MT_M)] ^ (y >> 1) ^ mag01[y & 1];
    }
    { uint32_t y = (mt->array[MT_SIZE-1] & 0x80000000U) | (mt->array[0] & 0x7fffffffU);
      mt->array[MT_SIZE-1] = mt->array[MT_M-1] ^ (y >> 1) ^ mag01[y & 1]; }
    mt->index = 0;
}
static void refSetSeed(struct RefMT* mt, uint64_t seed, int n) {
    if (n > 0) {
        int end = MT_SIZE - 1;
        int i;
        if (n + 396 < end) end = n + 396;
        mt->array[0] = (uint32_t)seed;
        for (i = 1; i <= end; i++) {
            uint32_t prev = mt->array[i-1];
            mt->array[i] = (uint32_t)(1812433253U * (prev ^ (prev >> 30)) + i);
        }
    }
    mt->index = MT_SIZE;
}
static uint32_t refNext(struct RefMT* mt) {
    uint32_t y;
    if (mt->index >= MT_SIZE) refTwist(mt);
    y = mt->array[mt->index++];
    y ^= (y >> 11); y ^= (y << 7) & 0x9d2c5680U; y ^= (y << 15) & 0xefc60000U;
    return y ^ (y >> 18);
}
static int refNextInt(struct RefMT* mt, int n) { return (int)(refNext(mt) % (uint32_t)n); }

static uint64_t refMix(uint64_t seed, int rx, int rz, uint64_t salt) {
    return rx * UINT64_C(341873128712) + rz * UINT64_C(132897987541) + seed + salt;
}

// Reference: uniform placement (desert pyramid config)
static Pos refFeaturePos(int32_t salt, int regSize, int chunkRange,
                         uint64_t seed, int rx, int rz) {
    struct RefMT mt;
    Pos p;
    refSetSeed(&mt, refMix(seed, rx, rz, (uint64_t)(int64_t)salt), 2);
    p.x = refNextInt(&mt, chunkRange);
    p.z = refNextInt(&mt, chunkRange);
    p.x = (int)(((uint64_t)rx * regSize + p.x) << 4) + 8;
    p.z = (int)(((uint64_t)rz * regSize + p.z) << 4) + 8;
    return p;
}

// Reference: triangular placement
static Pos refLargePos(int32_t salt, int regSize, int chunkRange,
                       uint64_t seed, int rx, int rz) {
    struct RefMT mt;
    Pos p;
    refSetSeed(&mt, refMix(seed, rx, rz, (uint64_t)(int64_t)salt), 4);
    p.x = (refNextInt(&mt, chunkRange) + refNextInt(&mt, chunkRange)) >> 1;
    p.z = (refNextInt(&mt, chunkRange) + refNextInt(&mt, chunkRange)) >> 1;
    p.x = (int)(((uint64_t)rx * regSize + p.x) << 4) + 8;
    p.z = (int)(((uint64_t)rz * regSize + p.z) << 4) + 8;
    return p;
}

int main(void) {
    int fails = 0, total = 0;
    uint64_t seeds[] = {0, 1, 42, 123456789, 0xDEADBEEF, 0xFFFFFFFFFFFFFFFFULL};
    int ns = (int)(sizeof(seeds)/sizeof(seeds[0]));

    for (int s = 0; s < ns; s++) {
        for (int rx = -3; rx <= 3; rx++) {
            for (int rz = -3; rz <= 3; rz++) {
                Pos got, want;

                // Desert pyramid: uniform, salt 14357617, 32/24
                if (!getBedrockStructurePos(Desert_Pyramid, MC_1_21_3, seeds[s], rx, rz, &got)) {
                    printf("FAIL: desert pyramid returned false\n"); fails++; continue;
                }
                want = refFeaturePos(14357617, 32, 24, seeds[s], rx, rz);
                total++;
                if (got.x != want.x || got.z != want.z) {
                    printf("FAIL desert s=%llu r=(%d,%d) got(%d,%d) want(%d,%d)\n",
                        (unsigned long long)seeds[s], rx, rz, got.x, got.z, want.x, want.z);
                    fails++;
                }

                // Village: triangular, salt 10387312, 34/26 over 1.17
                if (!getBedrockStructurePos(Village, MC_1_21_3, seeds[s], rx, rz, &got)) {
                    printf("FAIL: village returned false\n"); fails++; continue;
                }
                want = refLargePos(10387312, 34, 26, seeds[s], rx, rz);
                total++;
                if (got.x != want.x || got.z != want.z) {
                    printf("FAIL village s=%llu r=(%d,%d) got(%d,%d) want(%d,%d)\n",
                        (unsigned long long)seeds[s], rx, rz, got.x, got.z, want.x, want.z);
                    fails++;
                }

                // Mansion: triangular, salt 10387319, 80/60
                if (!getBedrockStructurePos(Mansion, MC_1_21_3, seeds[s], rx, rz, &got)) {
                    printf("FAIL: mansion returned false\n"); fails++; continue;
                }
                want = refLargePos(10387319, 80, 60, seeds[s], rx, rz);
                total++;
                if (got.x != want.x || got.z != want.z) {
                    printf("FAIL mansion s=%llu r=(%d,%d) got(%d,%d) want(%d,%d)\n",
                        (unsigned long long)seeds[s], rx, rz, got.x, got.z, want.x, want.z);
                    fails++;
                }
            }
        }
    }
    printf("Bedrock placement: %d checks, %d failures\n", total, fails);
    return fails ? 1 : 0;
}
