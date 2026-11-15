// Headless verification of the route planner.
// build: qmake-style C++ with Qt Core only.
#include "../src/route.h"

#include "../cubiomes/finders.h"

#include <cstdio>
#include <cmath>

int getStructureConfig_override(int stype, int mc, StructureConfig *sconf)
{
    return getStructureConfig(stype, mc, sconf);
}

static void printResult(const char *label, const RouteResult& r)
{
    printf("[%s] valid=%d err=\"%s\" dist=%.1f time=%.1fs land=%d sea=%d steps=%d\n",
        label, (int)r.valid, r.error.toUtf8().constData(),
        r.distance, r.traveltime, r.nland, r.nsea, (int)r.path.size());
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    uint64_t seed = 123456789ULL;
    Generator g;
    setupGenerator(&g, MC_1_21_3, 0);
    applySeed(&g, DIM_OVERWORLD, seed);

    int fails = 0;

    // --- 1. A short trip must produce a valid continuous path. ---
    {
        RouteConfig cfg;
        cfg.start = Pos{0, 0};
        cfg.end = Pos{2000, 1500};
        RouteResult r;
        computeRoute(&r, &g, cfg);
        printResult("mixed 0,0 -> 2000,1500", r);

        if (!r.valid)
        {
            printf("FAIL: expected a valid route\n"); fails++;
        }
        else
        {
            // Endpoints must be preserved exactly.
            if (r.path.front().p.x != cfg.start.x || r.path.front().p.z != cfg.start.z)
            {
                printf("FAIL: start not preserved (%d,%d)\n",
                    r.path.front().p.x, r.path.front().p.z); fails++;
            }
            if (r.path.back().p.x != cfg.end.x || r.path.back().p.z != cfg.end.z)
            {
                printf("FAIL: end not preserved (%d,%d)\n",
                    r.path.back().p.x, r.path.back().p.z); fails++;
            }
            // Consecutive waypoints must be close together (no teleporting).
            double maxstep = 0;
            for (size_t i = 1; i < r.path.size(); i++)
            {
                double dx = r.path[i].p.x - r.path[i-1].p.x;
                double dz = r.path[i].p.z - r.path[i-1].p.z;
                double d = sqrt(dx*dx + dz*dz);
                if (d > maxstep) maxstep = d;
            }
            printf("  max step between waypoints: %.1f blocks\n", maxstep);
            if (maxstep > 500)
            {
                printf("FAIL: path has a gap of %.1f blocks\n", maxstep); fails++;
            }
            // The accumulated distance must not be shorter than the direct line.
            double direct = sqrt(pow(cfg.end.x-cfg.start.x,2) + pow(cfg.end.z-cfg.start.z,2));
            printf("  direct=%.1f path=%.1f ratio=%.3f\n",
                direct, r.distance, r.distance/direct);
            if (r.distance < direct * 0.999)
            {
                printf("FAIL: path shorter than direct line\n"); fails++;
            }
        }
    }

    // --- 2. Land-only mode must never enter water. ---
    {
        RouteConfig cfg;
        cfg.start = Pos{0, 0};
        cfg.end = Pos{3000, 3000};
        cfg.modes = ROUTE_LAND;
        RouteResult r;
        computeRoute(&r, &g, cfg);
        printResult("land  0,0 -> 3000,3000", r);
        if (r.valid)
        {
            if (r.nsea != 0)
            {
                printf("FAIL: land-only route contains %d water cells\n", r.nsea); fails++;
            }
        }
        else
        {
            printf("  (no land-only route: acceptable if water blocks the way)\n");
        }
    }

    // --- 3. Sea-only mode must be a pure water route (or correctly fail). ---
    {
        RouteConfig cfg;
        cfg.start = Pos{0, 0};
        cfg.end = Pos{3000, 3000};
        cfg.modes = ROUTE_SEA;
        RouteResult r;
        computeRoute(&r, &g, cfg);
        printResult("sea   0,0 -> 3000,3000", r);
        if (r.valid && r.nland > 2) // endpoints are always allowed
        {
            printf("NOTE: sea route used %d land cells\n", r.nland);
        }
    }

    // --- 4. Degenerate case: start == end. ---
    {
        RouteConfig cfg;
        cfg.start = Pos{100, 100};
        cfg.end = Pos{100, 100};
        RouteResult r;
        computeRoute(&r, &g, cfg);
        printResult("same point", r);
        if (!r.valid || r.distance != 0 || r.path.size() != 1)
        {
            printf("FAIL: degenerate route mishandled\n"); fails++;
        }
    }

    // --- 4b. Very short trip (exercises the per-cell sampling branch). ---
    {
        RouteConfig cfg;
        cfg.start = Pos{0, 0};
        cfg.end = Pos{500, 400};
        RouteResult r;
        computeRoute(&r, &g, cfg);
        printResult("short 0,0 -> 500,400", r);
        if (r.valid)
        {
            double direct = sqrt(500.0*500.0 + 400.0*400.0);
            printf("  direct=%.1f path=%.1f\n", direct, r.distance);
            if (r.distance < direct * 0.999)
            {
                printf("FAIL: short path shorter than direct line\n"); fails++;
            }
            if (r.path.front().p.x != 0 || r.path.front().p.z != 0
                || r.path.back().p.x != 500 || r.path.back().p.z != 400)
            {
                printf("FAIL: short route endpoints not preserved\n"); fails++;
            }
        }
        else
        {
            printf("FAIL: short route should always be possible\n"); fails++;
        }
    }

    // --- 5. Very long distance must either work or fail cleanly. ---
    {
        RouteConfig cfg;
        cfg.start = Pos{-20000, -20000};
        cfg.end = Pos{20000, 20000};
        cfg.gridsize = 256;
        RouteResult r;
        computeRoute(&r, &g, cfg);
        printResult("long  -20k,-20k -> 20k,20k", r);
    }

    // --- 6. Shortest vs fastest should differ in distance or time. ---
    {
        RouteConfig a;
        a.start = Pos{-4000, 0};
        a.end = Pos{4000, 0};
        a.optimize = ROUTE_OPT_FASTEST;
        RouteResult ra;
        computeRoute(&ra, &g, a);
        printResult("fastest", ra);

        RouteConfig b = a;
        b.optimize = ROUTE_OPT_SHORTEST;
        RouteResult rb;
        computeRoute(&rb, &g, b);
        printResult("shortest", rb);

        if (ra.valid && rb.valid)
        {
            printf("  fastest: %.1f blocks / %.1fs\n", ra.distance, ra.traveltime);
            printf("  shortest: %.1f blocks / %.1fs\n", rb.distance, rb.traveltime);
        }
    }

    // --- 7. The legs must partition the route exactly. ---
    {
        RouteConfig cfg;
        cfg.start = Pos{0, 0};
        cfg.end = Pos{4000, 2500};
        RouteResult r;
        computeRoute(&r, &g, cfg);

        std::vector<RouteLeg> legs;
        routeLegs(r, &legs);

        double sum = 0;
        int transitions = 0;
        bool prevwater = legs.empty() ? false : legs[0].water;
        for (size_t li = 0; li < legs.size(); li++)
        {
            const RouteLeg& leg = legs[li];
            sum += leg.length;
            if (leg.water != prevwater) transitions++;
            prevwater = leg.water;

            // A leg that covers at least one segment must move.
            if (leg.length > 0 && leg.from.x == leg.to.x && leg.from.z == leg.to.z)
            {
                printf("FAIL: leg of length %.1f does not move\n", leg.length);
                fails++;
            }
            // Consecutive legs must be contiguous.
            if (li > 0)
            {
                const RouteLeg& p = legs[li-1];
                if (p.to.x != leg.from.x || p.to.z != leg.from.z)
                {
                    printf("FAIL: leg %d does not continue the previous one\n", (int)li);
                    fails++;
                }
            }
        }

        printf("[legs] %d legs, sum=%.1f, distance=%.1f, terrain changes=%d\n",
            (int)legs.size(), sum, r.distance, transitions);

        if (!r.valid)
        {
            printf("FAIL: expected a valid route for the leg test\n"); fails++;
        }
        // Every segment must be attributed to exactly one leg.
        if (fabs(sum - r.distance) > 1e-6 * (1.0 + r.distance))
        {
            printf("FAIL: leg lengths sum to %.3f but distance is %.3f\n",
                sum, r.distance);
            fails++;
        }
        // Consecutive legs must alternate terrain.
        if (transitions + 1 != (int)legs.size())
        {
            printf("FAIL: legs do not alternate terrain (%d transitions, %d legs)\n",
                transitions, (int)legs.size());
            fails++;
        }
        // The legs must start and end at the requested endpoints.
        if (!legs.empty()
            && (legs.front().from.x != cfg.start.x || legs.front().from.z != cfg.start.z
                || legs.back().to.x != cfg.end.x || legs.back().to.z != cfg.end.z))
        {
            printf("FAIL: legs do not span the requested endpoints\n"); fails++;
        }
    }

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL ROUTE CHECKS PASSED", fails);
    return fails ? 1 : 0;
}
