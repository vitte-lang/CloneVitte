/*
 * Vitte Compiler
 * src/arena/stats.c
 *
 * Memory statistics and observation layer.
 *
 * ISO C17.
 *
 * Architecture:
 *
 *     block.c/.h
 *         physical arena blocks
 *
 *     allocator.c/.h
 *         low-level monotonic allocator
 *
 *     arena.c/.h
 *         compiler-facing arena context
 *
 *     checkpoint.c/.h
 *         transactional checkpoints
 *
 *     pool.c/.h
 *         reusable fixed-size pools
 *
 *     reset.c/.h
 *         coordinated reset policy
 *
 *     stats.c/.h
 *         observation, aggregation and deltas
 *
 * Design:
 *
 *     - no allocation
 *     - no mutation of observed allocators
 *     - deterministic snapshots
 *     - overflow-safe aggregation
 *     - saturating aggregate counters
 *     - arena statistics
 *     - pool statistics
 *     - aggregate pool statistics
 *     - global memory statistics
 *     - before/after deltas
 *     - high-water information
 *     - utilization ratios
 *     - padding/overhead information
 *     - structural validation
 *
 * Ratios are expressed in basis points:
 *
 *         0 =   0.00%
 *      5000 =  50.00%
 *     10000 = 100.00%
 */

#include "stats.h"

#include "arena.h"
#include "pool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

#ifndef VITTE_STATS_BASIS_POINTS
#define VITTE_STATS_BASIS_POINTS UINT32_C(10000)
#endif

/* ========================================================================= */
/* Compile-time checks                                                       */
/* ========================================================================= */

#if defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 201112L

_Static_assert(
    VITTE_STATS_BASIS_POINTS != 0u,
    "statistics basis-point scale must not be zero");

_Static_assert(
    VITTE_STATS_BASIS_POINTS <= UINT32_MAX,
    "statistics basis-point scale must fit uint32_t");

_Static_assert(
    PTRDIFF_MAX > 0,
    "ptrdiff_t must have a positive range");

#endif

/* ========================================================================= */
/* Arithmetic                                                                */
/* ========================================================================= */

static bool
vitte_stats_add_overflow(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (left > SIZE_MAX - right) {
        return true;
    }

    *result = left + right;
    return false;
}

static bool
vitte_stats_mul_overflow(
    size_t left,
    size_t right,
    size_t *result)
{
    if (result == NULL) {
        return true;
    }

    if (left != 0u &&
        right > SIZE_MAX / left) {
        return true;
    }

    *result = left * right;
    return false;
}

static size_t
vitte_stats_saturating_add(
    size_t left,
    size_t right,
    bool *saturated)
{
    size_t result;

    if (vitte_stats_add_overflow(
            left,
            right,
            &result)) {
        if (saturated != NULL) {
            *saturated = true;
        }

        return SIZE_MAX;
    }

    return result;
}

static size_t
vitte_stats_saturating_mul(
    size_t left,
    size_t right,
    bool *saturated)
{
    size_t result;

    if (vitte_stats_mul_overflow(
            left,
            right,
            &result)) {
        if (saturated != NULL) {
            *saturated = true;
        }

        return SIZE_MAX;
    }

    return result;
}

static size_t
vitte_stats_nonnegative_difference(
    size_t left,
    size_t right)
{
    if (left <= right) {
        return 0u;
    }

    return left - right;
}

static ptrdiff_t
vitte_stats_signed_delta(
    size_t before,
    size_t after,
    bool *saturated)
{
    size_t difference;

    if (after >= before) {
        difference = after - before;

        if (difference >
            (size_t)PTRDIFF_MAX) {
            if (saturated != NULL) {
                *saturated = true;
            }

            return PTRDIFF_MAX;
        }

        return (ptrdiff_t)difference;
    }

    difference = before - after;

    /*
     * PTRDIFF_MIN can have one additional unit of magnitude compared with
     * PTRDIFF_MAX. Converting that magnitude to ptrdiff_t directly would be
     * implementation-defined, so saturate at PTRDIFF_MIN whenever the
     * negative difference cannot be represented through the positive range.
     */
    if (difference >
        (size_t)PTRDIFF_MAX) {
        if (saturated != NULL) {
            *saturated = true;
        }

        return PTRDIFF_MIN;
    }

    return -(ptrdiff_t)difference;
}

/* ========================================================================= */
/* Overflow-safe ratio                                                       */
/* ========================================================================= */

/*
 * Calculate:
 *
 *     floor(value * scale / total)
 *
 * exactly, without forming value * scale.
 *
 * This routine is designed for ratios where:
 *
 *     value < total
 *
 * and scale is small. In this module scale is the basis-point constant.
 *
 * Algorithm:
 *
 *     Generate the fractional result using base-10-style long division, but
 *     in one generic scale-sized step:
 *
 *         result = floor(value * scale / total)
 *
 *     Instead of multiplying value by scale, binary-search the result k in
 *     [0, scale - 1] and test:
 *
 *         k * total <= value * scale
 *
 *     through the equivalent overflow-safe comparison:
 *
 *         ceil(k * total / scale) <= value
 *
 *     k <= scale, so total is decomposed first:
 *
 *         total = q * scale + r
 *
 *         ceil(k * total / scale)
 *           = k*q + ceil(k*r/scale)
 *
 *     Since r < scale and k <= scale, k*r is bounded by scale².
 *
 * For the default scale 10000 this is trivially representable on every
 * supported Vitte target. The implementation nevertheless avoids relying on
 * that property where possible.
 */
