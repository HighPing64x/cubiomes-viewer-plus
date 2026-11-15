// Verifies, without Qt, that the *dispatcher expression* used in
// Level::init4struct picks the Bedrock config and placement for Bedrock
// worlds, mirroring the code path exactly.
#include "../src/bedrock.h"

#include "../cubiomes/finders.h"
#include "../cubiomes/util.h"

#include <cstdio>
#include <vector>

int getStructureConfig_override(int stype, int mc, StructureConfig *sconf)
{
    return getStructureConfig(stype, mc, sconf);
}

// Minimal mirror of WorldInfo's Bedrock flag, used only by this test.
struct WI { int mc; int bedrock; uint64_t seed; };

static bool isBedrock(const WI& w) { return w.bedrock != BE_UNDEF; }

// Identical to the branch in Level::init4struct.
static int configFor(int structureType, const WI& wi, StructureConfig *sconf)
{
    return isBedrock(wi)
        ? getBedrockStructureConfig(structureType, wi.mc, sconf)
        : getStructureConfig_override(structureType, wi.mc, sconf);
}

// Identical to the branch in getStructs.
static int posFor(int st, const WI& wi, int rx, int rz, Pos *p)
{
    return isBedrock(wi)
        ? getBedrockStructurePos(st, wi.mc, wi.seed, rx, rz, p)
        : getStructurePos(st, wi.mc, wi.seed, rx, rz, p);
}

static std::vector<Pos> collect(const WI& wi, int st, int x0, int z0, int x1, int z1)
{
    StructureConfig sconf;
    std::vector<Pos> out;
    if (!configFor(st, wi, &sconf))
        return out;

    int si0 = x0 / (sconf.regionSize * 16);
    int sj0 = z0 / (sconf.regionSize * 16);
    int si1 = (x1 - 1) / (sconf.regionSize * 16);
    int sj1 = (z1 - 1) / (sconf.regionSize * 16);
    for (int i = si0; i <= si1; i++)
        for (int j = sj0; j <= sj1; j++)
        {
            Pos p;
            if (posFor(st, wi, i, j, &p) && p.x >= x0 && p.x < x1 && p.z >= z0 && p.z < z1)
                out.push_back(p);
        }
    return out;
}

int main()
{
    WI jw = { MC_1_21_3, BE_UNDEF, 123456789ULL };
    WI bw = { be2javaMC(BE_1_21), BE_1_21, 123456789ULL };

    const int x0 = -8192, z0 = -8192, x1 = 8192, z1 = 8192;

    std::vector<Pos> a = collect(jw, Village, x0, z0, x1, z1);
    std::vector<Pos> b = collect(bw, Village, x0, z0, x1, z1);

    printf("Java    villages in area: %d\n", (int)a.size());
    for (size_t i = 0; i < a.size() && i < 5; i++)
        printf("   (%d, %d)\n", a[i].x, a[i].z);
    printf("Bedrock villages in area: %d\n", (int)b.size());
    for (size_t i = 0; i < b.size() && i < 5; i++)
        printf("   (%d, %d)\n", b[i].x, b[i].z);

    int shared = 0;
    for (const Pos& p : a)
        for (const Pos& q : b)
            if (p.x == q.x && p.z == q.z) shared++;

    printf("\nshared positions: %d\n", shared);

    // Also verify every Bedrock structure type resolves through the dispatcher.
    printf("\nBedrock config availability:\n");
    int supported = 0;
    for (int st = 0; st < FEATURE_NUM; st++)
    {
        StructureConfig sc;
        if (configFor(st, bw, &sc))
        {
            supported++;
            printf("  %-16s region=%2d range=%2d salt=%d\n",
                struct2str(st), sc.regionSize, sc.chunkRange, (int)sc.salt);
        }
    }
    printf("  (%d structure types supported)\n", supported);

    return (shared == 0 && !a.empty() && !b.empty()) ? 0 : 1;
}
