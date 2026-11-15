#include "route.h"

#include "util.h"

#include <QCoreApplication>

#include <cmath>
#include <cstring>
#include <queue>
#include <vector>
#include <algorithm>

/*
 * The planner works on a regular grid of "cells" covering the bounding box of
 * the start and end position. Each cell is classified once, which keeps the
 * amount of biome queries low, and the A* search then runs purely on that
 * cached classification.
 */

// Terrain classes, avoiding a dependency on the exact biome ids in the search.
enum
{
    TC_BLOCKED, // not traversable with the selected modes
    TC_LAND,
    TC_SEA,
};

// Speed (in blocks per second) used to weight the traversability of a cell.
static const double SPEED_ROAD     = 7.0;   // plains and other open terrain
static const double SPEED_FOREST   = 4.5;   // dense vegetation
static const double SPEED_SAND     = 5.0;   // desert, beaches
static const double SPEED_SNOW     = 3.5;   // snow layers slow movement down
static const double SPEED_MOUNTAIN = 2.5;   // steep terrain
static const double SPEED_WATER    = 8.0;   // boat on open water
static const double SPEED_SWIM     = 2.0;   // swimming without a boat

static bool biomeIsWater(int id)
{
    return isOceanic(id) || id == river || id == frozen_river;
}

static bool biomeIsHazard(int id)
{
    // Lava lakes and other biomes that should be avoided if requested.
    return id == the_void;
}

/*
 * Returns a relative traversal speed for the given biome.
 * A return value of zero means the biome cannot be crossed.
 */
static double terrainSpeed(int id, int modes)
{
    bool water = biomeIsWater(id);

    if (water)
    {
        if (!(modes & ROUTE_SEA))
            return 0;
        // Deep water is faster to cross by boat than a shallow river.
        return isDeepOcean(id) ? SPEED_WATER : SPEED_SWIM + 2.0;
    }

    if (!(modes & ROUTE_LAND))
        return 0;

    switch (id)
    {
    case ocean: case frozen_ocean: case warm_ocean:
    case lukewarm_ocean: case cold_ocean: case deep_ocean:
    case deep_warm_ocean: case deep_lukewarm_ocean:
    case deep_cold_ocean: case deep_frozen_ocean:
    case river: case frozen_river:
        return 0; // handled above, kept for clarity

    case mushroom_fields: case mushroom_field_shore:
    case stone_shore: case beach: case snowy_beach:
        return SPEED_SAND;

    case mountains: case mountain_edge: case wooded_mountains:
    case snowy_mountains: case gravelly_mountains:
    case modified_gravelly_mountains:
        return SPEED_MOUNTAIN;

    case desert: case desert_hills: case badlands:
    case badlands_plateau: case wooded_badlands_plateau:
    case modified_wooded_badlands_plateau:
        return SPEED_SAND;

    case snowy_tundra: case snowy_taiga: case snowy_taiga_hills:
    case ice_spikes:
        return SPEED_SNOW;

    case jungle: case jungle_hills: case jungle_edge:
    case bamboo_jungle: case bamboo_jungle_hills:
    case dark_forest: case dark_forest_hills:
        return SPEED_FOREST;

    case forest: case wooded_hills: case birch_forest:
    case birch_forest_hills: case taiga: case taiga_hills:
    case giant_tree_taiga: case giant_tree_taiga_hills:
    case flower_forest: case sunflower_plains:
        return SPEED_FOREST;

    default:
        return SPEED_ROAD;
    }
}

double routeCellSpeed(int biome, bool water)
{
    if (water)
        return biomeIsWater(biome) ? SPEED_WATER : 0;
    double v = terrainSpeed(biome, ROUTE_LAND);
    return v > 0 ? v : 0;
}

QString routeBiomeDesc(int biome)
{
    if (biome < 0)
        return QString();
    bool water = biomeIsWater(biome);
    if (water)
        return QCoreApplication::translate("Route", "water");
    return QCoreApplication::translate("Route", "land");
}

void routeLegs(const RouteResult& result, std::vector<RouteLeg> *legs)
{
    legs->clear();
    const size_t n = result.path.size();
    if (!result.valid || n == 0)
        return;

    size_t i = 0;
    while (i < n)
    {
        const bool water = result.path[i].water;

        // Extend the leg while the terrain type stays the same.
        size_t j = i;
        while (j + 1 < n && result.path[j+1].water == water)
            j++;

        // Segments are attributed to the cell they leave, so each segment is
        // counted exactly once across all legs. The leg therefore reaches from
        // the first cell it covers to the end of its last segment, which also
        // keeps consecutive legs contiguous.
        double len = 0;
        for (size_t k = i; k <= j && k + 1 < n; k++)
        {
            double dx = result.path[k+1].p.x - result.path[k].p.x;
            double dz = result.path[k+1].p.z - result.path[k].p.z;
            len += sqrt(dx*dx + dz*dz);
        }
        const size_t endidx = (j + 1 < n) ? j + 1 : j;

        RouteLeg leg;
        leg.water = water;
        leg.length = len;
        leg.from = result.path[i].p;
        leg.to = result.path[endidx].p;
        legs->push_back(leg);

        i = j + 1;
    }
}


