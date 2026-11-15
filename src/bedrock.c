#include "bedrock.h"

#include "cubiomes/rng.h"
#include "cubiomes/biomes.h"
#include "cubiomes/util.h"

#include <string.h>
#include <stdlib.h>


///=============================================================================
///                        Mersenne Twister (MT19937)
///=============================================================================
/*
 * Bedrock seeds its structure RNG by initializing only a small prefix of the
 * state array ("mSetSeed"), which is why the reference implementation below
 * writes n + 396 entries rather than the full 624.
 */

#define MT_SIZE     624
#define MT_M        397
#define MT_MATRIX_A 0x9908b0dfU
#define MT_UPPER_MASK 0x80000000U
#define MT_LOWER_MASK 0x7fffffffU

struct MersenneTwister
{
    uint32_t array[MT_SIZE];
    uint16_t index;
};

static void mtTwist(struct MersenneTwister *mt)
{
    static const uint32_t mag01[2] = {0, MT_MATRIX_A};
    int i;

    for (i = 0; i < MT_SIZE - MT_M; i++)
    {
        uint32_t y = (mt->array[i] & MT_UPPER_MASK)
                   | (mt->array[i+1] & MT_LOWER_MASK);
        mt->array[i] = mt->array[i+MT_M] ^ (y >> 1) ^ mag01[y & 1];
    }
    for (; i < MT_SIZE - 1; i++)
    {
        uint32_t y = (mt->array[i] & MT_UPPER_MASK)
                   | (mt->array[i+1] & MT_LOWER_MASK);
        mt->array[i] = mt->array[i-(MT_SIZE-MT_M)] ^ (y >> 1) ^ mag01[y & 1];
    }
    {
        uint32_t y = (mt->array[MT_SIZE-1] & MT_UPPER_MASK)
                   | (mt->array[0] & MT_LOWER_MASK);
        mt->array[MT_SIZE-1] = mt->array[MT_M-1] ^ (y >> 1) ^ mag01[y & 1];
    }

    mt->index = 0;
}

static void mtSetSeed(struct MersenneTwister *mt, uint64_t seed, int n)
{
    if (n > 0)
    {
        int end = MT_SIZE - 1;
        int i;
        if (n + 396 < end)
            end = n + 396;
        mt->array[0] = (uint32_t)seed;
        for (i = 1; i <= end; i++)
        {
            uint32_t prev = mt->array[i-1];
            mt->array[i] = (uint32_t)(1812433253U * (prev ^ (prev >> 30)) + i);
        }
    }
    mt->index = MT_SIZE;
}

