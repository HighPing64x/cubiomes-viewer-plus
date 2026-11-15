#ifndef ROUTE_H
#define ROUTE_H

#include "config.h"

#include <QString>
#include <QVector>

#include <vector>
#include <atomic>

/*
 * Optimal route calculation between two positions of a Minecraft world.
 *
 * The planner samples the biome map once into a coarse grid and then runs a
 * weighted A* search over that grid. The traversal cost of a cell depends on
 * the biome, i.e. whether the cell is walkable, swimmable or blocked, and on
 * whether the selected travel mode is allowed to use it.
 *
 * Travel modes are combined as flags, so that e.g. a route may use both land
 * and sea, while a boat-only route is restricted to water.
 */

// travel modes
enum {
    ROUTE_LAND  = 0x1, // walkable terrain (no water)
    ROUTE_SEA   = 0x2, // navigable water (boat / swimming)
};

// how strongly the route prefers the fastest terrain over the shortest one
enum {
    ROUTE_OPT_FASTEST,
    ROUTE_OPT_SHORTEST,
};

struct RouteConfig
{
    RouteConfig();

    void reset();
    void load(QSettings& settings);
    void save(QSettings& settings);
    void load() { QSettings s(APP_STRING, APP_STRING); load(s); }
    void save() { QSettings s(APP_STRING, APP_STRING); save(s); }

    Pos start;
    Pos end;
    int modes;      // combination of ROUTE_LAND / ROUTE_SEA
    int optimize;   // ROUTE_OPT_FASTEST / ROUTE_OPT_SHORTEST
    bool avoidHazards; // steer around lava, powder snow, etc.
    int gridsize;   // target number of cells along the longer axis
};

// A single waypoint of the computed route.
struct RouteStep
{
    RouteStep() : p(),biome(),water(),cost() {}
    Pos p;
    int biome;
    bool water;
    double cost; // accumulated cost from the start
};

// The result of a route computation.
struct RouteResult
{
    RouteResult();

    void clear();

    bool valid;
    QString error;
    std::vector<RouteStep> path; // from start to end, inclusive
    double distance;   // in blocks, along the path
    double traveltime; // in seconds, using the mode dependent speeds
    int nland;         // number of land cells
    int nsea;          // number of water cells
    Pos start, end;
};

/*
 * Computes the optimal route between the given positions.
 *
 * The generator 'g' must be set up for the Overworld of the desired world, so
 * that the biome queries reflect the requested edition and version.
 *
 * 'abort' may be null. If it becomes non-zero the computation stops early and
 * the result marked invalid.
 *
 * The function is thread safe as long as 'g' is not shared between calls.
 */
void computeRoute(RouteResult *result, const Generator *g,
        const RouteConfig& config, const std::atomic_bool *abort = nullptr);

// speed in blocks per second for a given terrain and travel mode
double routeCellSpeed(int biome, bool water);

// describes the terrain of a biome as a short human readable string
QString routeBiomeDesc(int biome);

#endif // ROUTE_H
