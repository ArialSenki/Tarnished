#ifndef VITALS_CORE_H
#define VITALS_CORE_H
#include <math.h>

typedef struct {
    int enabled, modify_maximum, infinite, regenerate;
    double maximum_multiplier, maximum_add, points_per_second, percent_per_second;
} ResourceConfig;

/* Keep rates fractional between ticks; never bank healing at full health. */
static int regenerated_value(int current, int maximum, int alive,
                            const ResourceConfig *c, double seconds, double *fraction) {
    if (!alive || !c->enabled || !c->regenerate || c->infinite || current >= maximum) {
        *fraction = 0; return current;
    }
    if (!isfinite(seconds) || seconds <= 0) return current;
    if (seconds > 0.1) seconds = 0.1;
    double rate = c->points_per_second + maximum * c->percent_per_second / 100.0;
    double amount = *fraction + rate * seconds;
    double room = (double)maximum - current;
    if (amount >= room) { *fraction = 0; return maximum; }
    int whole = (int)floor(amount + 1e-9);
    *fraction = amount - whole;
    if (*fraction < 0) *fraction = 0;
    return current + whole;
}

static int adjusted_maximum(int baseline, const ResourceConfig *c) {
    double v = baseline * c->maximum_multiplier + c->maximum_add;
    if (v < 1) return 1;
    if (v > 1000000) return 1000000;
    return (int)floor(v + 0.5);
}

typedef struct { int initialized, baseline, last_written; } MaximumState;
static int track_maximum(int observed, const ResourceConfig *c, MaximumState *s) {
    if (!s->initialized || observed != s->last_written) {
        s->baseline = observed; s->initialized = 1;
    }
    s->last_written = adjusted_maximum(s->baseline, c);
    return s->last_written;
}

/* The HUD clamps a bar once maximum / visual_limit reaches 1. In fixed mode,
   preserve the bar's initial vanilla fraction of that drawing area while the
   resource maximum changes. A width of 1 is therefore its initial vanilla
   length, rather than the drawing area's absolute maximum. */
static int visual_bar_limit(int effective_maximum, int vanilla_limit,
                            int baseline_maximum, double width, int fixed_width) {
    double reference = vanilla_limit;
    if (fixed_width && baseline_maximum > 0)
        reference = (double)effective_maximum * vanilla_limit / baseline_maximum;
    else if (!fixed_width && effective_maximum > vanilla_limit)
        reference = effective_maximum;
    double limit = reference / width;
    if (limit < 1) return 1;
    if (limit > 1000000) return 1000000;
    return (int)floor(limit + 0.5);
}

/* The matching HUD artwork provides the maximum drawable width.  Keeping the
   limit at or above the real maximum ensures spending or damage changes the
   fill ratio immediately. Width is the desired drawable fraction (0.01..1). */
static int fit_screen_bar_limit(int effective_maximum, double width) {
    if (effective_maximum < 1) return 1;
    if (effective_maximum > 1000000) effective_maximum = 1000000;
    if (!isfinite(width) || width <= 0.0) return 1000000;
    double limit = ceil((double)effective_maximum / width);
    if (limit > 1000000) return 1000000;
    if (limit < effective_maximum) return effective_maximum;
    return (int)limit;
}
#endif
