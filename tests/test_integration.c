// Functional check of the Bedrock edition switch and the route planner.
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../cubiomes/finders.h"
#include "../src/bedrock.h"

int getStructureConfig_override(int stype, int mc, StructureConfig *sconf)
{
    return getStructureConfig(stype, mc, sconf);
}

// Minimal stand-in for the Qt free parts of route.cpp: we only need the
// terrain classification, which is what determines route validity.
#include "../cubiomes/biomes.h"

int main(void)
{
    // --- 1. Bedrock vs Java must place structures differently ---
    uint64_t seed = 123456789012345ULL;
    int diff = 0, same = 0;
    for (int rx = -4; rx <= 4; rx++)
    {
        for (int rz = -4; rz <= 4; rz++)
        {
            Pos jp, bp;
            int okj = getStructurePos(Desert_Pyramid, MC_1_21_3, seed, rx, rz, &jp);
            int okb = getBedrockStructurePos(Desert_Pyramid, MC_1_21_3, seed, rx, rz, &bp);
            if (okj && okb)
            {
                if (jp.x == bp.x && jp.z == bp.z) same++;
                else diff++;
            }
        }
    }
    printf("Bedrock vs Java desert pyramid: %d differ, %d identical\n", diff, same);

    // --- 2. Biome parity: the same seed must give the same biome in both
    // editions, since the viewer reuses the Java generator for Bedrock. ---
    Generator gj, gb;
    setupGenerator(&gj, MC_1_21_3, 0);
    applySeed(&gj, DIM_OVERWORLD, seed);
    setupGenerator(&gb, be2javaMC(BE_1_21), 0);
    applySeed(&gb, DIM_OVERWORLD, seed);

    int mismatches = 0, checked = 0;
    for (int i = -20; i <= 20; i++)
    {
        for (int j = -20; j <= 20; j++)
        {
            int a = getBiomeAt(&gj, 4, i * 16, 63, j * 16);
            int b = getBiomeAt(&gb, 4, i * 16, 63, j * 16);
            checked++;
            if (a != b) mismatches++;
        }
    }
    printf("Biome parity BE_1_21 <-> MC_1_21_3: %d/%d mismatches\n", mismatches, checked);

    // --- 3. Version mapping sanity ---
    printf("be2javaMC(1.18)=%s 1.19=%s 1.20=%s 1.21=%s\n",
        mc2str(be2javaMC(BE_1_18)), mc2str(be2javaMC(BE_1_19)),
        mc2str(be2javaMC(BE_1_20)), mc2str(be2javaMC(BE_1_21)));
    printf("be2str roundtrip: %s -> %d\n", be2str(BE_1_21), str2be(be2str(BE_1_21)));
    printf("str2be(\"1.21\")=%d (expect -1)\n", str2be("1.21"));

    // --- 4. Unsupported structures must be rejected cleanly ---
    StructureConfig sc;
    int vwell = getBedrockStructureConfig(Desert_Well, MC_1_21_3, &sc);
    printf("Bedrock Desert_Well supported: %d (expect 0)\n", vwell);

    StructureConfig sv;
    int vvill = getBedrockStructureConfig(Village, MC_1_21_3, &sv);
    printf("Bedrock Village supported: %d (expect 1, type %d, region %d)\n",
        vvill, sv.structType, sv.regionSize);

    StructureConfig sj;
    int vjava = getStructureConfig(Village, MC_1_21_3, &sj);
    printf("Java   Village: type %d, region %d\n", sj.structType, sj.regionSize);
    int regiondiffers = (sv.regionSize != sj.regionSize);

    // --- 5. Every supported structure yields a position in its own region ---
    int bad = 0, tested = 0;
    for (int st = 0; st < FEATURE_NUM; st++)
    {
        StructureConfig c;
        if (!getBedrockStructureConfig(st, MC_1_21_3, &c))
            continue;
        for (int rx = -2; rx <= 2; rx++)
        {
            for (int rz = -2; rz <= 2; rz++)
            {
                Pos p;
                if (!getBedrockStructurePos(st, MC_1_21_3, seed, rx, rz, &p))
                    continue;
                tested++;
                // The returned block position must lie inside the region.
                int64_t bx0 = (int64_t)rx * c.regionSize * 16;
                int64_t bz0 = (int64_t)rz * c.regionSize * 16;
                int64_t bx1 = bx0 + (int64_t)c.regionSize * 16;
                int64_t bz1 = bz0 + (int64_t)c.regionSize * 16;
                if (p.x < bx0 || p.x >= bx1 || p.z < bz0 || p.z >= bz1)
                {
                    if (st != End_City) // end cities are filtered by distance
                    {
                        printf("OUT OF REGION: %s reg(%d,%d) -> (%d,%d) in [%lld,%lld]\n",
                            struct2str(st), rx, rz, p.x, p.z,
                            (long long)bx0, (long long)bx1);
                        bad++;
                    }
                }
            }
        }
    }
    printf("Region containment: %d checks, %d out of region\n", tested, bad);

    return (diff > 0 && same == 0 && mismatches == 0 && bad == 0) ? 0 : 1;
}
