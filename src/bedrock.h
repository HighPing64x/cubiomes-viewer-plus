#ifndef BEDROCK_H_
#define BEDROCK_H_

#include "cubiomes/finders.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Minecraft Bedrock Edition support.
 *
 * Since 1.18 the biome generation of Bedrock and Java are identical, so the
 * biome layer of the viewer can be reused unchanged by simply mapping a
 * Bedrock version onto the equivalent Java version (see be2javaMC()).
 *
 * Structures, however, are NOT placed the same way. Bedrock uses a 32-bit
 * Mersenne Twister over region seeds instead of the Java LCG, and the region
 * layout differs per structure type. The functions below mirror that
 * behaviour so that the map can display Bedrock structure locations.
 */

// Bedrock versions for which the structure layout is known.
// These values are also used by WorldInfo::bedrock in config.h.
enum BedrockVersion
{
    BE_UNDEF = -1,
    BE_1_18,
    BE_1_19,
    BE_1_20,
    BE_1_21,
    BE_NEWEST = BE_1_21,
};

/// convert between Bedrock version enum and text
const char *be2str(int be);
int str2be(const char *s);

/**
 * Maps a Bedrock version onto the Java version with the equivalent biome
 * generation. Used so that the biome generator and all biome dependent code
 * can be shared between the editions.
 */
int be2javaMC(int be);

/**
 * Returns the Bedrock structure configuration for the given structure type.
 * The 'mc' argument is the Java version that the Bedrock world was mapped to
 * (see be2javaMC). Returns zero if the structure is not supported.
 */
int getBedrockStructureConfig(int structureType, int mc, StructureConfig *sconf);

/**
 * Gets the position (in blocks) of the structure in the given region, using
 * the Bedrock placement algorithm. Returns zero if the region does not
 * contain the structure.
 */
int getBedrockStructurePos(int structureType, int mc, uint64_t seed,
        int regX, int regZ, Pos *pos);

/**
 * Checks whether the biome at the given block position is suitable for the
 * structure. For Bedrock this largely agrees with the Java rules, since the
 * biome layer is shared.
 */
int isViableBedrockStructurePos(int structureType, Generator *g,
        int blockX, int blockZ, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif /* BEDROCK_H_ */
