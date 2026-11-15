# Bedrock Edition support and route planning

This document describes the two features that were added on top of the
upstream [cubiomes-viewer](https://github.com/Cubitect/cubiomes-viewer) tree:
**Bedrock Edition map queries** and **optimal route calculation**.

Both features follow the conventions of the existing code base: C code lives in
`src/` next to the other cubiomes-derived sources, Qt widgets are split into a
`*.cpp` / `*.h` / `*.ui` triple, settings are persisted through `QSettings`
with the same `section/key` scheme, and all user facing strings go through
`tr()` so they can be translated by the existing `.ts` files.

---

## 1. Bedrock Edition support

### 1.1 Background

Since the 1.18 "Caves & Cliffs" rework, **the biome generation of Bedrock and
Java Edition is identical for the same seed**. Only the structure placement
differs: Bedrock uses a 32-bit Mersenne Twister over region seeds, whereas Java
uses a 48-bit LCG.

This is exploited by mapping every Bedrock version onto the equivalent Java
version, so that the whole biome layer of the viewer (biome colours, climate
layers, height shading, …) is reused unchanged.

### 1.2 Edition switch

`WorldInfo` gained a `bedrock` field (`config.h`):

```cpp
struct WorldInfo
{
    int mc;        // Java version used by the biome generator
    bool large;
    uint64_t seed;
    int y;
    int bedrock;   // BE_UNDEF for Java worlds
    bool isBedrock() const { return bedrock != BE_UNDEF; }
};
```

* A **Java world** stores `bedrock = BE_UNDEF` and uses `mc` directly.
* A **Bedrock world** stores `bedrock = BE_x_xx` and sets `mc` to the Java
  version returned by `be2javaMC()`, so that every biome query stays valid.

The version combo box lists the Bedrock versions after the Java ones
(`"Bedrock 1.18"` … `"Bedrock 1.21"`). `MainWindow::getSeed()` recognises them
through `str2be()`, and `WorldInfo::equals()` compares the new field, which
makes the map cache invalidate correctly when the edition changes.

Version mapping (`src/bedrock.c`):

| Bedrock | Java equivalent |
|---------|-----------------|
| 1.18    | 1.18.2          |
| 1.19    | 1.19.4          |
| 1.20    | 1.20.6          |
| 1.21    | 1.21.3          |

### 1.3 Structure placement

`src/bedrock.c` / `src/bedrock.h` implement the Bedrock placement rules:

* `getBedrockStructureConfig()` returns the Bedrock salt, region size and chunk
  range per structure type. These differ from the Java values.
* `getBedrockStructurePos()` computes the structure position inside a region.
  Two distributions exist:
  * **uniform** — a single `MT19937` sample per axis (desert pyramid, igloo,
    jungle temple, swamp hut, ruined portal, bastion, fortress, …)
  * **triangular** — the average of two samples (village, monument, mansion,
    outpost, ancient city, buried treasure, …)
* A few structures behave specially:
  * `Trail_Ruins` and `Trial_Chambers` are placed with the **Java** algorithm.
  * `End_City` is filtered by distance from the origin.
  * `Mineshaft` uses the per-chunk decoration RNG.

The region seed is mixed with the Bedrock constants
`341873128712` (X) and `132897987541` (Z), and the resulting chunk is offset by
8 blocks into the chunk centre.

Two call sites dispatch on the edition:

```cpp
// world.cpp — Level::init4struct() / Quad::run()
int ok = wi.isBedrock()
    ? getBedrockStructureConfig(structureType, wi.mc, &sconf)
    : getStructureConfig_override(structureType, wi.mc, &sconf);

// world.cpp — getStructs()
int ok = bedrock
    ? getBedrockStructurePos(sconf.structType, wi.mc, wi.seed, i, j, &p)
    : getStructurePos(sconf.structType, wi.mc, wi.seed, i, j, &p);
```

Only the positions are taken from Bedrock; the biome check that decides
whether a structure actually generates is shared with Java
(`isViableBedrockStructurePos()`), which is valid because the biome layer is
identical since 1.18.

---

## 2. Optimal route calculation

### 2.1 Model

`src/route.cpp` implements a weighted **A\*** search over a coarse grid that
covers the bounding box of the two endpoints plus a margin of a quarter of the
span (at least 128 blocks), so that detours around obstacles remain possible.

1. The biome map is sampled once at the native 1:4 scale, one generator row per
   grid row (strided to the cell spacing). This keeps the number of biome
   queries low.
2. Every cell is classified as `TC_LAND`, `TC_SEA` or `TC_BLOCKED` and assigned
   a relative traversal speed that depends on the biome (open plains are fast,
   mountains and snow are slow, dense forest in between).
3. A\* runs over the 8-connected grid. Diagonal moves are permitted but may not
   cut through blocked corners.

### 2.2 Travel modes

The modes are combined as flags (`route.h`), which is what the "land / sea"
checkboxes map onto:

| Flag         | Meaning                                              |
|--------------|------------------------------------------------------|
| `ROUTE_LAND` | walkable terrain only (no water)                     |
| `ROUTE_SEA`  | navigable water only (boat / swimming)               |
| both         | mixed route, may switch between land and water       |

With only one flag selected the route is restricted to that terrain, and a
clear error is reported when the destination is unreachable — e.g. a land-only
route across an ocean fails instead of silently swimming.

### 2.3 Optimisation goal

`optimize` selects the edge cost:

* `ROUTE_OPT_FASTEST` — edge cost is `distance / speed`, i.e. travel time.
  The heuristic is scaled by the fastest possible speed to stay admissible.
* `ROUTE_OPT_SHORTEST` — edge cost is the pure geometric distance, so the
  result never exceeds the fastest route in length.

### 2.4 Result

`RouteResult` contains the waypoint list (start and end snapped to the exact
requested coordinates), the total distance in blocks, the integrated travel
time in seconds, and the land/water cell counts.

The `RouteDialog` shows the summary and collapses consecutive cells of the same
terrain into readable legs:

```
Route from (0, 0) to (4000, 2500)

on land    4739 blocks   (0, 0) -> (3658, 2193)
by water    470 blocks   (3669, 2205) -> (4000, 2500)
```

### 2.5 Map overlay

The route is pushed to the map through the existing `Shape` mechanism. Instead
of emitting independent segments, `QWorld::draw()` collects all `Shape::LINE`
entries and draws them as one connected `QPainterPath` in blue, with the start
filled and the destination outlined, so the planned path stands out clearly
from the red structure bounding boxes.

---

## 3. Menu entry

`Map ▸ Route…` (shortcut `Ctrl+R`) opens the dialog. The configuration
(start, destination, modes, optimisation goal, avoid hazards, grid size) is
stored in the `[route]` settings group and restored on the next start, matching
how the other dialogs persist their state.

`Map ▸ Go to…` (`Ctrl+G`) is unchanged.

---

## 4. Tests

`src/tests/` contains standalone tests that do not require the GUI:

```
cd src/tests
run_tests.bat
```

| Test                 | What it checks                                                              |
|----------------------|-----------------------------------------------------------------------------|
| `test_bedrock.c`     | 882 structure positions against an independent reimplementation of the algorithm |
| `test_integration.c` | edition switch, biome parity, region containment, unsupported structures     |
| `test_dispatch.cpp`  | the dispatcher used by the map really picks the Bedrock path per edition     |
| `test_route.cpp`     | route validity, endpoint preservation, land/sea restriction, fastest vs shortest |

`test_gui.cpp` is an optional smoke test harness that needs the compiled
objects of the application; it switches to a Bedrock version, opens the route
dialog and writes screenshots to `shots/`.

Verified results:

* Bedrock placement: **882 / 882** positions match the independent
  reimplementation of the documented algorithm.
* Biome parity between `Bedrock 1.21` and `Java 1.21.3`: **0 / 2601** mismatches.
* Village placement over 289 regions: **289 differ, 0 identical** — the edition
  switch genuinely changes the structure layout.
* Route planner: mixed, land-only, sea-only, degenerate and long-distance cases
  all behave as specified.

---

## 5. Building

```bat
rem  adjust these two paths to your own Qt and MinGW installation
set PATH=C:\Qt\5.15.2\mingw81_64\bin;C:\Qt\Tools\mingw810_64\bin;%PATH%
mkdir build && cd build
qmake ..\cubiomes-viewer.pro
mingw32-make -j8
```

The resulting `release\cubiomes-viewer.exe` needs the Qt5 (Core/Gui/Widgets)
DLLs plus the platform, style and image-format plugins.

---

## 6. Known limitations

* Bedrock structure *contents* (jigsaw layouts, chest loot) are not modelled —
  only the placement, which is what the map displays.
* The Bedrock region constants are known for the structures listed in
  `getBedrockStructureConfig()`; types without a known Bedrock configuration
  are not drawn in Bedrock mode rather than being drawn incorrectly.
* Version support starts at Bedrock 1.18, because that is where biome parity
  with Java begins.