static size_t
vitte_stats_mul_div_fraction_floor(
    size_t value,
    size_t scale,
    size_t total)
{
    size_t low;
    size_t high;
    size_t total_q;
    size_t total_r;

    if (value == 0u ||
        scale == 0u ||
        total == 0u) {
        return 0u;
    }

    if (value >= total) {
        return scale;
    }

    /*
     * Fast exact path.
     */
    if (value <= SIZE_MAX / scale) {
        return
            (value * scale) /
            total;
    }

    total_q = total / scale;
    total_r = total % scale;

    low = 0u;
    high = scale - 1u;

    while (low < high) {
        size_t middle;
        size_t qpart;
        size_t rpart;
        size_t threshold = 0u;
        bool threshold_overflow;

        middle =
            low +
            (high - low + 1u) / 2u;

        threshold_overflow = false;

        /*
         * qpart = middle * floor(total / scale)
         *
         * middle <= scale and the mathematical threshold never needs to
         * exceed total. Still guard the machine multiplication explicitly.
         */
        if (middle != 0u &&
            total_q > SIZE_MAX / middle) {
            threshold_overflow = true;
            qpart = SIZE_MAX;
        } else {
            qpart = middle * total_q;
        }

        /*
         * rpart = middle * (total % scale)
         *
         * Normally this is bounded by scale². Handle arbitrary compile-time
         * scales defensively.
         */
        if (!threshold_overflow &&
            middle != 0u &&
            total_r > SIZE_MAX / middle) {
            /*
             * Generic fallback for an unusually large configured scale.
             *
             * Compute ceil(middle * total_r / scale) by repeated quotient
             * decomposition. This branch is irrelevant for the default
             * basis-point scale but preserves correctness.
             */
            size_t rq;
            size_t rr;
            size_t fraction;

            rq = total_r / scale;
            rr = total_r % scale;

            /*
             * total_r < scale, therefore rq is zero. Keep the generic form
             * explicit.
             */
            fraction = rq * middle;

            if (rr != 0u) {
                size_t f_low;
                size_t f_high;

                f_low = 0u;
                f_high = middle;

                while (f_low < f_high) {
                    size_t f_mid;
                    size_t lhs_q;
                    size_t lhs_r;
                    size_t ceil_value;

                    f_mid =
                        f_low +
                        (f_high - f_low) / 2u;

                    lhs_q = scale / middle;
                    lhs_r = scale % middle;

                    if (f_mid != 0u &&
                        lhs_q > SIZE_MAX / f_mid) {
                        ceil_value = SIZE_MAX;
                    } else {
                        size_t part;

                        ceil_value =
                            lhs_q * f_mid;

                        if (lhs_r != 0u) {
                            if (lhs_r >
                                SIZE_MAX / f_mid) {
                                ceil_value =
                                    SIZE_MAX;
                            } else {
                                part =
                                    lhs_r * f_mid;

                                if (ceil_value <=
                                    SIZE_MAX -
                                    part / middle) {
                                    ceil_value +=
                                        part / middle;

                                    if ((part % middle) != 0u &&
                                        ceil_value != SIZE_MAX) {
                                        ++ceil_value;
                                    }
                                } else {
                                    ceil_value =
                                        SIZE_MAX;
                                }
                            }
                        }
                    }

                    if (ceil_value >= rr) {
                        f_high = f_mid;
                    } else {
                        f_low = f_mid + 1u;
                    }
                }

                if (fraction >
                    SIZE_MAX - f_low) {
                    threshold_overflow = true;
                } else {
                    fraction += f_low;
                }
            }

            if (!threshold_overflow) {
                if (qpart >
                    SIZE_MAX - fraction) {
                    threshold_overflow = true;
                } else {
                    threshold =
                        qpart + fraction;
                }
            }
        } else if (!threshold_overflow) {
            size_t fraction;

            rpart =
                middle * total_r;

            fraction =
                rpart / scale;

            if ((rpart % scale) != 0u) {
                ++fraction;
            }

            if (qpart >
                SIZE_MAX - fraction) {
                threshold_overflow = true;
            } else {
                threshold =
                    qpart + fraction;
            }
        }

        if (!threshold_overflow &&
            threshold <= value) {
            low = middle;
        } else {
            high = middle - 1u;
        }
    }

    return low;
}

/*
 * Ratio in basis points.
 *
 * value >= total is deliberately clamped to 10000. This makes the helper
 * robust when called with saturated or externally supplied observations.
 */
static uint32_t
vitte_stats_ratio_basis_points(
    size_t value,
    size_t total)
{
    size_t result;

    if (value == 0u ||
        total == 0u) {
        return UINT32_C(0);
    }

    if (value >= total) {
        return VITTE_STATS_BASIS_POINTS;
    }

    result =
        vitte_stats_mul_div_fraction_floor(
            value,
            (size_t)VITTE_STATS_BASIS_POINTS,
            total);

    if (result >
        (size_t)VITTE_STATS_BASIS_POINTS) {
        result =
            (size_t)VITTE_STATS_BASIS_POINTS;
    }

    return (uint32_t)result;
}

/* ========================================================================= */
/* Internal consistency helpers                                              */
/* ========================================================================= */

static bool
vitte_stats_power_of_two(
    size_t value)
{
    return
        value != 0u &&
        (value & (value - 1u)) == 0u;
}

static bool
vitte_stats_ratio_valid(
    uint32_t value)
{
    return
        value <=
        VITTE_STATS_BASIS_POINTS;
}