static uint32_t mtNext(struct MersenneTwister *mt)
{
    uint32_t y;
    if (mt->index >= MT_SIZE)
        mtTwist(mt);

    y = mt->array[mt->index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    return y ^ (y >> 18);
}

static int mtNextInt(struct MersenneTwister *mt, int n)
{
    return (int)(mtNext(mt) % (uint32_t)n);
}

static uint32_t mtNextIntUnbound(struct MersenneTwister *mt)
{
    return mtNext(mt) >> 1;
}

static float mtNextFloat(struct MersenneTwister *mt)
{
    return (float)(mtNext(mt) * (1.0 / 4294967296.0));
}


///=============================================================================
///                       Bedrock Structure Placement
///=============================================================================

// Seed mixing constants used by the region based placement.
#define BEDROCK_SALT_X   UINT64_C(341873128712)
#define BEDROCK_SALT_Z   UINT64_C(132897987541)
// Structures are offset by half a chunk into the chunk centre.
#define BEDROCK_CHUNK_OFFSET 8

static uint64_t bedrockMixSeed(uint64_t seed, int regX, int regZ, uint64_t salt)
{
    return regX * BEDROCK_SALT_X + regZ * BEDROCK_SALT_Z + seed + salt;
}

// Uniformly distributed: the structure can be anywhere inside the region.
static Pos bedrockFeaturePos(const StructureConfig *sconf, uint64_t seed,
        int regX, int regZ)
{
    struct MersenneTwister mt;
    uint64_t mixedSeed = bedrockMixSeed(seed, regX, regZ, sconf->salt);
    int range = sconf->chunkRange;
    Pos p;

    mtSetSeed(&mt, mixedSeed, 2);
    p.x = mtNextInt(&mt, range);
    p.z = mtNextInt(&mt, range);

    p.x = (int)(((uint64_t)regX * sconf->regionSize + p.x) << 4)
        + BEDROCK_CHUNK_OFFSET;
    p.z = (int)(((uint64_t)regZ * sconf->regionSize + p.z) << 4)
        + BEDROCK_CHUNK_OFFSET;
    return p;
}

// Triangularly distributed: the average of two uniform samples.
static Pos bedrockLargeStructurePos(const StructureConfig *sconf, uint64_t seed,
        int regX, int regZ)
{
    struct MersenneTwister mt;
    uint64_t mixedSeed = bedrockMixSeed(seed, regX, regZ, sconf->salt);
    int range = sconf->chunkRange;
    Pos p;

    mtSetSeed(&mt, mixedSeed, 4);
    p.x = (mtNextInt(&mt, range) + mtNextInt(&mt, range)) >> 1;
    p.z = (mtNextInt(&mt, range) + mtNextInt(&mt, range)) >> 1;

    p.x = (int)(((uint64_t)regX * sconf->regionSize + p.x) << 4)
        + BEDROCK_CHUNK_OFFSET;
    p.z = (int)(((uint64_t)regZ * sconf->regionSize + p.z) << 4)
        + BEDROCK_CHUNK_OFFSET;
    return p;
}

// Per-chunk decoration RNG, used by mineshafts and similar features.
static void bedrockChunkGenerateRnd(uint64_t worldseed, int chunkX, int chunkZ,
        int n, struct MersenneTwister *mt)
{
    uint64_t r1, r2, mixedSeed;

    mtSetSeed(mt, worldseed, 2);
    r1 = mtNextIntUnbound(mt);
    r2 = mtNextIntUnbound(mt);

    mixedSeed = (r1 * (uint32_t)chunkX) ^ (r2 * (uint32_t)chunkZ) ^ worldseed;
    mtSetSeed(mt, mixedSeed, n);
}


///=============================================================================
///                          Version Conversion
///=============================================================================

const char *be2str(int be)
{
    switch (be)
    {
    case BE_1_18:   return "Bedrock 1.18";
    case BE_1_19:   return "Bedrock 1.19";
    case BE_1_20:   return "Bedrock 1.20";
    case BE_1_21:   return "Bedrock 1.21";
    }
    return "Bedrock ?";
}

int str2be(const char *s)
{
    int i;
    if (!s)
        return BE_UNDEF;
    if (strncmp(s, "Bedrock", 7) != 0)
        return BE_UNDEF;
    for (i = BE_NEWEST; i >= BE_1_18; i--)
    {
        const char *name = be2str(i);
        if (strcmp(s, name) == 0)
            return i;
    }
    return BE_UNDEF;
}

int be2javaMC(int be)
{
    switch (be)
    {
    // Biome generation has been identical since the 1.18 "Caves & Cliffs"
    // rework, so each Bedrock version maps onto the closest Java version.
    case BE_1_18:   return MC_1_18_2;
    case BE_1_19:   return MC_1_19_4;
    case BE_1_20:   return MC_1_20_6;
    case BE_1_21:   return MC_1_21_3;
    }
    return MC_UNDEF;
}


///=============================================================================
///                     Bedrock Structure Configuration
///=============================================================================

int getBedrockStructureConfig(int structureType, int mc, StructureConfig *sconf)
{
    // The salt, region size and chunk range follow the Bedrock worldgen
    // behaviour; they are not interchangeable with the Java values.
    static const StructureConfig
    s_ancient_city    = { 20083232, 24, 16, Ancient_City,    DIM_OVERWORLD, 0},
    s_desert_pyramid  = { 14357617, 32, 24, Desert_Pyramid,  DIM_OVERWORLD, 0},
    s_igloo           = { 14357617, 32, 24, Igloo,           DIM_OVERWORLD, 0},
    s_jungle_pyramid  = { 14357617, 32, 24, Jungle_Pyramid,  DIM_OVERWORLD, 0},
    s_mansion         = { 10387319, 80, 60, Mansion,         DIM_OVERWORLD, 0},
    s_mineshaft       = {        0,  1,  1, Mineshaft,       DIM_OVERWORLD, 0},
    s_monument        = { 10387313, 32, 27, Monument,        DIM_OVERWORLD, 0},
    s_ocean_ruin_17   = { 14357621, 12,  5, Ocean_Ruin,      DIM_OVERWORLD, 0},
    s_ocean_ruin      = { 14357621, 20, 12, Ocean_Ruin,      DIM_OVERWORLD, 0},
    s_outpost         = {165745296, 80, 56, Outpost,         DIM_OVERWORLD, 0},
    s_ruined_portal   = { 40552231, 40, 25, Ruined_Portal,   DIM_OVERWORLD, 0},
    s_shipwreck_117   = {165745295, 10,  5, Shipwreck,       DIM_OVERWORLD, 0},
    s_shipwreck       = {165745295, 24, 20, Shipwreck,       DIM_OVERWORLD, 0},
    s_swamp_hut       = { 14357617, 32, 24, Swamp_Hut,       DIM_OVERWORLD, 0},
    s_trail_ruins     = { 83469867, 34, 26, Trail_Ruins,     DIM_OVERWORLD, 0},
    s_treasure        = { 16842397,  4,  2, Treasure,        DIM_OVERWORLD, 0},
    s_trial_chambers  = { 94251327, 34, 22, Trial_Chambers,  DIM_OVERWORLD, 0},
    s_village_117     = { 10387312, 27, 17, Village,         DIM_OVERWORLD, 0},
    s_village         = { 10387312, 34, 26, Village,         DIM_OVERWORLD, 0},
    // Nether structures
    s_bastion         = { 30084232, 30, 26, Bastion,         DIM_NETHER,    0},
    s_fortress        = { 30084232, 30, 26, Fortress,        DIM_NETHER,    0},
    s_ruined_portal_n = { 40552231, 25, 15, Ruined_Portal_N, DIM_NETHER,    0},
    // End structures
    s_end_city        = { 10387313, 20,  9, End_City,        DIM_END,       0};

    switch (structureType)
    {
    case Ancient_City:
        *sconf = s_ancient_city;
        return mc >= MC_1_19_2;
    case Desert_Pyramid:
        *sconf = s_desert_pyramid;
        return mc >= MC_1_14;
    case Igloo:
        *sconf = s_igloo;
        return mc >= MC_1_14;
    case Jungle_Pyramid:
        *sconf = s_jungle_pyramid;
        return mc >= MC_1_14;
    case Mansion:
        *sconf = s_mansion;
        return mc >= MC_1_14;
    case Mineshaft:
        *sconf = s_mineshaft;
        return mc >= MC_1_14;
    case Monument:
        *sconf = s_monument;
        return mc >= MC_1_14;
    case Ocean_Ruin:
        *sconf = mc <= MC_1_17 ? s_ocean_ruin_17 : s_ocean_ruin;
        return mc >= MC_1_16;
    case Outpost:
        *sconf = s_outpost;
        return mc >= MC_1_14;
    case Ruined_Portal:
        *sconf = s_ruined_portal;
        return mc >= MC_1_14;
    case Shipwreck:
        *sconf = mc <= MC_1_17 ? s_shipwreck_117 : s_shipwreck;
        return mc >= MC_1_14;
    case Swamp_Hut:
        *sconf = s_swamp_hut;
        return mc >= MC_1_14;
    case Trail_Ruins:
        *sconf = s_trail_ruins;
        return mc >= MC_1_20;
    case Treasure:
        *sconf = s_treasure;
        return mc >= MC_1_14;
    case Trial_Chambers:
        *sconf = s_trial_chambers;
        return mc >= MC_1_21_1;
    case Village:
        *sconf = mc <= MC_1_17 ? s_village_117 : s_village;
        return mc >= MC_1_14;
    case Bastion:
        *sconf = s_bastion;
        return mc >= MC_1_14;
    case Fortress:
        *sconf = s_fortress;
        return mc >= MC_1_14;
    case Ruined_Portal_N:
        *sconf = s_ruined_portal_n;
        return mc >= MC_1_14;
    case End_City:
        *sconf = s_end_city;
        return mc >= MC_1_14;
    default:
        memset(sconf, 0, sizeof(StructureConfig));
        return 0;
    }
}


int getBedrockStructurePos(int structureType, int mc, uint64_t seed,
        int regX, int regZ, Pos *pos)
{
    StructureConfig sconf;

    if (!getBedrockStructureConfig(structureType, mc, &sconf))
        return 0;

    switch (structureType)
    {
    // Uniformly distributed
    case Desert_Pyramid:
    case Igloo:
    case Jungle_Pyramid:
    case Ruined_Portal:
    case Swamp_Hut:
    case Bastion:
    case Fortress:
    case Ruined_Portal_N:
        *pos = bedrockFeaturePos(&sconf, seed, regX, regZ);
        return 1;

    // Triangularly distributed
    case Ancient_City:
    case Mansion:
    case Monument:
    case Outpost:
    case Treasure:
    case Village:
        *pos = bedrockLargeStructurePos(&sconf, seed, regX, regZ);
        return 1;

    // Distribution changed between versions
    case Ocean_Ruin:
    case Shipwreck:
        *pos = (mc <= MC_1_17 ? bedrockLargeStructurePos : bedrockFeaturePos)
                (&sconf, seed, regX, regZ);
        return 1;

    // Placed with the Java algorithm in Bedrock as well
    case Trail_Ruins:
    case Trial_Chambers:
        return getStructurePos(structureType, mc, seed, regX, regZ, pos);

    case End_City:
        *pos = bedrockLargeStructurePos(&sconf, seed, regX, regZ);
        // End cities only generate outside of the central island.
        return pos->x * (int64_t)pos->x + pos->z * (int64_t)pos->z
            >= 1008 * INT64_C(1008);

    case Mineshaft:
    {
        struct MersenneTwister mt;
        pos->x = regX * 16;
        pos->z = regZ * 16;
        bedrockChunkGenerateRnd(seed, regX, regZ, 3, &mt);
        mtNextIntUnbound(&mt);
        return mtNextFloat(&mt) < 0.004f
            && mtNextInt(&mt, 80) < (IABS(regX) > IABS(regZ) ? IABS(regX) : IABS(regZ));
    }

    default:
        return 0;
    }
}

int isViableBedrockStructurePos(int structureType, Generator *g,
        int blockX, int blockZ, uint32_t flags)
{
    // The biome layer is shared with Java, so the viability rules agree for
    // all structures that both editions have in common.
    return isViableStructurePos(structureType, g, blockX, blockZ, flags);
}