RouteConfig::RouteConfig()
{
    reset();
}

void RouteConfig::reset()
{
    start = Pos{0, 0};
    end = Pos{0, 0};
    modes = ROUTE_LAND | ROUTE_SEA;
    optimize = ROUTE_OPT_FASTEST;
    avoidHazards = true;
    gridsize = 512;
}

void RouteConfig::load(QSettings& settings)
{
    start.x = settings.value("route/startx", start.x).toInt();
    start.z = settings.value("route/startz", start.z).toInt();
    end.x = settings.value("route/endx", end.x).toInt();
    end.z = settings.value("route/endz", end.z).toInt();
    modes = settings.value("route/modes", modes).toInt();
    optimize = settings.value("route/optimize", optimize).toInt();
    avoidHazards = settings.value("route/avoidHazards", avoidHazards).toBool();
    gridsize = settings.value("route/gridsize", gridsize).toInt();

    if (!(modes & (ROUTE_LAND | ROUTE_SEA)))
        modes = ROUTE_LAND | ROUTE_SEA;
    if (gridsize < 64)
        gridsize = 64;
    if (gridsize > 4096)
        gridsize = 4096;
}

void RouteConfig::save(QSettings& settings)
{
    settings.setValue("route/startx", start.x);
    settings.setValue("route/startz", start.z);
    settings.setValue("route/endx", end.x);
    settings.setValue("route/endz", end.z);
    settings.setValue("route/modes", modes);
    settings.setValue("route/optimize", optimize);
    settings.setValue("route/avoidHazards", avoidHazards);
    settings.setValue("route/gridsize", gridsize);
}


RouteResult::RouteResult()
{
    clear();
}

void RouteResult::clear()
{
    valid = false;
    error.clear();
    path.clear();
    distance = 0;
    traveltime = 0;
    nland = 0;
    nsea = 0;
    start = Pos{0, 0};
    end = Pos{0, 0};
}


namespace {

// One node of the search frontier, ordered by the smallest f-score.
struct QueueNode
{
    QueueNode() : f(),idx() {}
    QueueNode(double f_, int idx_) : f(f_),idx(idx_) {}

    bool operator<(const QueueNode& o) const
    {
        // std::priority_queue is a max-heap, so invert the comparison.
        return f > o.f;
    }

    double f;
    int idx;
};

} // namespace