static bool
vitte_stats_snapshot_shapes_match(
    const vitte_stats_snapshot_t *before,
    const vitte_stats_snapshot_t *after)
{
    if (before == NULL ||
        after == NULL) {
        return false;
    }

    if (before->has_arena !=
        after->has_arena) {
        return false;
    }

    if (before->has_pools !=
        after->has_pools) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Initialization                                                            */
/* ========================================================================= */

void
vitte_stats_snapshot_init(
    vitte_stats_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(
        snapshot,
        0,
        sizeof(*snapshot));

    snapshot->valid = false;
    snapshot->has_arena = false;
    snapshot->has_pools = false;
    snapshot->saturated = false;
}

void
vitte_stats_delta_init(
    vitte_stats_delta_t *delta)
{
    if (delta == NULL) {
        return;
    }

    memset(
        delta,
        0,
        sizeof(*delta));

    delta->valid = false;
    delta->saturated = false;
}

/* ========================================================================= */
/* Arena capture                                                             */
/* ========================================================================= */

bool
vitte_stats_capture_arena(
    const vitte_arena_context_t *arena,
    vitte_stats_arena_t *stats)
{
    vitte_arena_context_stats_t source;

    if (stats == NULL) {
        return false;
    }

    memset(
        stats,
        0,
        sizeof(*stats));

    if (arena == NULL ||
        !vitte_arena_context_is_valid(
            arena)) {
        return false;
    }

    source =
        vitte_arena_context_stats(
            arena);

    stats->block_size =
        source.block_size;

    stats->block_count =
        source.block_count;

    stats->peak_block_count =
        source.peak_block_count;

    stats->reserved_bytes =
        source.reserved_bytes;

    stats->peak_reserved_bytes =
        source.peak_reserved_bytes;

    stats->used_bytes =
        source.used_bytes;

    stats->peak_used_bytes =
        source.peak_used_bytes;

    stats->requested_bytes =
        source.requested_bytes;

    stats->unused_bytes =
        source.unused_bytes;

    stats->padding_bytes =
        source.padding_bytes;

    stats->allocation_count =
        source.allocation_count;

    stats->generation =
        source.generation;

    stats->utilization_basis_points =
        vitte_stats_ratio_basis_points(
            stats->used_bytes,
            stats->reserved_bytes);

    stats->requested_utilization_basis_points =
        vitte_stats_ratio_basis_points(
            stats->requested_bytes,
            stats->reserved_bytes);

    stats->padding_basis_points =
        vitte_stats_ratio_basis_points(
            stats->padding_bytes,
            stats->used_bytes);

    /*
     * Publish valid only after the complete snapshot has been populated.
     *
     * Structural validation remains a separate operation because capture is
     * intentionally observational and does not force an expensive deep arena
     * validation.
     */
    stats->valid = true;

    return true;
}

/* ========================================================================= */
/* Pool capture                                                              */
/* ========================================================================= */

bool
vitte_stats_capture_pool(
    const vitte_pool_t *pool,
    vitte_stats_pool_t *stats)
{
    vitte_pool_stats_t source;
    size_t live_object_bytes;
    size_t slot_storage_bytes;
    size_t slot_padding_bytes;
    bool saturated;

    if (stats == NULL) {
        return false;
    }

    memset(
        stats,
        0,
        sizeof(*stats));

    if (pool == NULL ||
        !vitte_pool_is_valid(pool)) {
        return false;
    }

    source =
        vitte_pool_stats(pool);

    stats->object_size =
        source.object_size;

    stats->slot_size =
        source.slot_size;

    stats->alignment =
        source.alignment;

    stats->page_count =
        source.page_count;

    stats->peak_page_count =
        source.peak_page_count;

    stats->capacity =
        source.capacity;

    stats->live_count =
        source.live_count;

    stats->peak_live_count =
        source.peak_live_count;

    stats->free_count =
        source.free_count;

    stats->reserved_bytes =
        source.reserved_bytes;

    stats->peak_reserved_bytes =
        source.peak_reserved_bytes;

    stats->allocation_count =
        source.allocation_count;

    stats->free_operation_count =
        source.free_operation_count;

    stats->generation =
        source.generation;

    stats->max_objects =
        source.max_objects;

    stats->max_reserved_bytes =
        source.max_reserved_bytes;

    saturated = false;

    live_object_bytes =
        vitte_stats_saturating_mul(
            stats->live_count,
            stats->object_size,
            &saturated);

    slot_storage_bytes =
        vitte_stats_saturating_mul(
            stats->capacity,
            stats->slot_size,
            &saturated);

    slot_padding_bytes = 0u;

    if (stats->slot_size >=
        stats->object_size) {
        slot_padding_bytes =
            vitte_stats_saturating_mul(
                stats->capacity,
                stats->slot_size -
                    stats->object_size,
                &saturated);
    }

    stats->live_object_bytes =
        live_object_bytes;

    stats->slot_storage_bytes =
        slot_storage_bytes;

    stats->slot_padding_bytes =
        slot_padding_bytes;

    /*
     * reserved_bytes includes page headers, bitmap storage, page-level
     * alignment padding and slot storage.
     *
     * If slot_storage_bytes saturated, subtraction is no longer exact. Keep
     * metadata at zero and advertise saturation rather than manufacturing a
     * misleading metadata quantity.
     */
    if (!saturated &&
        stats->reserved_bytes >=
            stats->slot_storage_bytes) {
        stats->metadata_bytes =
            stats->reserved_bytes -
            stats->slot_storage_bytes;
    } else {
        stats->metadata_bytes = 0u;

        if (stats->slot_storage_bytes >
            stats->reserved_bytes) {
            saturated = true;
        }
    }

    stats->utilization_basis_points =
        vitte_stats_ratio_basis_points(
            stats->live_count,
            stats->capacity);

    /*
     * live_object_bytes represents logical user payload. It can never exceed
     * reserved bytes in a non-saturated valid pool.
     */
    stats->live_bytes_basis_points =
        vitte_stats_ratio_basis_points(
            stats->live_object_bytes,
            stats->reserved_bytes);

    stats->metadata_basis_points =
        vitte_stats_ratio_basis_points(
            stats->metadata_bytes,
            stats->reserved_bytes);

    stats->saturated =
        saturated;

    stats->valid = true;

    return true;
}

/* ========================================================================= */
/* Pool aggregation                                                          */
/* ========================================================================= */

bool
vitte_stats_capture_pools(
    vitte_pool_t *const *pools,
    size_t pool_count,
    vitte_stats_pools_t *stats)
{
    size_t index;
    bool saturated;

    if (stats == NULL) {
        return false;
    }

    memset(
        stats,
        0,
        sizeof(*stats));

    stats->pool_count =
        pool_count;

    if (pool_count != 0u &&
        pools == NULL) {
        stats->invalid_pool_count =
            pool_count;

        stats->valid = false;
        return false;
    }

    saturated = false;

    for (index = 0u;
         index < pool_count;
         ++index) {
        vitte_stats_pool_t current;

        memset(
            &current,
            0,
            sizeof(current));

        if (pools[index] == NULL ||
            !vitte_stats_capture_pool(
                pools[index],
                &current)) {
            if (stats->invalid_pool_count !=
                SIZE_MAX) {
                ++stats->invalid_pool_count;
            } else {
                saturated = true;
            }

            continue;
        }

        if (stats->valid_pool_count !=
            SIZE_MAX) {
            ++stats->valid_pool_count;
        } else {
            saturated = true;
        }

#define VITTE_STATS_ACCUMULATE(field)                       \
        do {                                                \
            stats->field =                                  \
                vitte_stats_saturating_add(                 \
                    stats->field,                           \
                    current.field,                          \
                    &saturated);                            \
        } while (0)

        VITTE_STATS_ACCUMULATE(page_count);
        VITTE_STATS_ACCUMULATE(peak_page_count);

        VITTE_STATS_ACCUMULATE(capacity);

        VITTE_STATS_ACCUMULATE(live_count);
        VITTE_STATS_ACCUMULATE(peak_live_count);
        VITTE_STATS_ACCUMULATE(free_count);

        VITTE_STATS_ACCUMULATE(reserved_bytes);
        VITTE_STATS_ACCUMULATE(peak_reserved_bytes);

        VITTE_STATS_ACCUMULATE(live_object_bytes);
        VITTE_STATS_ACCUMULATE(slot_storage_bytes);
        VITTE_STATS_ACCUMULATE(slot_padding_bytes);
        VITTE_STATS_ACCUMULATE(metadata_bytes);

        VITTE_STATS_ACCUMULATE(allocation_count);
        VITTE_STATS_ACCUMULATE(free_operation_count);

#undef VITTE_STATS_ACCUMULATE

        if (current.saturated) {
            saturated = true;
        }
    }

    stats->utilization_basis_points =
        vitte_stats_ratio_basis_points(
            stats->live_count,
            stats->capacity);

    stats->live_bytes_basis_points =
        vitte_stats_ratio_basis_points(
            stats->live_object_bytes,
            stats->reserved_bytes);

    stats->metadata_basis_points =
        vitte_stats_ratio_basis_points(
            stats->metadata_bytes,
            stats->reserved_bytes);

    stats->saturated =
        saturated;

    /*
     * Saturation does not make a snapshot structurally invalid. It means one
     * or more aggregate counters are lower-information saturated values.
     *
     * Invalid targets, however, make the aggregate incomplete and therefore
     * invalid for exact snapshot/delta operations.
     */
    stats->valid =
        stats->invalid_pool_count == 0u;

    return stats->valid;
}

/* ========================================================================= */
/* Snapshot capture                                                          */
/* ========================================================================= */

bool
vitte_stats_capture(
    const vitte_arena_context_t *arena,
    vitte_pool_t *const *pools,
    size_t pool_count,
    vitte_stats_snapshot_t *snapshot)
{
    bool arena_ok;
    bool pools_ok;
    bool saturated;

    if (snapshot == NULL) {
        return false;
    }

    vitte_stats_snapshot_init(
        snapshot);

    if (arena == NULL &&
        pool_count == 0u) {
        return false;
    }

    if (pool_count != 0u &&
        pools == NULL) {
        return false;
    }

    arena_ok = true;
    pools_ok = true;

    if (arena != NULL) {
        arena_ok =
            vitte_stats_capture_arena(
                arena,
                &snapshot->arena);

        snapshot->has_arena =
            arena_ok;
    }

    if (pool_count != 0u) {
        /*
         * has_pools describes requested snapshot shape, not successful
         * aggregate validity. This distinction allows callers to diagnose an
         * invalid pool aggregate while preserving what was requested.
         */
        snapshot->has_pools = true;

        pools_ok =
            vitte_stats_capture_pools(
                pools,
                pool_count,
                &snapshot->pools);
    }

    saturated = false;

    snapshot->total_reserved_bytes =
        vitte_stats_saturating_add(
            snapshot->has_arena
                ? snapshot->arena.reserved_bytes
                : 0u,
            snapshot->has_pools
                ? snapshot->pools.reserved_bytes
                : 0u,
            &saturated);

    /*
     * "live bytes" deliberately combines:
     *
     *     arena physical used bytes
     *     pool logical live object payload bytes
     *
     * It therefore describes memory associated with currently live logical
     * state, not raw committed slot storage.
     */
    snapshot->total_live_bytes =
        vitte_stats_saturating_add(
            snapshot->has_arena
                ? snapshot->arena.used_bytes
                : 0u,
            snapshot->has_pools
                ? snapshot->pools.live_object_bytes
                : 0u,
            &saturated);

    /*
     * For the arena, requested_bytes is logical requested payload.
     *
     * Pools do not expose a distinct current requested-byte counter because
     * every live object has fixed object_size. Therefore live_object_bytes is
     * the corresponding logical requested payload.
     */
    snapshot->total_requested_bytes =
        vitte_stats_saturating_add(
            snapshot->has_arena
                ? snapshot->arena.requested_bytes
                : 0u,
            snapshot->has_pools
                ? snapshot->pools.live_object_bytes
                : 0u,
            &saturated);

    /*
     * This is net reserved storage not represented by total_live_bytes.
     *
     * When an aggregate has saturated, the exact difference may no longer be
     * recoverable. The saturated flag communicates that loss of precision.
     */
    snapshot->total_unused_bytes =
        vitte_stats_nonnegative_difference(
            snapshot->total_reserved_bytes,
            snapshot->total_live_bytes);

    /*
     * Overhead:
     *
     *     arena alignment/physical overhead
     *   + pool page/bitmap/alignment metadata
     *   + pool per-slot internal padding
     *
     * Free slot payload capacity is intentionally not counted as overhead;
     * it is reusable reserved capacity.
     */
    snapshot->total_overhead_bytes =
        vitte_stats_saturating_add(
            snapshot->has_arena
                ? snapshot->arena.padding_bytes
                : 0u,
            snapshot->has_pools
                ? snapshot->pools.metadata_bytes
                : 0u,
            &saturated);

    snapshot->total_overhead_bytes =
        vitte_stats_saturating_add(
            snapshot->total_overhead_bytes,
            snapshot->has_pools
                ? snapshot->pools.slot_padding_bytes
                : 0u,
            &saturated);

    /*
     * Pool allocation_count is lifetime successful allocation operations,
     * while arena allocation_count is the current logical arena allocation
     * count according to arena.c.
     *
     * The aggregate is therefore an operation-oriented observation, not a
     * current live-object count. Keep this semantic distinction explicit.
     */
    snapshot->total_allocation_count =
        vitte_stats_saturating_add(
            snapshot->has_arena
                ? snapshot->arena.allocation_count
                : 0u,
            snapshot->has_pools
                ? snapshot->pools.allocation_count
                : 0u,
            &saturated);

    snapshot->utilization_basis_points =
        vitte_stats_ratio_basis_points(
            snapshot->total_live_bytes,
            snapshot->total_reserved_bytes);

    snapshot->overhead_basis_points =
        vitte_stats_ratio_basis_points(
            snapshot->total_overhead_bytes,
            snapshot->total_reserved_bytes);

    snapshot->saturated =
        saturated ||
        (snapshot->has_pools &&
         snapshot->pools.saturated);

    snapshot->valid =
        arena_ok &&
        pools_ok &&
        (snapshot->has_arena ||
         snapshot->has_pools);

    return snapshot->valid;
}

/* ========================================================================= */
/* Delta                                                                     */
/* ========================================================================= */

bool
vitte_stats_delta(
    const vitte_stats_snapshot_t *before,
    const vitte_stats_snapshot_t *after,
    vitte_stats_delta_t *delta)
{
    bool saturated;

    if (delta == NULL) {
        return false;
    }

    vitte_stats_delta_init(
        delta);

    if (before == NULL ||
        after == NULL ||
        !before->valid ||
        !after->valid) {
        return false;
    }

    if (!vitte_stats_snapshot_shapes_match(
            before,
            after)) {
        return false;
    }

    saturated = false;

#define VITTE_STATS_DELTA_FIELD(field)                      \
    do {                                                    \
        delta->field =                                      \
            vitte_stats_signed_delta(                       \
                before->field,                              \
                after->field,                               \
                &saturated);                                \
    } while (0)

    VITTE_STATS_DELTA_FIELD(
        total_reserved_bytes);

    VITTE_STATS_DELTA_FIELD(
        total_live_bytes);

    VITTE_STATS_DELTA_FIELD(
        total_requested_bytes);

    VITTE_STATS_DELTA_FIELD(
        total_unused_bytes);

    VITTE_STATS_DELTA_FIELD(
        total_overhead_bytes);

    VITTE_STATS_DELTA_FIELD(
        total_allocation_count);

#undef VITTE_STATS_DELTA_FIELD

    if (before->has_arena) {
#define VITTE_STATS_ARENA_DELTA(field)                      \
        do {                                                \
            delta->arena_##field =                          \
                vitte_stats_signed_delta(                   \
                    before->arena.field,                    \
                    after->arena.field,                     \
                    &saturated);                            \
        } while (0)

        VITTE_STATS_ARENA_DELTA(
            block_count);

        VITTE_STATS_ARENA_DELTA(
            reserved_bytes);

        VITTE_STATS_ARENA_DELTA(
            used_bytes);

        VITTE_STATS_ARENA_DELTA(
            requested_bytes);

        VITTE_STATS_ARENA_DELTA(
            unused_bytes);

        VITTE_STATS_ARENA_DELTA(
            padding_bytes);

        VITTE_STATS_ARENA_DELTA(
            allocation_count);

#undef VITTE_STATS_ARENA_DELTA
    }

    if (before->has_pools) {
#define VITTE_STATS_POOLS_DELTA(field)                      \
        do {                                                \
            delta->pools_##field =                          \
                vitte_stats_signed_delta(                   \
                    before->pools.field,                    \
                    after->pools.field,                     \
                    &saturated);                            \
        } while (0)

        VITTE_STATS_POOLS_DELTA(
            page_count);

        VITTE_STATS_POOLS_DELTA(
            capacity);

        VITTE_STATS_POOLS_DELTA(
            live_count);

        VITTE_STATS_POOLS_DELTA(
            free_count);

        VITTE_STATS_POOLS_DELTA(
            reserved_bytes);

        VITTE_STATS_POOLS_DELTA(
            live_object_bytes);

        VITTE_STATS_POOLS_DELTA(
            metadata_bytes);

        VITTE_STATS_POOLS_DELTA(
            allocation_count);

        VITTE_STATS_POOLS_DELTA(
            free_operation_count);

#undef VITTE_STATS_POOLS_DELTA
    }

    delta->utilization_before_basis_points =
        before->utilization_basis_points;

    delta->utilization_after_basis_points =
        after->utilization_basis_points;

    delta->overhead_before_basis_points =
        before->overhead_basis_points;

    delta->overhead_after_basis_points =
        after->overhead_basis_points;

    /*
     * A delta derived from an already-saturated endpoint cannot be exact even
     * if every signed subtraction itself fits ptrdiff_t.
     */
    delta->saturated =
        saturated ||
        before->saturated ||
        after->saturated;

    delta->valid = true;

    return true;
}

/* ========================================================================= */
/* Released / increased helpers                                              */
/* ========================================================================= */

size_t
vitte_stats_reserved_released(
    const vitte_stats_snapshot_t *before,
    const vitte_stats_snapshot_t *after)
{
    if (before == NULL ||
        after == NULL ||
        !before->valid ||
        !after->valid ||
        !vitte_stats_snapshot_shapes_match(
            before,
            after)) {
        return 0u;
    }

    return
        vitte_stats_nonnegative_difference(
            before->total_reserved_bytes,
            after->total_reserved_bytes);
}

size_t
vitte_stats_reserved_increased(
    const vitte_stats_snapshot_t *before,
    const vitte_stats_snapshot_t *after)
{
    if (before == NULL ||
        after == NULL ||
        !before->valid ||
        !after->valid ||
        !vitte_stats_snapshot_shapes_match(
            before,
            after)) {
        return 0u;
    }

    return
        vitte_stats_nonnegative_difference(
            after->total_reserved_bytes,
            before->total_reserved_bytes);
}

size_t
vitte_stats_live_released(
    const vitte_stats_snapshot_t *before,
    const vitte_stats_snapshot_t *after)
{
    if (before == NULL ||
        after == NULL ||
        !before->valid ||
        !after->valid ||
        !vitte_stats_snapshot_shapes_match(
            before,
            after)) {
        return 0u;
    }

    return
        vitte_stats_nonnegative_difference(
            before->total_live_bytes,
            after->total_live_bytes);
}

/* ========================================================================= */
/* Comparison                                                                */
/* ========================================================================= */

int
vitte_stats_compare_reserved(
    const vitte_stats_snapshot_t *left,
    const vitte_stats_snapshot_t *right)
{
    if (left == right) {
        return 0;
    }

    if (left == NULL) {
        return -1;
    }

    if (right == NULL) {
        return 1;
    }

    if (left->total_reserved_bytes <
        right->total_reserved_bytes) {
        return -1;
    }

    if (left->total_reserved_bytes >
        right->total_reserved_bytes) {
        return 1;
    }

    return 0;
}

int
vitte_stats_compare_live(
    const vitte_stats_snapshot_t *left,
    const vitte_stats_snapshot_t *right)
{
    if (left == right) {
        return 0;
    }

    if (left == NULL) {
        return -1;
    }

    if (right == NULL) {
        return 1;
    }

    if (left->total_live_bytes <
        right->total_live_bytes) {
        return -1;
    }

    if (left->total_live_bytes >
        right->total_live_bytes) {
        return 1;
    }

    return 0;
}

/* ========================================================================= */
/* Arena validation                                                          */
/* ========================================================================= */

bool
vitte_stats_validate_arena(
    const vitte_stats_arena_t *stats)
{
    if (stats == NULL ||
        !stats->valid) {
        return false;
    }

    if (stats->generation == UINT64_C(0)) {
        return false;
    }

    if (stats->used_bytes >
        stats->reserved_bytes) {
        return false;
    }

    /*
     * High-level arena requested_bytes is expected to describe the current
     * logical requested payload. Zero-byte allocations may consume physical
     * bytes while contributing zero requested bytes, so <= is correct.
     *
     * This invariant intentionally belongs at the arena-context layer. Raw
     * block-local rewind accounting must not be validated with the same rule
     * unless block accounting is restored transactionally.
     */
    if (stats->requested_bytes >
        stats->used_bytes) {
        return false;
    }

    if (stats->unused_bytes !=
        stats->reserved_bytes -
            stats->used_bytes) {
        return false;
    }

    /*
     * padding_bytes may include alignment overhead and physical bytes used to
     * represent zero-byte allocations. It remains physical used overhead and
     * therefore cannot exceed used_bytes.
     */
    if (stats->padding_bytes >
        stats->used_bytes) {
        return false;
    }

    if (stats->peak_block_count <
        stats->block_count) {
        return false;
    }

    if (stats->peak_reserved_bytes <
        stats->reserved_bytes) {
        return false;
    }

    if (stats->peak_used_bytes <
        stats->used_bytes) {
        return false;
    }

    if (stats->block_count == 0u) {
        if (stats->reserved_bytes != 0u ||
            stats->used_bytes != 0u) {
            return false;
        }
    }

    if (stats->reserved_bytes == 0u &&
        stats->used_bytes != 0u) {
        return false;
    }

    if (!vitte_stats_ratio_valid(
            stats->utilization_basis_points) ||
        !vitte_stats_ratio_valid(
            stats->requested_utilization_basis_points) ||
        !vitte_stats_ratio_valid(
            stats->padding_basis_points)) {
        return false;
    }

    if (stats->utilization_basis_points !=
        vitte_stats_ratio_basis_points(
            stats->used_bytes,
            stats->reserved_bytes)) {
        return false;
    }

    if (stats->requested_utilization_basis_points !=
        vitte_stats_ratio_basis_points(
            stats->requested_bytes,
            stats->reserved_bytes)) {
        return false;
    }

    if (stats->padding_basis_points !=
        vitte_stats_ratio_basis_points(
            stats->padding_bytes,
            stats->used_bytes)) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Pool validation                                                           */
/* ========================================================================= */

bool
vitte_stats_validate_pool(
    const vitte_stats_pool_t *stats)
{
    size_t expected;
    size_t per_slot_padding;

    if (stats == NULL ||
        !stats->valid) {
        return false;
    }

    if (stats->object_size == 0u ||
        stats->slot_size == 0u ||
        stats->alignment == 0u) {
        return false;
    }

    if (!vitte_stats_power_of_two(
            stats->alignment)) {
        return false;
    }

    if (stats->slot_size <
        stats->object_size) {
        return false;
    }

    if ((stats->slot_size %
         stats->alignment) != 0u) {
        return false;
    }

    if (stats->live_count >
            stats->capacity ||
        stats->free_count >
            stats->capacity) {
        return false;
    }

    if (stats->capacity -
            stats->live_count !=
        stats->free_count) {
        return false;
    }

    if (stats->peak_live_count <
        stats->live_count) {
        return false;
    }

    if (stats->peak_page_count <
        stats->page_count) {
        return false;
    }

    if (stats->peak_reserved_bytes <
        stats->reserved_bytes) {
        return false;
    }

    if (stats->page_count == 0u) {
        if (stats->capacity != 0u ||
            stats->live_count != 0u ||
            stats->free_count != 0u ||
            stats->reserved_bytes != 0u) {
            return false;
        }
    }

    if (stats->capacity == 0u &&
        (stats->live_count != 0u ||
         stats->free_count != 0u)) {
        return false;
    }

    if (stats->max_objects != SIZE_MAX &&
        stats->capacity >
            stats->max_objects) {
        return false;
    }

    if (stats->max_reserved_bytes !=
            SIZE_MAX &&
        stats->reserved_bytes >
            stats->max_reserved_bytes) {
        return false;
    }

    if (!stats->saturated) {
        if (vitte_stats_mul_overflow(
                stats->live_count,
                stats->object_size,
                &expected)) {
            return false;
        }

        if (stats->live_object_bytes !=
            expected) {
            return false;
        }

        if (vitte_stats_mul_overflow(
                stats->capacity,
                stats->slot_size,
                &expected)) {
            return false;
        }

        if (stats->slot_storage_bytes !=
            expected) {
            return false;
        }

        if (stats->slot_storage_bytes >
            stats->reserved_bytes) {
            return false;
        }

        if (stats->metadata_bytes !=
            stats->reserved_bytes -
                stats->slot_storage_bytes) {
            return false;
        }

        per_slot_padding =
            stats->slot_size -
            stats->object_size;

        if (vitte_stats_mul_overflow(
                stats->capacity,
                per_slot_padding,
                &expected)) {
            return false;
        }

        if (stats->slot_padding_bytes !=
            expected) {
            return false;
        }

        if (stats->live_object_bytes >
            stats->slot_storage_bytes) {
            return false;
        }
    }

    if (!vitte_stats_ratio_valid(
            stats->utilization_basis_points) ||
        !vitte_stats_ratio_valid(
            stats->live_bytes_basis_points) ||
        !vitte_stats_ratio_valid(
            stats->metadata_basis_points)) {
        return false;
    }

    if (!stats->saturated) {
        if (stats->utilization_basis_points !=
            vitte_stats_ratio_basis_points(
                stats->live_count,
                stats->capacity)) {
            return false;
        }

        if (stats->live_bytes_basis_points !=
            vitte_stats_ratio_basis_points(
                stats->live_object_bytes,
                stats->reserved_bytes)) {
            return false;
        }

        if (stats->metadata_basis_points !=
            vitte_stats_ratio_basis_points(
                stats->metadata_bytes,
                stats->reserved_bytes)) {
            return false;
        }
    }

    return true;
}

/* ========================================================================= */
/* Aggregate pool validation                                                 */
/* ========================================================================= */

bool
vitte_stats_validate_pools(
    const vitte_stats_pools_t *stats)
{
    size_t classified;

    if (stats == NULL) {
        return false;
    }

    if (stats->valid &&
        stats->invalid_pool_count != 0u) {
        return false;
    }

    if (!stats->valid &&
        stats->invalid_pool_count == 0u) {
        return false;
    }

    if (stats->valid_pool_count >
            stats->pool_count ||
        stats->invalid_pool_count >
            stats->pool_count) {
        return false;
    }

    if (vitte_stats_add_overflow(
            stats->valid_pool_count,
            stats->invalid_pool_count,
            &classified)) {
        return false;
    }

    if (classified !=
        stats->pool_count) {
        return false;
    }

    if (!stats->saturated) {
        if (stats->live_count >
                stats->capacity ||
            stats->free_count >
                stats->capacity) {
            return false;
        }

        if (stats->capacity -
                stats->live_count !=
            stats->free_count) {
            return false;
        }

        if (stats->peak_live_count <
            stats->live_count) {
            return false;
        }

        if (stats->peak_page_count <
            stats->page_count) {
            return false;
        }

        if (stats->peak_reserved_bytes <
            stats->reserved_bytes) {
            return false;
        }

        if (stats->slot_storage_bytes >
            stats->reserved_bytes) {
            return false;
        }

        if (stats->metadata_bytes >
            stats->reserved_bytes) {
            return false;
        }

        if (stats->live_object_bytes >
            stats->slot_storage_bytes) {
            return false;
        }
    }

    if (!vitte_stats_ratio_valid(
            stats->utilization_basis_points) ||
        !vitte_stats_ratio_valid(
            stats->live_bytes_basis_points) ||
        !vitte_stats_ratio_valid(
            stats->metadata_basis_points)) {
        return false;
    }

    if (!stats->saturated) {
        if (stats->utilization_basis_points !=
            vitte_stats_ratio_basis_points(
                stats->live_count,
                stats->capacity)) {
            return false;
        }

        if (stats->live_bytes_basis_points !=
            vitte_stats_ratio_basis_points(
                stats->live_object_bytes,
                stats->reserved_bytes)) {
            return false;
        }

        if (stats->metadata_basis_points !=
            vitte_stats_ratio_basis_points(
                stats->metadata_bytes,
                stats->reserved_bytes)) {
            return false;
        }
    }

    return true;
}

/* ========================================================================= */
/* Snapshot validation                                                       */
/* ========================================================================= */

bool
vitte_stats_validate_snapshot(
    const vitte_stats_snapshot_t *snapshot)
{
    size_t expected_reserved;
    size_t expected_live;
    size_t expected_requested;
    size_t expected_overhead;
    size_t expected_allocations;
    size_t arena_reserved;
    size_t pool_reserved;
    size_t arena_live;
    size_t pool_live;
    size_t arena_requested;
    size_t pool_requested;
    size_t arena_overhead;
    size_t pool_metadata;
    size_t pool_padding;
    size_t arena_allocations;
    size_t pool_allocations;
    bool saturated;

    if (snapshot == NULL ||
        !snapshot->valid) {
        return false;
    }

    if (!snapshot->has_arena &&
        !snapshot->has_pools) {
        return false;
    }

    if (snapshot->has_arena) {
        if (!vitte_stats_validate_arena(
                &snapshot->arena)) {
            return false;
        }
    } else if (snapshot->arena.valid) {
        return false;
    }

    if (snapshot->has_pools) {
        if (!vitte_stats_validate_pools(
                &snapshot->pools)) {
            return false;
        }
    } else if (snapshot->pools.pool_count != 0u ||
               snapshot->pools.valid_pool_count != 0u ||
               snapshot->pools.invalid_pool_count != 0u) {
        return false;
    }

    arena_reserved =
        snapshot->has_arena
            ? snapshot->arena.reserved_bytes
            : 0u;

    pool_reserved =
        snapshot->has_pools
            ? snapshot->pools.reserved_bytes
            : 0u;

    arena_live =
        snapshot->has_arena
            ? snapshot->arena.used_bytes
            : 0u;

    pool_live =
        snapshot->has_pools
            ? snapshot->pools.live_object_bytes
            : 0u;

    arena_requested =
        snapshot->has_arena
            ? snapshot->arena.requested_bytes
            : 0u;

    pool_requested =
        snapshot->has_pools
            ? snapshot->pools.live_object_bytes
            : 0u;

    arena_overhead =
        snapshot->has_arena
            ? snapshot->arena.padding_bytes
            : 0u;

    pool_metadata =
        snapshot->has_pools
            ? snapshot->pools.metadata_bytes
            : 0u;

    pool_padding =
        snapshot->has_pools
            ? snapshot->pools.slot_padding_bytes
            : 0u;

    arena_allocations =
        snapshot->has_arena
            ? snapshot->arena.allocation_count
            : 0u;

    pool_allocations =
        snapshot->has_pools
            ? snapshot->pools.allocation_count
            : 0u;

    saturated = false;

    expected_reserved =
        vitte_stats_saturating_add(
            arena_reserved,
            pool_reserved,
            &saturated);

    expected_live =
        vitte_stats_saturating_add(
            arena_live,
            pool_live,
            &saturated);

    expected_requested =
        vitte_stats_saturating_add(
            arena_requested,
            pool_requested,
            &saturated);

    expected_overhead =
        vitte_stats_saturating_add(
            arena_overhead,
            pool_metadata,
            &saturated);

    expected_overhead =
        vitte_stats_saturating_add(
            expected_overhead,
            pool_padding,
            &saturated);

    expected_allocations =
        vitte_stats_saturating_add(
            arena_allocations,
            pool_allocations,
            &saturated);

    if (snapshot->total_reserved_bytes !=
        expected_reserved) {
        return false;
    }

    if (snapshot->total_live_bytes !=
        expected_live) {
        return false;
    }

    if (snapshot->total_requested_bytes !=
        expected_requested) {
        return false;
    }

    if (snapshot->total_overhead_bytes !=
        expected_overhead) {
        return false;
    }

    if (snapshot->total_allocation_count !=
        expected_allocations) {
        return false;
    }

    if (snapshot->total_unused_bytes !=
        vitte_stats_nonnegative_difference(
            snapshot->total_reserved_bytes,
            snapshot->total_live_bytes)) {
        return false;
    }

    if (!snapshot->saturated &&
        snapshot->total_live_bytes >
            snapshot->total_reserved_bytes) {
        return false;
    }

    if (!snapshot->saturated &&
        snapshot->total_requested_bytes >
            snapshot->total_live_bytes) {
        return false;
    }

    if (!vitte_stats_ratio_valid(
            snapshot->utilization_basis_points) ||
        !vitte_stats_ratio_valid(
            snapshot->overhead_basis_points)) {
        return false;
    }

    if (!snapshot->saturated) {
        if (snapshot->utilization_basis_points !=
            vitte_stats_ratio_basis_points(
                snapshot->total_live_bytes,
                snapshot->total_reserved_bytes)) {
            return false;
        }

        if (snapshot->overhead_basis_points !=
            vitte_stats_ratio_basis_points(
                snapshot->total_overhead_bytes,
                snapshot->total_reserved_bytes)) {
            return false;
        }
    }

    if (snapshot->has_pools &&
        snapshot->pools.saturated &&
        !snapshot->saturated) {
        return false;
    }

    if (saturated &&
        !snapshot->saturated) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Delta validation                                                          */
/* ========================================================================= */

bool
vitte_stats_validate_delta(
    const vitte_stats_delta_t *delta)
{
    if (delta == NULL ||
        !delta->valid) {
        return false;
    }

    if (!vitte_stats_ratio_valid(
            delta->utilization_before_basis_points) ||
        !vitte_stats_ratio_valid(
            delta->utilization_after_basis_points) ||
        !vitte_stats_ratio_valid(
            delta->overhead_before_basis_points) ||
        !vitte_stats_ratio_valid(
            delta->overhead_after_basis_points)) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* End                                                                       */
/* ========================================================================= */