void computeRoute(RouteResult *result, const Generator *g,
        const RouteConfig& config, const std::atomic_bool *abort)
{
    result->clear();
    result->start = config.start;
    result->end = config.end;

    const int modes = config.modes;

    // The grid spans the bounding box of both endpoints with a small margin, so
    // that a detour around an obstacle is possible even near the border.
    int x0 = qMin(config.start.x, config.end.x);
    int z0 = qMin(config.start.z, config.end.z);
    int x1 = qMax(config.start.x, config.end.x);
    int z1 = qMax(config.start.z, config.end.z);

    int spanx = x1 - x0;
    int spanz = z1 - z0;
    int span = qMax(spanx, spanz);
    if (span < 16)
        span = 16;

    // Add a margin of 1/4 of the span (at least 128 blocks) on every side.
    int margin = qMax(span / 4, 128);
    x0 -= margin; z0 -= margin;
    x1 += margin; z1 += margin;
    spanx = x1 - x0;
    spanz = z1 - z0;
    span = qMax(spanx, spanz);

    // Grid resolution: aim for the configured number of cells on the long axis.
    int ncells = qMax(config.gridsize, 64);
    double cell = (double)span / ncells;
    if (cell < 1.0)
        cell = 1.0;

    // The biome map is sampled at the native 1:4 scale. For the fast row based
    // path the cell stride must divide cleanly into that scale, otherwise the
    // samples drift away from the cell centres over long distances. The cell
    // size is therefore snapped down to a multiple of four blocks, and very
    // short routes fall back to one query per cell (few enough to be cheap).
    bool percell = cell < 4.0;
    int step = 1;
    if (!percell)
    {
        step = (int)floor(cell / 4.0);
        // Bound the number of generated samples for very large distances by
        // coarsening the grid until the row buffer is reasonable.
        for (;;)
        {
            cell = (double)step * 4.0;
            int64_t w = (int64_t)ceil(spanx / cell) + 1;
            int64_t h = (int64_t)ceil(spanz / cell) + 1;
            if (w * h * step <= (INT64_C(1) << 24) || step >= 4096)
                break;
            step++;
        }
    }

    int gw = (int)ceil(spanx / cell) + 1;
    int gh = (int)ceil(spanz / cell) + 1;
    if (gw < 2) gw = 2;
    if (gh < 2) gh = 2;

    // Guard against absurd allocations if the endpoints are very far apart.
    if ((int64_t)gw * gh > (INT64_C(1) << 22))
    {
        result->error = QCoreApplication::translate("Route",
            "The distance is too large for the selected resolution.");
        return;
    }

    // Sample the biome map into the grid. For large areas whole rows are
    // generated at once and then strided, which is considerably faster than
    // one generator call per cell.
    std::vector<int> biomes((size_t)gw * gh, none);
    {
        // Sea level is sampled for both editions; the vertical position only
        // selects which cave/surface biome is returned and does not affect the
        // horizontal layout that the planner uses.
        const int sampleY = 63;

        if (percell)
        {
            for (int j = 0; j < gh; j++)
            {
                if (abort && *abort)
                    return;
                int bz = (z0 + (int)((j + 0.5) * cell)) >> 2;
                for (int i = 0; i < gw; i++)
                {
                    int bx = (x0 + (int)((i + 0.5) * cell)) >> 2;
                    biomes[(size_t)j * gw + i] = getBiomeAt(g, 4, bx, sampleY, bz);
                }
            }
        }
        else
        {
            Range r;
            r.scale = 4;
            r.sy = 1;
            r.y = sampleY;
            r.x = (x0 >> 2);
            r.sx = gw * step;
            r.sz = 1;
            std::vector<int> rowbuf((size_t)gw * step, none);

            for (int j = 0; j < gh; j++)
            {
                if (abort && *abort)
                    return;

                // Rows advance along z; because the cell size is a multiple of
                // four, the strided samples land exactly on the cell centres.
                r.z = (z0 + (int)((j + 0.5) * cell)) >> 2;

                if (genBiomes(g, &rowbuf[0], r) != 0)
                    continue;

                for (int i = 0; i < gw; i++)
                    biomes[(size_t)j * gw + i] = rowbuf[(size_t)i * step];
            }
        }
    }

    // Classify the cells into traversable terrain classes.
    std::vector<uint8_t> tclass((size_t)gw * gh, TC_BLOCKED);
    std::vector<float> speed((size_t)gw * gh, 0.0f);
    for (int j = 0; j < gh; j++)
    {
        for (int i = 0; i < gw; i++)
        {
            size_t k = (size_t)j * gw + i;
            int id = biomes[k];
            if (id < 0)
                continue;
            if (config.avoidHazards && biomeIsHazard(id))
                continue;

            double sp = terrainSpeed(id, modes);
            if (sp <= 0)
                continue;

            tclass[k] = biomeIsWater(id) ? TC_SEA : TC_LAND;
            speed[k] = (float)sp;
        }
    }

    // Map the start and end positions onto grid indices.
    auto toIndex = [&](Pos p, int *out)
    {
        int i = (int)floor((p.x - x0) / cell);
        int j = (int)floor((p.z - z0) / cell);
        if (i < 0 || i >= gw || j < 0 || j >= gh)
            return false;
        *out = j * gw + i;
        return true;
    };

    int startidx = -1, endidx = -1;
    if (!toIndex(config.start, &startidx) || !toIndex(config.end, &endidx))
    {
        result->error = QCoreApplication::translate("Route",
            "Start or destination is outside of the computed area.");
        return;
    }

    // The endpoints are always considered passable, even if they happen to be
    // sampled in a blocked cell (e.g. on a bridge or inside a structure).
    if (tclass[startidx] == TC_BLOCKED && speed[startidx] <= 0)
        speed[startidx] = SPEED_ROAD;
    if (tclass[endidx] == TC_BLOCKED && speed[endidx] <= 0)
        speed[endidx] = SPEED_ROAD;

    if (startidx == endidx)
    {
        RouteStep st;
        st.p = config.start;
        st.biome = biomes[startidx];
        st.water = biomeIsWater(st.biome);
        st.cost = 0;
        result->path.push_back(st);
        result->distance = 0;
        result->traveltime = 0;
        result->valid = true;
        return;
    }

    const size_t n = (size_t)gw * gh;
    std::vector<double> gscore(n, HUGE_VAL);
    std::vector<int> camefrom(n, -1);
    std::vector<char> closed(n, 0);

    // Heuristic: the straight line distance scaled by the fastest possible
    // speed. For the shortest option the heuristic is purely geometric, which
    // keeps it admissible.
    double bestspeed = SPEED_WATER;
    double hscale = (config.optimize == ROUTE_OPT_SHORTEST) ? 1.0 : 1.0 / bestspeed;

    auto heuristic = [&](int idx)
    {
        int i = idx % gw;
        int j = idx / gw;
        int ei = endidx % gw;
        int ej = endidx / gw;
        double dx = (ei - i) * cell;
        double dz = (ej - j) * cell;
        return sqrt(dx*dx + dz*dz) * hscale;
    };

    // Edge cost of moving onto a cell. Diagonal moves are allowed, but not
    // through blocked corners.
    // For the fastest option the cost is the travel time across the cell, for
    // the shortest option it is the pure geometric distance.
    auto edgeCost = [&](int from, int to)
    {
        if (tclass[to] == TC_BLOCKED && speed[to] <= 0)
            return HUGE_VAL;

        int fi = from % gw, fj = from / gw;
        int ti = to % gw, tj = to / gw;
        double dx = (ti - fi) * cell;
        double dz = (tj - fj) * cell;
        double dist = sqrt(dx*dx + dz*dz);

        if (config.optimize == ROUTE_OPT_SHORTEST)
            return dist;

        float sp = speed[to];
        if (sp <= 0)
            sp = (float)SPEED_ROAD;
        return dist / sp;
    };

    std::priority_queue<QueueNode> open;
    gscore[startidx] = 0;
    open.push(QueueNode(heuristic(startidx), startidx));

    const int nb[8][2] = {
        {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}
    };

    bool found = false;
    while (!open.empty())
    {
        if (abort && *abort)
            return;

        QueueNode node = open.top();
        open.pop();

        int cur = node.idx;
        if (closed[cur])
            continue;
        closed[cur] = 1;

        if (cur == endidx)
        {
            found = true;
            break;
        }

        int ci = cur % gw;
        int cj = cur / gw;

        for (int d = 0; d < 8; d++)
        {
            int ni = ci + nb[d][0];
            int nj = cj + nb[d][1];
            if (ni < 0 || ni >= gw || nj < 0 || nj >= gh)
                continue;

            int nidx = nj * gw + ni;
            if (closed[nidx])
                continue;

            double cost = edgeCost(cur, nidx);
            if (cost >= HUGE_VAL)
                continue;

            // Diagonal moves must not cut through blocked corners.
            if (d >= 4)
            {
                int a = cj * gw + ni;
                int b = nj * gw + ci;
                bool oka = tclass[a] != TC_BLOCKED || speed[a] > 0;
                bool okb = tclass[b] != TC_BLOCKED || speed[b] > 0;
                if (!oka || !okb)
                    continue;
            }

            double tentative = gscore[cur] + cost;
            if (tentative < gscore[nidx])
            {
                gscore[nidx] = tentative;
                camefrom[nidx] = cur;
                open.push(QueueNode(tentative + heuristic(nidx), nidx));
            }
        }
    }

    if (!found)
    {
        result->error = QCoreApplication::translate("Route",
            "No route found with the selected travel modes.");
        return;
    }

    // Reconstruct the path from the destination back to the start.
    std::vector<int> cells;
    for (int cur = endidx; cur >= 0; cur = camefrom[cur])
    {
        cells.push_back(cur);
        if (cur == startidx)
            break;
    }
    std::reverse(cells.begin(), cells.end());

    result->path.reserve(cells.size());
    double prevspeed = 0;
    for (size_t i = 0; i < cells.size(); i++)
    {
        int idx = cells[i];
        int i2 = idx % gw;
        int j2 = idx / gw;

        RouteStep st;
        st.p = Pos{x0 + (int)((i2 + 0.5) * cell), z0 + (int)((j2 + 0.5) * cell)};
        st.biome = biomes[idx];
        st.water = biomeIsWater(st.biome);
        st.cost = gscore[idx];

        // Snap the endpoints onto the exact requested positions.
        if (i == 0)
            st.p = config.start;
        else if (i + 1 == cells.size())
            st.p = config.end;

        result->path.push_back(st);

        if (st.water)
            result->nsea++;
        else
            result->nland++;

        // Travel time is integrated per segment using the slower of the two
        // adjacent cells, which approximates accelerating through a biome edge.
        double sp = speed[idx] > 0 ? speed[idx] : SPEED_ROAD;
        if (i > 0)
        {
            double ddx = result->path[i].p.x - result->path[i-1].p.x;
            double ddz = result->path[i].p.z - result->path[i-1].p.z;
            double seg = sqrt(ddx*ddx + ddz*ddz);
            double usespeed = qMin(sp, prevspeed > 0 ? prevspeed : sp);
            result->distance += seg;
            result->traveltime += seg / usespeed;
        }
        prevspeed = sp;
    }

    result->valid = true;
}
