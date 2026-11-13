/*
 * Vitte Compiler
 * src/arena/pool.c
 *
 * Fixed-size object pool allocator.
 *
 * ISO C17.
 *
 * Properties:
 *
 *   - O(1) allocation in the common case
 *   - O(1) release after page lookup
 *   - fixed object size and alignment
 *   - paged/geometric growth
 *   - intrusive free-list
 *   - allocation bitmap
 *   - deterministic reset/reuse
 *   - reliable double-free detection
 *   - foreign/interior-pointer rejection
 *   - overflow-safe layout arithmetic
 *   - configurable object/memory limits
 *   - optional page trimming
 *   - optional memory poisoning
 *   - cumulative high-water statistics
 *   - generation tracking
 *   - transactional page construction
 *   - deep structural validation
 *
 * Free slots contain an intrusive next pointer. Consequently slot_size is at
 * least sizeof(vitte_pool_free_node_t), regardless of the logical object size.
 *
 * The pool is not internally synchronized. Concurrent mutation requires
 * external synchronization.
 */

#include "pool.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

#ifndef VITTE_POOL_MAGIC
#define VITTE_POOL_MAGIC \
    UINT64_C(0x5649545445504F4F)
#endif

#ifndef VITTE_POOL_DEAD_MAGIC
#define VITTE_POOL_DEAD_MAGIC \
    UINT64_C(0x44454144504F4F4C)
#endif

#ifndef VITTE_POOL_PAGE_MAGIC
#define VITTE_POOL_PAGE_MAGIC \
    UINT64_C(0x5649545445504147)
#endif

#ifndef VITTE_POOL_PAGE_DEAD_MAGIC
#define VITTE_POOL_PAGE_DEAD_MAGIC \
    UINT64_C(0x4445414450414745)
#endif

#ifndef VITTE_POOL_DEFAULT_OBJECTS_PER_PAGE
#define VITTE_POOL_DEFAULT_OBJECTS_PER_PAGE \
    ((size_t)64u)
#endif

#ifndef VITTE_POOL_MIN_OBJECTS_PER_PAGE
#define VITTE_POOL_MIN_OBJECTS_PER_PAGE \
    ((size_t)8u)
#endif

#ifndef VITTE_POOL_MAX_OBJECTS_PER_PAGE
#define VITTE_POOL_MAX_OBJECTS_PER_PAGE \
    ((size_t)65536u)
#endif

#ifndef VITTE_POOL_DEFAULT_MAX_OBJECTS
#define VITTE_POOL_DEFAULT_MAX_OBJECTS \
    SIZE_MAX
#endif

#ifndef VITTE_POOL_DEFAULT_MAX_RESERVED_BYTES
#define VITTE_POOL_DEFAULT_MAX_RESERVED_BYTES \
    SIZE_MAX
#endif

#ifndef VITTE_POOL_GROWTH_FACTOR
#define VITTE_POOL_GROWTH_FACTOR \
    ((size_t)2u)
#endif

#ifndef VITTE_POOL_POISON_ALLOC
#define VITTE_POOL_POISON_ALLOC 0
#endif

#ifndef VITTE_POOL_POISON_FREE
#define VITTE_POOL_POISON_FREE 0
#endif

#ifndef VITTE_POOL_POISON_RESET
#define VITTE_POOL_POISON_RESET 0
#endif

#ifndef VITTE_POOL_POISON_DESTROY
#define VITTE_POOL_POISON_DESTROY 0
#endif

#ifndef VITTE_POOL_ALLOC_PATTERN
#define VITTE_POOL_ALLOC_PATTERN 0xCD
#endif

#ifndef VITTE_POOL_FREE_PATTERN
#define VITTE_POOL_FREE_PATTERN 0xDD
#endif

#ifndef VITTE_POOL_RESET_PATTERN
#define VITTE_POOL_RESET_PATTERN 0xDE
#endif

#ifndef VITTE_POOL_DESTROY_PATTERN
#define VITTE_POOL_DESTROY_PATTERN 0xFD
#endif

/* ========================================================================= */
/* Internal structures                                                       */
/* ========================================================================= */

typedef struct vitte_pool_free_node {
    struct vitte_pool_free_node *next;
} vitte_pool_free_node_t;

/*
 * One malloc() owns:
 *
 *     [ page header ]
 *     [ allocation bitmap ]
 *     [ alignment padding ]
 *     [ slots ]
 */
struct vitte_pool_page {
    uint64_t magic;

    vitte_pool_page_t *next;
    vitte_pool_page_t *previous;

    size_t slot_count;
    size_t live_count;

    size_t bitmap_size;
    size_t slots_offset;
    size_t allocation_size;

    uint64_t generation;
};

/* ========================================================================= */
/* Compile-time invariants                                                   */
/* ========================================================================= */

_Static_assert(
    sizeof(uint64_t) == 8u,
    "Vitte pool requires 64-bit uint64_t");

_Static_assert(
    sizeof(uintptr_t) >= sizeof(void *),
    "uintptr_t must represent native pointers");

_Static_assert(
    VITTE_POOL_MIN_OBJECTS_PER_PAGE != 0u,
    "minimum objects per page must not be zero");

_Static_assert(
    VITTE_POOL_DEFAULT_OBJECTS_PER_PAGE != 0u,
    "default objects per page must not be zero");

_Static_assert(
    VITTE_POOL_MAX_OBJECTS_PER_PAGE != 0u,
    "maximum objects per page must not be zero");

_Static_assert(
    VITTE_POOL_MIN_OBJECTS_PER_PAGE <=
        VITTE_POOL_MAX_OBJECTS_PER_PAGE,
    "minimum objects per page exceeds maximum");

_Static_assert(
    VITTE_POOL_DEFAULT_OBJECTS_PER_PAGE <=
        VITTE_POOL_MAX_OBJECTS_PER_PAGE,
    "default objects per page exceeds maximum");

_Static_assert(
    VITTE_POOL_GROWTH_FACTOR != 0u,
    "pool growth factor must not be zero");

_Static_assert(
    _Alignof(max_align_t) != 0u,
    "max_align_t must have valid alignment");

/* ========================================================================= */
/* Arithmetic                                                                */
/* ========================================================================= */

static bool
vitte_pool_add_overflow(
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
vitte_pool_mul_overflow(
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

static bool
vitte_pool_is_power_of_two(
    size_t value)
{
    return
        value != 0u &&
        (value & (value - 1u)) == 0u;
}

static bool
vitte_pool_align_up(
    size_t value,
    size_t alignment,
    size_t *result)
{
    size_t mask;
    size_t temporary;

    if (result == NULL ||
        !vitte_pool_is_power_of_two(
            alignment)) {
        return false;
    }

    mask = alignment - 1u;

    if (vitte_pool_add_overflow(
            value,
            mask,
            &temporary)) {
        return false;
    }

    *result =
        temporary & ~mask;

    return true;
}

static size_t
vitte_pool_saturating_increment(
    size_t value)
{
    return
        value == SIZE_MAX
            ? SIZE_MAX
            : value + 1u;
}

static uint64_t
vitte_pool_next_generation(
    uint64_t generation)
{
    ++generation;

    if (generation == UINT64_C(0)) {
        generation = UINT64_C(1);
    }

    return generation;
}

/* ========================================================================= */
/* Pointer arithmetic                                                        */
/* ========================================================================= */

static unsigned char *
vitte_pool_page_bytes(
    vitte_pool_page_t *page)
{
    return
        (unsigned char *)page;
}

static const unsigned char *
vitte_pool_page_bytes_const(
    const vitte_pool_page_t *page)
{
    return
        (const unsigned char *)page;
}

static unsigned char *
vitte_pool_page_bitmap(
    vitte_pool_page_t *page)
{
    return
        vitte_pool_page_bytes(page) +
        sizeof(vitte_pool_page_t);
}

static const unsigned char *
vitte_pool_page_bitmap_const(
    const vitte_pool_page_t *page)
{
    return
        vitte_pool_page_bytes_const(page) +
        sizeof(vitte_pool_page_t);
}

static unsigned char *
vitte_pool_page_slots(
    vitte_pool_page_t *page)
{
    return
        vitte_pool_page_bytes(page) +
        page->slots_offset;
}

static const unsigned char *
vitte_pool_page_slots_const(
    const vitte_pool_page_t *page)
{
    return
        vitte_pool_page_bytes_const(page) +
        page->slots_offset;
}

/* ========================================================================= */
/* Bitmap                                                                    */
/* ========================================================================= */

static bool
vitte_pool_bitmap_size(
    size_t slot_count,
    size_t *result)
{
    size_t temporary;

    if (result == NULL ||
        slot_count == 0u) {
        return false;
    }

    if (vitte_pool_add_overflow(
            slot_count,
            (size_t)7u,
            &temporary)) {
        return false;
    }

    *result =
        temporary / (size_t)8u;

    return
        *result != 0u;
}

static bool
vitte_pool_bitmap_get(
    const vitte_pool_page_t *page,
    size_t index)
{
    const unsigned char *bitmap;
    unsigned char mask;

    if (page == NULL ||
        index >= page->slot_count) {
        return false;
    }

    bitmap =
        vitte_pool_page_bitmap_const(
            page);

    mask =
        (unsigned char)(
            (unsigned int)1u <<
            (unsigned int)(index & 7u));

    return
        (bitmap[index >> 3u] & mask) != 0u;
}

static bool
vitte_pool_bitmap_set(
    vitte_pool_page_t *page,
    size_t index)
{
    unsigned char *bitmap;
    unsigned char mask;

    if (page == NULL ||
        index >= page->slot_count) {
        return false;
    }

    bitmap =
        vitte_pool_page_bitmap(page);

    mask =
        (unsigned char)(
            (unsigned int)1u <<
            (unsigned int)(index & 7u));

    bitmap[index >> 3u] |= mask;

    return true;
}

static bool
vitte_pool_bitmap_clear(
    vitte_pool_page_t *page,
    size_t index)
{
    unsigned char *bitmap;
    unsigned char mask;

    if (page == NULL ||
        index >= page->slot_count) {
        return false;
    }

    bitmap =
        vitte_pool_page_bitmap(page);

    mask =
        (unsigned char)(
            (unsigned int)1u <<
            (unsigned int)(index & 7u));

    bitmap[index >> 3u] &=
        (unsigned char)~mask;

    return true;
}

static void
vitte_pool_bitmap_clear_all(
    vitte_pool_page_t *page)
{
    if (page == NULL ||
        page->bitmap_size == 0u) {
        return;
    }

    memset(
        vitte_pool_page_bitmap(page),
        0,
        page->bitmap_size);
}

static size_t
vitte_pool_bitmap_count_live(
    const vitte_pool_page_t *page)
{
    size_t index;
    size_t count;

    if (page == NULL) {
        return 0u;
    }

    count = 0u;

    for (index = 0u;
         index < page->slot_count;
         ++index) {
        if (vitte_pool_bitmap_get(
                page,
                index)) {
            ++count;
        }
    }

    return count;
}

static bool
vitte_pool_bitmap_unused_bits_clear(
    const vitte_pool_page_t *page)
{
    size_t remainder;
    unsigned char valid_mask;
    unsigned char last;

    if (page == NULL ||
        page->bitmap_size == 0u) {
        return false;
    }

    remainder =
        page->slot_count & 7u;

    if (remainder == 0u) {
        return true;
    }

    valid_mask =
        (unsigned char)(
            ((unsigned int)1u <<
             (unsigned int)remainder) -
            (unsigned int)1u);

    last =
        vitte_pool_page_bitmap_const(page)
            [page->bitmap_size - 1u];

    return
        (last & (unsigned char)~valid_mask) == 0u;
}

/* ========================================================================= */
/* Validity                                                                  */
/* ========================================================================= */

static bool
vitte_pool_magic_valid(
    const vitte_pool_t *pool)
{
    return
        pool != NULL &&
        pool->magic == VITTE_POOL_MAGIC;
}

static bool
vitte_pool_page_magic_valid(
    const vitte_pool_page_t *page)
{
    return
        page != NULL &&
        page->magic ==
            VITTE_POOL_PAGE_MAGIC;
}

/* ========================================================================= */
/* Errors                                                                    */
/* ========================================================================= */

static void
vitte_pool_set_error(
    vitte_pool_t *pool,
    vitte_pool_error_t error)
{
    if (!vitte_pool_magic_valid(pool)) {
        return;
    }

    pool->last_error = error;
}

vitte_pool_error_t
vitte_pool_last_error(
    const vitte_pool_t *pool)
{
    if (!vitte_pool_magic_valid(pool)) {
        return
            VITTE_POOL_ERROR_INVALID_POOL;
    }

    return pool->last_error;
}

void
vitte_pool_clear_error(
    vitte_pool_t *pool)
{
    if (!vitte_pool_magic_valid(pool)) {
        return;
    }

    pool->last_error =
        VITTE_POOL_ERROR_NONE;
}

const char *
vitte_pool_error_name(
    vitte_pool_error_t error)
{
    switch (error) {
        case VITTE_POOL_ERROR_NONE:
            return "none";

        case VITTE_POOL_ERROR_INVALID_POOL:
            return "invalid-pool";

        case VITTE_POOL_ERROR_INVALID_ARGUMENT:
            return "invalid-argument";

        case VITTE_POOL_ERROR_INVALID_ALIGNMENT:
            return "invalid-alignment";

        case VITTE_POOL_ERROR_OVERFLOW:
            return "overflow";

        case VITTE_POOL_ERROR_OUT_OF_MEMORY:
            return "out-of-memory";

        case VITTE_POOL_ERROR_LIMIT_EXCEEDED:
            return "limit-exceeded";

        case VITTE_POOL_ERROR_FOREIGN_POINTER:
            return "foreign-pointer";

        case VITTE_POOL_ERROR_MISALIGNED_POINTER:
            return "misaligned-pointer";

        case VITTE_POOL_ERROR_DOUBLE_FREE:
            return "double-free";

        case VITTE_POOL_ERROR_CORRUPTION:
            return "corruption";

        default:
            return "unknown";
    }
}

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

vitte_pool_config_t
vitte_pool_config_default(void)
{
    vitte_pool_config_t config;

    memset(
        &config,
        0,
        sizeof(config));

    config.objects_per_page =
        VITTE_POOL_DEFAULT_OBJECTS_PER_PAGE;

    config.max_objects =
        VITTE_POOL_DEFAULT_MAX_OBJECTS;

    config.max_reserved_bytes =
        VITTE_POOL_DEFAULT_MAX_RESERVED_BYTES;

    config.geometric_growth = true;
    config.retain_empty_pages = true;
    config.zero_on_allocate = false;

    return config;
}

bool
vitte_pool_config_validate(
    const vitte_pool_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    if (config->objects_per_page == 0u ||
        config->objects_per_page >
            VITTE_POOL_MAX_OBJECTS_PER_PAGE) {
        return false;
    }

    /*
     * SIZE_MAX means unlimited.
     *
     * Zero is also accepted here because init() normalizes zero limits to the
     * documented defaults before validating the effective configuration.
     */

    return true;
}

/* ========================================================================= */
/* Slot size                                                                 */
/* ========================================================================= */

static bool
vitte_pool_compute_slot_size(
    size_t object_size,
    size_t alignment,
    size_t *slot_size)
{
    size_t minimum;

    if (slot_size == NULL ||
        object_size == 0u ||
        !vitte_pool_is_power_of_two(
            alignment)) {
        return false;
    }

    minimum = object_size;

    if (minimum <
        sizeof(vitte_pool_free_node_t)) {
        minimum =
            sizeof(vitte_pool_free_node_t);
    }

    return
        vitte_pool_align_up(
            minimum,
            alignment,
            slot_size);
}

/* ========================================================================= */
/* Page layout                                                               */
/* ========================================================================= */

static bool
vitte_pool_compute_page_layout(
    const vitte_pool_t *pool,
    size_t slot_count,
    size_t *bitmap_size,
    size_t *slots_offset,
    size_t *allocation_size)
{
    size_t bitmap;
    size_t offset;
    size_t slots_bytes;
    size_t total;

    if (pool == NULL ||
        bitmap_size == NULL ||
        slots_offset == NULL ||
        allocation_size == NULL ||
        slot_count == 0u ||
        slot_count >
            VITTE_POOL_MAX_OBJECTS_PER_PAGE ||
        pool->slot_size == 0u ||
        !vitte_pool_is_power_of_two(
            pool->alignment)) {
        return false;
    }

    if (!vitte_pool_bitmap_size(
            slot_count,
            &bitmap)) {
        return false;
    }

    if (vitte_pool_add_overflow(
            sizeof(vitte_pool_page_t),
            bitmap,
            &offset)) {
        return false;
    }

    if (!vitte_pool_align_up(
            offset,
            pool->alignment,
            &offset)) {
        return false;
    }

    if (vitte_pool_mul_overflow(
            slot_count,
            pool->slot_size,
            &slots_bytes)) {
        return false;
    }

    if (vitte_pool_add_overflow(
            offset,
            slots_bytes,
            &total)) {
        return false;
    }

    if (offset >
        total) {
        return false;
    }

    *bitmap_size = bitmap;
    *slots_offset = offset;
    *allocation_size = total;

    return true;
}

/* ========================================================================= */
/* Page structural validation                                                */
/* ========================================================================= */

static bool
vitte_pool_page_layout_valid(
    const vitte_pool_t *pool,
    const vitte_pool_page_t *page)
{
    size_t bitmap_size;
    size_t slots_offset;
    size_t allocation_size;
    const unsigned char *slots;
    uintptr_t slots_address;

    if (!vitte_pool_magic_valid(pool) ||
        !vitte_pool_page_magic_valid(page)) {
        return false;
    }

    if (page->slot_count == 0u ||
        page->slot_count >
            VITTE_POOL_MAX_OBJECTS_PER_PAGE ||
        page->live_count >
            page->slot_count ||
        page->generation !=
            pool->generation) {
        return false;
    }

    if (!vitte_pool_compute_page_layout(
            pool,
            page->slot_count,
            &bitmap_size,
            &slots_offset,
            &allocation_size)) {
        return false;
    }

    if (page->bitmap_size != bitmap_size ||
        page->slots_offset != slots_offset ||
        page->allocation_size !=
            allocation_size) {
        return false;
    }

    slots =
        vitte_pool_page_slots_const(page);
    slots_address =
        (uintptr_t)slots;

    if ((slots_address &
         (uintptr_t)(pool->alignment - 1u)) !=
        (uintptr_t)0u) {
        return false;
    }

    if (!vitte_pool_bitmap_unused_bits_clear(
            page)) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Page ownership                                                            */
/* ========================================================================= */

static bool
vitte_pool_page_contains(
    const vitte_pool_t *pool,
    const vitte_pool_page_t *page,
    const void *pointer)
{
    uintptr_t address;
    uintptr_t begin;
    uintptr_t end;
    const unsigned char *slots;
    size_t slots_bytes;

    if (!vitte_pool_magic_valid(pool) ||
        !vitte_pool_page_magic_valid(page) ||
        pointer == NULL ||
        pool->slot_size == 0u) {
        return false;
    }

    if (vitte_pool_mul_overflow(
            page->slot_count,
            pool->slot_size,
            &slots_bytes)) {
        return false;
    }

    slots =
        vitte_pool_page_slots_const(page);
    begin =
        (uintptr_t)slots;

    if ((uintptr_t)slots_bytes >
        UINTPTR_MAX - begin) {
        return false;
    }

    end =
        begin +
        (uintptr_t)slots_bytes;

    address =
        (uintptr_t)pointer;

    return
        address >= begin &&
        address < end;
}

static bool
vitte_pool_page_index_of(
    const vitte_pool_t *pool,
    const vitte_pool_page_t *page,
    const void *pointer,
    size_t *index)
{
    uintptr_t address;
    uintptr_t begin;
    uintptr_t difference;
    const unsigned char *slots;
    size_t offset;

    if (index == NULL) {
        return false;
    }

    *index = 0u;

    if (!vitte_pool_page_contains(
            pool,
            page,
            pointer)) {
        return false;
    }

    address =
        (uintptr_t)pointer;

    slots =
        vitte_pool_page_slots_const(page);
    begin =
        (uintptr_t)slots;

    difference =
        address - begin;

    if (difference >
        (uintptr_t)SIZE_MAX) {
        return false;
    }

    offset =
        (size_t)difference;

    if (pool->slot_size == 0u ||
        (offset % pool->slot_size) != 0u) {
        return false;
    }

    *index =
        offset / pool->slot_size;

    return
        *index < page->slot_count;
}

static unsigned char *
vitte_pool_page_slot(
    const vitte_pool_t *pool,
    vitte_pool_page_t *page,
    size_t index)
{
    size_t offset;

    if (!vitte_pool_magic_valid(pool) ||
        !vitte_pool_page_magic_valid(page) ||
        index >= page->slot_count) {
        return NULL;
    }

    if (vitte_pool_mul_overflow(
            index,
            pool->slot_size,
            &offset)) {
        return NULL;
    }

    return
        vitte_pool_page_slots(page) +
        offset;
}

static const unsigned char *
vitte_pool_page_slot_const(
    const vitte_pool_t *pool,
    const vitte_pool_page_t *page,
    size_t index)
{
    size_t offset;

    if (!vitte_pool_magic_valid(pool) ||
        !vitte_pool_page_magic_valid(page) ||
        index >= page->slot_count) {
        return NULL;
    }

    if (vitte_pool_mul_overflow(
            index,
            pool->slot_size,
            &offset)) {
        return NULL;
    }

    return
        vitte_pool_page_slots_const(page) +
        offset;
}

/* ========================================================================= */
/* Page chain lookup                                                         */
/* ========================================================================= */

static vitte_pool_page_t *
vitte_pool_find_page(
    const vitte_pool_t *pool,
    const void *pointer)
{
    vitte_pool_page_t *page;
    size_t guard;

    if (!vitte_pool_magic_valid(pool) ||
        pointer == NULL) {
        return NULL;
    }

    page = pool->first_page;
    guard = 0u;

    while (page != NULL) {
        if (!vitte_pool_page_magic_valid(
                page)) {
            return NULL;
        }

        if (vitte_pool_page_contains(
                pool,
                page,
                pointer)) {
            return page;
        }

        page = page->next;

        ++guard;

        if (guard > pool->page_count) {
            return NULL;
        }
    }

    return NULL;
}

/* ========================================================================= */
/* Page chain manipulation                                                   */
/* ========================================================================= */

static bool
vitte_pool_link_page(
    vitte_pool_t *pool,
    vitte_pool_page_t *page)
{
    if (!vitte_pool_magic_valid(pool) ||
        !vitte_pool_page_magic_valid(page) ||
        page->next != NULL ||
        page->previous != NULL ||
        pool->page_count == SIZE_MAX) {
        return false;
    }

    page->previous =
        pool->last_page;

    if (pool->last_page != NULL) {
        pool->last_page->next = page;
    } else {
        pool->first_page = page;
    }

    pool->last_page = page;

    ++pool->page_count;

    if (pool->page_count >
        pool->peak_page_count) {
        pool->peak_page_count =
            pool->page_count;
    }

    return true;
}

static bool
vitte_pool_unlink_page(
    vitte_pool_t *pool,
    vitte_pool_page_t *page)
{
    if (!vitte_pool_magic_valid(pool) ||
        !vitte_pool_page_magic_valid(page) ||
        pool->page_count == 0u) {
        return false;
    }

    if (page->previous != NULL) {
        if (page->previous->next != page) {
            return false;
        }

        page->previous->next =
            page->next;
    } else {
        if (pool->first_page != page) {
            return false;
        }

        pool->first_page =
            page->next;
    }

    if (page->next != NULL) {
        if (page->next->previous != page) {
            return false;
        }

        page->next->previous =
            page->previous;
    } else {
        if (pool->last_page != page) {
            return false;
        }

        pool->last_page =
            page->previous;
    }

    page->next = NULL;
    page->previous = NULL;

    --pool->page_count;

    return true;
}

/* ========================================================================= */
/* Free-list                                                                 */
/* ========================================================================= */

static void
vitte_pool_push_free(
    vitte_pool_t *pool,
    void *slot)
{
    vitte_pool_free_node_t *node;

    node =
        (vitte_pool_free_node_t *)slot;

    node->next =
        pool->free_list;

    pool->free_list = node;
}

static void *
vitte_pool_pop_free(
    vitte_pool_t *pool)
{
    vitte_pool_free_node_t *node;

    node = pool->free_list;

    if (node == NULL) {
        return NULL;
    }

    pool->free_list =
        node->next;

    node->next = NULL;

    return node;
}

/* ========================================================================= */
/* Free-list validation helpers                                              */
/* ========================================================================= */

static bool
vitte_pool_free_node_valid(
    const vitte_pool_t *pool,
    const vitte_pool_free_node_t *node)
{
    vitte_pool_page_t *page;
    size_t index;

    if (node == NULL) {
        return false;
    }

    page =
        vitte_pool_find_page(
            pool,
            node);

    if (page == NULL) {
        return false;
    }

    if (!vitte_pool_page_index_of(
            pool,
            page,
            node,
            &index)) {
        return false;
    }

    return
        !vitte_pool_bitmap_get(
            page,
            index);
}

/*
 * Safe cycle detection.
 *
 * Unlike a conventional raw Floyd loop, every node is verified as pool-owned
 * before its next pointer is dereferenced.
 */
static bool
vitte_pool_free_list_has_cycle(
    const vitte_pool_t *pool)
{
    const vitte_pool_free_node_t *slow;
    const vitte_pool_free_node_t *fast;

    slow = pool->free_list;
    fast = pool->free_list;

    for (;;) {
        if (fast == NULL) {
            return false;
        }

        if (!vitte_pool_free_node_valid(
                pool,
                fast)) {
            return true;
        }

        fast = fast->next;

        if (fast == NULL) {
            return false;
        }

        if (!vitte_pool_free_node_valid(
                pool,
                fast)) {
            return true;
        }

        fast = fast->next;

        if (slow == NULL ||
            !vitte_pool_free_node_valid(
                pool,
                slow)) {
            return true;
        }

        slow = slow->next;

        if (slow != NULL &&
            slow == fast) {
            return true;
        }
    }
}

/* ========================================================================= */
/* Free-list rebuild                                                         */
/* ========================================================================= */

static bool
vitte_pool_rebuild_free_list(
    vitte_pool_t *pool)
{
    vitte_pool_page_t *page;
    size_t free_count;
    size_t page_guard;

    if (!vitte_pool_magic_valid(pool)) {
        return false;
    }

    pool->free_list = NULL;

    free_count = 0u;
    page = pool->last_page;
    page_guard = 0u;

    while (page != NULL) {
        size_t index;

        if (!vitte_pool_page_layout_valid(
                pool,
                page)) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            pool->free_list = NULL;
            pool->free_count = 0u;

            return false;
        }

        index = page->slot_count;

        while (index != 0u) {
            void *slot;

            --index;

            if (vitte_pool_bitmap_get(
                    page,
                    index)) {
                continue;
            }

            slot =
                vitte_pool_page_slot(
                    pool,
                    page,
                    index);

            if (slot == NULL ||
                free_count == SIZE_MAX) {
                vitte_pool_set_error(
                    pool,
                    VITTE_POOL_ERROR_CORRUPTION);

                pool->free_list = NULL;
                pool->free_count = 0u;

                return false;
            }

            vitte_pool_push_free(
                pool,
                slot);

            ++free_count;
        }

        page = page->previous;

        ++page_guard;

        if (page_guard > pool->page_count) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            pool->free_list = NULL;
            pool->free_count = 0u;

            return false;
        }
    }

    pool->free_count = free_count;

    return true;
}

/* ========================================================================= */
/* Page storage creation                                                     */
/* ========================================================================= */

static vitte_pool_page_t *
vitte_pool_allocate_page_storage(
    const vitte_pool_t *pool,
    size_t slot_count,
    size_t *allocation_size_result)
{
    vitte_pool_page_t *page;
    size_t bitmap_size;
    size_t slots_offset;
    size_t allocation_size;

    if (allocation_size_result != NULL) {
        *allocation_size_result = 0u;
    }

    if (!vitte_pool_magic_valid(pool) ||
        slot_count == 0u ||
        slot_count >
            VITTE_POOL_MAX_OBJECTS_PER_PAGE) {
        return NULL;
    }

    if (!vitte_pool_compute_page_layout(
            pool,
            slot_count,
            &bitmap_size,
            &slots_offset,
            &allocation_size)) {
        return NULL;
    }

    page =
        (vitte_pool_page_t *)
            malloc(allocation_size);

    if (page == NULL) {
        return NULL;
    }

    memset(
        page,
        0,
        sizeof(*page));

    page->magic =
        VITTE_POOL_PAGE_MAGIC;

    page->next = NULL;
    page->previous = NULL;

    page->slot_count =
        slot_count;

    page->live_count = 0u;

    page->bitmap_size =
        bitmap_size;

    page->slots_offset =
        slots_offset;

    page->allocation_size =
        allocation_size;

    page->generation =
        pool->generation;

    vitte_pool_bitmap_clear_all(
        page);

#if VITTE_POOL_POISON_RESET
    {
        size_t slots_bytes;

        if (vitte_pool_mul_overflow(
                slot_count,
                pool->slot_size,
                &slots_bytes)) {
            page->magic =
                VITTE_POOL_PAGE_DEAD_MAGIC;

            free(page);
            return NULL;
        }

        memset(
            vitte_pool_page_slots(page),
            VITTE_POOL_RESET_PATTERN,
            slots_bytes);
    }
#endif

    if (allocation_size_result != NULL) {
        *allocation_size_result =
            allocation_size;
    }

    return page;
}

/* ========================================================================= */
/* Page destruction                                                          */
/* ========================================================================= */

static void
vitte_pool_destroy_page_storage(
    vitte_pool_page_t *page)
{
    size_t allocation_size;

    if (!vitte_pool_page_magic_valid(
            page)) {
        return;
    }

    allocation_size =
        page->allocation_size;

#if VITTE_POOL_POISON_DESTROY
    if (allocation_size >
        sizeof(vitte_pool_page_t)) {
        memset(
            vitte_pool_page_bytes(page) +
                sizeof(vitte_pool_page_t),
            VITTE_POOL_DESTROY_PATTERN,
            allocation_size -
                sizeof(vitte_pool_page_t));
    }
#else
    (void)allocation_size;
#endif

    page->magic =
        VITTE_POOL_PAGE_DEAD_MAGIC;

    page->next = NULL;
    page->previous = NULL;

    page->slot_count = 0u;
    page->live_count = 0u;
    page->bitmap_size = 0u;
    page->slots_offset = 0u;
    page->allocation_size = 0u;
    page->generation = 0u;

    free(page);
}

/* ========================================================================= */
/* Growth                                                                    */
/* ========================================================================= */

static size_t
vitte_pool_next_page_size(
    const vitte_pool_t *pool)
{
    size_t result;

    if (!vitte_pool_magic_valid(pool)) {
        return 0u;
    }

    if (pool->config.max_objects !=
            SIZE_MAX &&
        pool->capacity >=
            pool->config.max_objects) {
        return 0u;
    }

    if (pool->page_count == 0u ||
        !pool->config.geometric_growth) {
        result =
            pool->config.objects_per_page;
    } else {
        size_t previous;

        if (pool->last_page == NULL ||
            !vitte_pool_page_magic_valid(
                pool->last_page)) {
            return 0u;
        }

        previous =
            pool->last_page->slot_count;

        if (VITTE_POOL_GROWTH_FACTOR <= 1u) {
            result = previous;
        } else if (vitte_pool_mul_overflow(
                       previous,
                       VITTE_POOL_GROWTH_FACTOR,
                       &result)) {
            result =
                VITTE_POOL_MAX_OBJECTS_PER_PAGE;
        }
    }

    if (result <
        VITTE_POOL_MIN_OBJECTS_PER_PAGE) {
        result =
            VITTE_POOL_MIN_OBJECTS_PER_PAGE;
    }

    if (result >
        VITTE_POOL_MAX_OBJECTS_PER_PAGE) {
        result =
            VITTE_POOL_MAX_OBJECTS_PER_PAGE;
    }

    if (pool->config.max_objects !=
        SIZE_MAX) {
        size_t remaining;

        remaining =
            pool->config.max_objects -
            pool->capacity;

        if (result > remaining) {
            result = remaining;
        }
    }

    return result;
}

/* ========================================================================= */
/* Transactional page creation                                               */
/* ========================================================================= */

static vitte_pool_page_t *
vitte_pool_create_page(
    vitte_pool_t *pool,
    size_t requested_slot_count)
{
    vitte_pool_page_t *page;

    size_t slot_count;
    size_t allocation_size;

    size_t new_capacity;
    size_t new_reserved;
    size_t new_free_count;

    size_t index;

    if (!vitte_pool_magic_valid(pool) ||
        requested_slot_count == 0u) {
        if (vitte_pool_magic_valid(pool)) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_INVALID_ARGUMENT);
        }

        return NULL;
    }

    slot_count =
        requested_slot_count;

    if (slot_count >
        VITTE_POOL_MAX_OBJECTS_PER_PAGE) {
        slot_count =
            VITTE_POOL_MAX_OBJECTS_PER_PAGE;
    }

    if (pool->config.max_objects !=
        SIZE_MAX) {
        size_t remaining;

        if (pool->capacity >
            pool->config.max_objects) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            return NULL;
        }

        remaining =
            pool->config.max_objects -
            pool->capacity;

        if (remaining == 0u) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_LIMIT_EXCEEDED);

            return NULL;
        }

        if (slot_count > remaining) {
            slot_count = remaining;
        }
    }

    if (slot_count == 0u) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_LIMIT_EXCEEDED);

        return NULL;
    }

    /*
     * Compute layout before allocating so arithmetic errors are distinguished
     * from malloc failure.
     */
    {
        size_t bitmap_size;
        size_t slots_offset;

        if (!vitte_pool_compute_page_layout(
                pool,
                slot_count,
                &bitmap_size,
                &slots_offset,
                &allocation_size)) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_OVERFLOW);

            return NULL;
        }
    }

    if (vitte_pool_add_overflow(
            pool->capacity,
            slot_count,
            &new_capacity) ||
        vitte_pool_add_overflow(
            pool->reserved_bytes,
            allocation_size,
            &new_reserved) ||
        vitte_pool_add_overflow(
            pool->free_count,
            slot_count,
            &new_free_count)) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_OVERFLOW);

        return NULL;
    }

    if (pool->page_count == SIZE_MAX) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_OVERFLOW);

        return NULL;
    }

    if (pool->config.max_reserved_bytes !=
            SIZE_MAX &&
        new_reserved >
            pool->config.max_reserved_bytes) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_LIMIT_EXCEEDED);

        return NULL;
    }

    page =
        vitte_pool_allocate_page_storage(
            pool,
            slot_count,
            NULL);

    if (page == NULL) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_OUT_OF_MEMORY);

        return NULL;
    }

    /*
     * Build the page-local free-list before publishing the page into the
     * global chain. This keeps page creation transactional.
     */
    {
        vitte_pool_free_node_t *head;

        head = NULL;
        index = slot_count;

        while (index != 0u) {
            vitte_pool_free_node_t *node;
            unsigned char *slot;

            --index;

            slot =
                vitte_pool_page_slot(
                    pool,
                    page,
                    index);

            if (slot == NULL) {
                vitte_pool_destroy_page_storage(
                    page);

                vitte_pool_set_error(
                    pool,
                    VITTE_POOL_ERROR_CORRUPTION);

                return NULL;
            }

            node =
                (vitte_pool_free_node_t *)
                    (void *)slot;

            node->next = head;
            head = node;
        }

        /*
         * Linking cannot fail after all structural/counter preflight checks
         * unless the existing pool chain is already inconsistent.
         */
        if (!vitte_pool_link_page(
                pool,
                page)) {
            vitte_pool_destroy_page_storage(
                page);

            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            return NULL;
        }

        /*
         * Append the old global free-list after the last node of this page so
         * the first physical slot of the newly created page is allocated
         * first.
         */
        if (head != NULL) {
            vitte_pool_free_node_t *tail;
            size_t guard;

            tail = head;
            guard = 1u;

            while (tail->next != NULL) {
                tail = tail->next;
                ++guard;

                if (guard > slot_count) {
                    /*
                     * Impossible for a locally constructed list.
                     */
                    (void)vitte_pool_unlink_page(
                        pool,
                        page);

                    vitte_pool_destroy_page_storage(
                        page);

                    vitte_pool_set_error(
                        pool,
                        VITTE_POOL_ERROR_CORRUPTION);

                    return NULL;
                }
            }

            tail->next =
                pool->free_list;

            pool->free_list = head;
        }
    }

    pool->capacity =
        new_capacity;

    pool->reserved_bytes =
        new_reserved;

    pool->free_count =
        new_free_count;

    if (pool->reserved_bytes >
        pool->peak_reserved_bytes) {
        pool->peak_reserved_bytes =
            pool->reserved_bytes;
    }

    return page;
}

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

bool
vitte_pool_init(
    vitte_pool_t *pool,
    size_t object_size,
    size_t alignment,
    const vitte_pool_config_t *config)
{
    vitte_pool_config_t effective;
    size_t slot_size;

    if (pool == NULL ||
        object_size == 0u) {
        return false;
    }

    /*
     * Reinitializing a live pool without destroy() would leak its pages.
     *
     * We cannot safely inspect arbitrary uninitialized storage before
     * initialization, therefore this remains a caller contract:
     *
     *     initialize once, destroy before reinitializing.
     */
    memset(
        pool,
        0,
        sizeof(*pool));

    if (alignment == 0u) {
        alignment =
            _Alignof(max_align_t);
    }

    if (!vitte_pool_is_power_of_two(
            alignment)) {
        return false;
    }

    /*
     * malloc() guarantees alignment suitable for max_align_t.
     *
     * Over-aligned objects require a different page allocation primitive and
     * are deliberately rejected by this implementation.
     */
    if (alignment >
        _Alignof(max_align_t)) {
        return false;
    }

    if (!vitte_pool_compute_slot_size(
            object_size,
            alignment,
            &slot_size)) {
        return false;
    }

    if (config == NULL) {
        effective =
            vitte_pool_config_default();
    } else {
        effective = *config;
    }

    /*
     * Zero configuration limits preserve the historical/default behavior.
     */
    if (effective.objects_per_page == 0u) {
        effective.objects_per_page =
            VITTE_POOL_DEFAULT_OBJECTS_PER_PAGE;
    }

    if (effective.max_objects == 0u) {
        effective.max_objects =
            VITTE_POOL_DEFAULT_MAX_OBJECTS;
    }

    if (effective.max_reserved_bytes == 0u) {
        effective.max_reserved_bytes =
            VITTE_POOL_DEFAULT_MAX_RESERVED_BYTES;
    }

    if (!vitte_pool_config_validate(
            &effective)) {
        return false;
    }

    pool->magic =
        VITTE_POOL_MAGIC;

    pool->object_size =
        object_size;

    pool->alignment =
        alignment;

    pool->slot_size =
        slot_size;

    pool->config =
        effective;

    pool->first_page = NULL;
    pool->last_page = NULL;
    pool->free_list = NULL;

    pool->page_count = 0u;
    pool->peak_page_count = 0u;

    pool->capacity = 0u;

    pool->live_count = 0u;
    pool->peak_live_count = 0u;

    pool->free_count = 0u;

    pool->reserved_bytes = 0u;
    pool->peak_reserved_bytes = 0u;

    pool->allocation_count = 0u;
    pool->free_operation_count = 0u;

    pool->generation =
        UINT64_C(1);

    pool->last_error =
        VITTE_POOL_ERROR_NONE;

    return true;
}

bool
vitte_pool_init_default(
    vitte_pool_t *pool,
    size_t object_size,
    size_t alignment)
{
    vitte_pool_config_t config;

    config =
        vitte_pool_config_default();

    return
        vitte_pool_init(
            pool,
            object_size,
            alignment,
            &config);
}

void
vitte_pool_destroy(
    vitte_pool_t *pool)
{
    vitte_pool_page_t *page;
    size_t guard;

    if (!vitte_pool_magic_valid(pool)) {
        return;
    }

    /*
     * Detach externally visible state first. Destruction must never leave a
     * dangling free-list reachable through a still-valid pool object.
     */
    pool->free_list = NULL;

    page = pool->first_page;
    guard = 0u;

    while (page != NULL) {
        vitte_pool_page_t *next;

        if (!vitte_pool_page_magic_valid(
                page)) {
            /*
             * Stop instead of dereferencing arbitrary corrupted page state.
             */
            break;
        }

        next = page->next;

        page->next = NULL;
        page->previous = NULL;

        vitte_pool_destroy_page_storage(
            page);

        page = next;

        ++guard;

        if (guard > pool->page_count) {
            break;
        }
    }

    pool->first_page = NULL;
    pool->last_page = NULL;

    pool->page_count = 0u;
    pool->peak_page_count = 0u;

    pool->capacity = 0u;

    pool->live_count = 0u;
    pool->peak_live_count = 0u;

    pool->free_count = 0u;

    pool->reserved_bytes = 0u;
    pool->peak_reserved_bytes = 0u;

    pool->allocation_count = 0u;
    pool->free_operation_count = 0u;

    pool->object_size = 0u;
    pool->slot_size = 0u;
    pool->alignment = 0u;

    memset(
        &pool->config,
        0,
        sizeof(pool->config));

    pool->generation = 0u;

    pool->last_error =
        VITTE_POOL_ERROR_INVALID_POOL;

    pool->magic =
        VITTE_POOL_DEAD_MAGIC;
}

/* ========================================================================= */
/* Reserve                                                                   */
/* ========================================================================= */

bool
vitte_pool_reserve(
    vitte_pool_t *pool,
    size_t minimum_capacity)
{
    if (!vitte_pool_magic_valid(pool)) {
        return false;
    }

    pool->last_error =
        VITTE_POOL_ERROR_NONE;

    if (minimum_capacity <=
        pool->capacity) {
        return true;
    }

    if (pool->config.max_objects !=
            SIZE_MAX &&
        minimum_capacity >
            pool->config.max_objects) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_LIMIT_EXCEEDED);

        return false;
    }

    while (pool->capacity <
           minimum_capacity) {
        size_t page_size;

        page_size =
            vitte_pool_next_page_size(
                pool);

        if (page_size == 0u) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_LIMIT_EXCEEDED);

            return false;
        }

        /*
         * Do not grow beyond the caller's requested minimum merely because
         * geometric growth selected a larger page, unless the configured
         * minimum page size requires it.
         */
        {
            size_t needed;

            needed =
                minimum_capacity -
                pool->capacity;

            if (page_size > needed &&
                needed >=
                    VITTE_POOL_MIN_OBJECTS_PER_PAGE) {
                page_size = needed;
            }
        }

        if (vitte_pool_create_page(
                pool,
                page_size) == NULL) {
            return false;
        }
    }

    return true;
}

/* ========================================================================= */
/* Allocation                                                                */
/* ========================================================================= */

void *
vitte_pool_alloc(
    vitte_pool_t *pool)
{
    vitte_pool_free_node_t *node;
    vitte_pool_page_t *page;
    size_t index;

    if (!vitte_pool_magic_valid(pool)) {
        return NULL;
    }

    pool->last_error =
        VITTE_POOL_ERROR_NONE;

    if (pool->free_list == NULL) {
        size_t page_size;

        /*
         * free_count must agree with free_list emptiness.
         */
        if (pool->free_count != 0u) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            return NULL;
        }

        page_size =
            vitte_pool_next_page_size(
                pool);

        if (page_size == 0u) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_LIMIT_EXCEEDED);

            return NULL;
        }

        if (vitte_pool_create_page(
                pool,
                page_size) == NULL) {
            return NULL;
        }
    }

    if (pool->free_count == 0u ||
        pool->free_list == NULL) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return NULL;
    }

    /*
     * Validate the head before dereferencing its intrusive next pointer.
     */
    node = pool->free_list;

    page =
        vitte_pool_find_page(
            pool,
            node);

    if (page == NULL) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return NULL;
    }

    if (!vitte_pool_page_index_of(
            pool,
            page,
            node,
            &index)) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return NULL;
    }

    if (vitte_pool_bitmap_get(
            page,
            index)) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return NULL;
    }

    if (page->live_count >=
            page->slot_count ||
        pool->live_count >=
            pool->capacity) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return NULL;
    }

    /*
     * Commit free-list removal only after the candidate has been validated.
     */
    if (vitte_pool_pop_free(
            pool) !=
        (void *)node) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return NULL;
    }

    --pool->free_count;

    if (!vitte_pool_bitmap_set(
            page,
            index)) {
        vitte_pool_push_free(
            pool,
            node);

        ++pool->free_count;

        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return NULL;
    }

    ++page->live_count;
    ++pool->live_count;

    if (pool->live_count >
        pool->peak_live_count) {
        pool->peak_live_count =
            pool->live_count;
    }

    pool->allocation_count =
        vitte_pool_saturating_increment(
            pool->allocation_count);

    if (pool->config.zero_on_allocate) {
        memset(
            node,
            0,
            pool->slot_size);
    }
#if VITTE_POOL_POISON_ALLOC
    else {
        memset(
            node,
            VITTE_POOL_ALLOC_PATTERN,
            pool->slot_size);
    }
#endif

    return node;
}

void *
vitte_pool_calloc(
    vitte_pool_t *pool)
{
    void *object;

    object =
        vitte_pool_alloc(
            pool);

    if (object == NULL) {
        return NULL;
    }

    /*
     * calloc() promises logical object zeroing. Padding remains an internal
     * implementation detail.
     */
    memset(
        object,
        0,
        pool->object_size);

    return object;
}

/* ========================================================================= */
/* Free                                                                      */
/* ========================================================================= */

bool
vitte_pool_free(
    vitte_pool_t *pool,
    void *pointer)
{
    vitte_pool_page_t *page;
    size_t index;

    if (!vitte_pool_magic_valid(pool)) {
        return false;
    }

    pool->last_error =
        VITTE_POOL_ERROR_NONE;

    if (pointer == NULL) {
        return true;
    }

    page =
        vitte_pool_find_page(
            pool,
            pointer);

    if (page == NULL) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_FOREIGN_POINTER);

        return false;
    }

    if (!vitte_pool_page_index_of(
            pool,
            page,
            pointer,
            &index)) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_MISALIGNED_POINTER);

        return false;
    }

    if (!vitte_pool_bitmap_get(
            page,
            index)) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_DOUBLE_FREE);

        return false;
    }

    if (page->live_count == 0u ||
        pool->live_count == 0u ||
        pool->free_count >=
            pool->capacity) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return false;
    }

    /*
     * Clearing the bitmap first makes the slot formally free before its
     * payload is repurposed as a free-list node.
     */
    if (!vitte_pool_bitmap_clear(
            page,
            index)) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return false;
    }

    --page->live_count;
    --pool->live_count;

#if VITTE_POOL_POISON_FREE
    memset(
        pointer,
        VITTE_POOL_FREE_PATTERN,
        pool->slot_size);
#endif

    vitte_pool_push_free(
        pool,
        pointer);

    ++pool->free_count;

    pool->free_operation_count =
        vitte_pool_saturating_increment(
            pool->free_operation_count);

    return true;
}

/* ========================================================================= */
/* Reset                                                                     */
/* ========================================================================= */

void
vitte_pool_reset(
    vitte_pool_t *pool)
{
    vitte_pool_page_t *page;
    uint64_t generation;
    size_t guard;

    if (!vitte_pool_magic_valid(pool)) {
        return;
    }

    pool->last_error =
        VITTE_POOL_ERROR_NONE;

    generation =
        vitte_pool_next_generation(
            pool->generation);

    page = pool->first_page;
    guard = 0u;

    /*
     * Validate enough of the page chain before mutating anything.
     */
    while (page != NULL) {
        if (!vitte_pool_page_layout_valid(
                pool,
                page)) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            return;
        }

        page = page->next;

        ++guard;

        if (guard > pool->page_count) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            return;
        }
    }

    pool->generation =
        generation;

    page = pool->first_page;

    while (page != NULL) {
#if VITTE_POOL_POISON_RESET
        size_t slots_bytes;

        if (vitte_pool_mul_overflow(
                page->slot_count,
                pool->slot_size,
                &slots_bytes)) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            return;
        }

        memset(
            vitte_pool_page_slots(page),
            VITTE_POOL_RESET_PATTERN,
            slots_bytes);
#endif

        vitte_pool_bitmap_clear_all(
            page);

        page->live_count = 0u;
        page->generation =
            generation;

        page = page->next;
    }

    pool->live_count = 0u;

    if (!vitte_pool_rebuild_free_list(
            pool)) {
        return;
    }

    if (pool->free_count !=
        pool->capacity) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return;
    }

    pool->last_error =
        VITTE_POOL_ERROR_NONE;
}

/* ========================================================================= */
/* Trim                                                                      */
/* ========================================================================= */

size_t
vitte_pool_trim(
    vitte_pool_t *pool)
{
    vitte_pool_page_t *page;
    size_t removed;
    size_t guard;
    size_t empty_count;
    vitte_pool_page_t *retained_empty;

    if (!vitte_pool_magic_valid(pool)) {
        return 0u;
    }

    pool->last_error =
        VITTE_POOL_ERROR_NONE;

    /*
     * Free-list entries point directly into pages. Rebuild only after all
     * removals; never dereference the old list while trimming.
     */
    pool->free_list = NULL;
    pool->free_count = 0u;

    empty_count = 0u;
    retained_empty = NULL;

    if (pool->config.retain_empty_pages) {
        /*
         * Keep one deterministic empty page: the first empty page in chain
         * order.
         */
        page = pool->first_page;
        guard = 0u;

        while (page != NULL) {
            if (!vitte_pool_page_layout_valid(
                    pool,
                    page)) {
                vitte_pool_set_error(
                    pool,
                    VITTE_POOL_ERROR_CORRUPTION);

                return 0u;
            }

            if (page->live_count == 0u) {
                ++empty_count;

                if (retained_empty == NULL) {
                    retained_empty = page;
                }
            }

            page = page->next;

            ++guard;

            if (guard > pool->page_count) {
                vitte_pool_set_error(
                    pool,
                    VITTE_POOL_ERROR_CORRUPTION);

                return 0u;
            }
        }
    }

    (void)empty_count;

    removed = 0u;
    page = pool->first_page;
    guard = 0u;

    while (page != NULL) {
        vitte_pool_page_t *next;

        if (!vitte_pool_page_magic_valid(
                page)) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            break;
        }

        next = page->next;

        if (page->live_count == 0u &&
            page != retained_empty) {
            size_t page_capacity;
            size_t page_reserved;

            page_capacity =
                page->slot_count;

            page_reserved =
                page->allocation_size;

            if (pool->capacity <
                    page_capacity ||
                pool->reserved_bytes <
                    page_reserved ||
                pool->page_count == 0u) {
                vitte_pool_set_error(
                    pool,
                    VITTE_POOL_ERROR_CORRUPTION);

                break;
            }

            if (!vitte_pool_unlink_page(
                    pool,
                    page)) {
                vitte_pool_set_error(
                    pool,
                    VITTE_POOL_ERROR_CORRUPTION);

                break;
            }

            pool->capacity -=
                page_capacity;

            pool->reserved_bytes -=
                page_reserved;

            vitte_pool_destroy_page_storage(
                page);

            ++removed;
        }

        page = next;

        ++guard;

        /*
         * page_count shrinks during traversal, so use the original-capacity
         * upper bound instead of the mutable page_count as traversal guard.
         */
        if (guard >
            pool->peak_page_count + 1u &&
            pool->peak_page_count !=
                SIZE_MAX) {
            vitte_pool_set_error(
                pool,
                VITTE_POOL_ERROR_CORRUPTION);

            break;
        }
    }

    if (!vitte_pool_rebuild_free_list(
            pool)) {
        return removed;
    }

    if (pool->live_count >
            pool->capacity ||
        pool->free_count !=
            pool->capacity -
            pool->live_count) {
        vitte_pool_set_error(
            pool,
            VITTE_POOL_ERROR_CORRUPTION);

        return removed;
    }

    pool->last_error =
        VITTE_POOL_ERROR_NONE;

    return removed;
}

void
vitte_pool_reset_and_trim(
    vitte_pool_t *pool)
{
    bool retain;

    if (!vitte_pool_magic_valid(pool)) {
        return;
    }

    vitte_pool_reset(
        pool);

    if (pool->last_error !=
        VITTE_POOL_ERROR_NONE) {
        return;
    }

    retain =
        pool->config.retain_empty_pages;

    /*
     * Explicit reset_and_trim means release every empty page.
     */
    pool->config.retain_empty_pages =
        false;

    (void)vitte_pool_trim(
        pool);

    pool->config.retain_empty_pages =
        retain;
}

/* ========================================================================= */
/* Ownership                                                                 */
/* ========================================================================= */

bool
vitte_pool_contains(
    const vitte_pool_t *pool,
    const void *pointer)
{
    return
        vitte_pool_find_page(
            pool,
            pointer) != NULL;
}

bool
vitte_pool_contains_object(
    const vitte_pool_t *pool,
    const void *pointer)
{
    vitte_pool_page_t *page;
    size_t index;

    if (!vitte_pool_magic_valid(pool) ||
        pointer == NULL) {
        return false;
    }

    page =
        vitte_pool_find_page(
            pool,
            pointer);

    if (page == NULL) {
        return false;
    }

    return
        vitte_pool_page_index_of(
            pool,
            page,
            pointer,
            &index);
}

bool
vitte_pool_is_live(
    const vitte_pool_t *pool,
    const void *pointer)
{
    vitte_pool_page_t *page;
    size_t index;

    if (!vitte_pool_magic_valid(pool) ||
        pointer == NULL) {
        return false;
    }

    page =
        vitte_pool_find_page(
            pool,
            pointer);

    if (page == NULL) {
        return false;
    }

    if (!vitte_pool_page_index_of(
            pool,
            page,
            pointer,
            &index)) {
        return false;
    }

    return
        vitte_pool_bitmap_get(
            page,
            index);
}

/* ========================================================================= */
/* Accessors                                                                 */
/* ========================================================================= */

bool
vitte_pool_is_valid(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(
            pool);
}

size_t
vitte_pool_object_size(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->object_size
            : 0u;
}

size_t
vitte_pool_slot_size(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->slot_size
            : 0u;
}

size_t
vitte_pool_alignment(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->alignment
            : 0u;
}

size_t
vitte_pool_capacity(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->capacity
            : 0u;
}

size_t
vitte_pool_live_count(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->live_count
            : 0u;
}

size_t
vitte_pool_free_count(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->free_count
            : 0u;
}

size_t
vitte_pool_page_count(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->page_count
            : 0u;
}

size_t
vitte_pool_reserved_bytes(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->reserved_bytes
            : 0u;
}

uint64_t
vitte_pool_generation(
    const vitte_pool_t *pool)
{
    return
        vitte_pool_magic_valid(pool)
            ? pool->generation
            : UINT64_C(0);
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_pool_stats_t
vitte_pool_stats(
    const vitte_pool_t *pool)
{
    vitte_pool_stats_t stats;

    memset(
        &stats,
        0,
        sizeof(stats));

    if (!vitte_pool_magic_valid(pool)) {
        return stats;
    }

    stats.object_size =
        pool->object_size;

    stats.slot_size =
        pool->slot_size;

    stats.alignment =
        pool->alignment;

    stats.page_count =
        pool->page_count;

    stats.peak_page_count =
        pool->peak_page_count;

    stats.capacity =
        pool->capacity;

    stats.live_count =
        pool->live_count;

    stats.peak_live_count =
        pool->peak_live_count;

    stats.free_count =
        pool->free_count;

    stats.reserved_bytes =
        pool->reserved_bytes;

    stats.peak_reserved_bytes =
        pool->peak_reserved_bytes;

    stats.allocation_count =
        pool->allocation_count;

    stats.free_operation_count =
        pool->free_operation_count;

    stats.generation =
        pool->generation;

    stats.max_objects =
        pool->config.max_objects;

    stats.max_reserved_bytes =
        pool->config.max_reserved_bytes;

    return stats;
}

/* ========================================================================= */
/* Page-chain cycle detection                                                */
/* ========================================================================= */

static bool
vitte_pool_page_chain_has_cycle(
    const vitte_pool_t *pool)
{
    const vitte_pool_page_t *slow;
    const vitte_pool_page_t *fast;
    size_t guard;

    slow = pool->first_page;
    fast = pool->first_page;
    guard = 0u;

    while (fast != NULL) {
        if (!vitte_pool_page_magic_valid(
                fast)) {
            return true;
        }

        fast = fast->next;

        if (fast == NULL) {
            return false;
        }

        if (!vitte_pool_page_magic_valid(
                fast)) {
            return true;
        }

        fast = fast->next;

        if (slow == NULL ||
            !vitte_pool_page_magic_valid(
                slow)) {
            return true;
        }

        slow = slow->next;

        if (slow != NULL &&
            slow == fast) {
            return true;
        }

        ++guard;

        if (guard > pool->page_count) {
            return true;
        }
    }

    return false;
}

/* ========================================================================= */
/* Deep validation                                                          */
/* ========================================================================= */

bool
vitte_pool_validate(
    const vitte_pool_t *pool)
{
    const vitte_pool_page_t *page;
    const vitte_pool_page_t *previous;

    const vitte_pool_free_node_t *node;

    size_t page_count;
    size_t capacity;
    size_t live_count;
    size_t reserved_bytes;
    size_t free_count;

    if (!vitte_pool_magic_valid(pool)) {
        return false;
    }

    if (pool->object_size == 0u ||
        pool->slot_size == 0u ||
        pool->alignment == 0u ||
        pool->generation == UINT64_C(0)) {
        return false;
    }

    if (!vitte_pool_is_power_of_two(
            pool->alignment)) {
        return false;
    }

    if (pool->alignment >
        _Alignof(max_align_t)) {
        return false;
    }

    if (pool->slot_size <
            pool->object_size ||
        pool->slot_size <
            sizeof(vitte_pool_free_node_t)) {
        return false;
    }

    if ((pool->slot_size %
         pool->alignment) != 0u) {
        return false;
    }

    if (!vitte_pool_config_validate(
            &pool->config)) {
        return false;
    }

    if ((pool->first_page == NULL) !=
        (pool->last_page == NULL)) {
        return false;
    }

    if (pool->first_page != NULL &&
        pool->first_page->previous != NULL) {
        return false;
    }

    if (pool->last_page != NULL &&
        pool->last_page->next != NULL) {
        return false;
    }

    if ((pool->page_count == 0u) !=
        (pool->first_page == NULL)) {
        return false;
    }

    if ((pool->free_count == 0u) !=
        (pool->free_list == NULL)) {
        return false;
    }

    if (vitte_pool_page_chain_has_cycle(
            pool)) {
        return false;
    }

    page_count = 0u;
    capacity = 0u;
    live_count = 0u;
    reserved_bytes = 0u;

    previous = NULL;
    page = pool->first_page;

    while (page != NULL) {
        size_t bitmap_live;

        if (!vitte_pool_page_layout_valid(
                pool,
                page)) {
            return false;
        }

        if (page->previous != previous) {
            return false;
        }

        if (page->next == page ||
            page->previous == page) {
            return false;
        }

        if (page->previous != NULL &&
            page->previous->next != page) {
            return false;
        }

        if (page->next != NULL &&
            page->next->previous != page) {
            return false;
        }

        bitmap_live =
            vitte_pool_bitmap_count_live(
                page);

        if (bitmap_live !=
            page->live_count) {
            return false;
        }

        if (page_count == SIZE_MAX ||
            capacity >
                SIZE_MAX -
                page->slot_count ||
            live_count >
                SIZE_MAX -
                page->live_count ||
            reserved_bytes >
                SIZE_MAX -
                page->allocation_size) {
            return false;
        }

        ++page_count;

        capacity +=
            page->slot_count;

        live_count +=
            page->live_count;

        reserved_bytes +=
            page->allocation_size;

        previous = page;
        page = page->next;

        if (page_count >
            pool->page_count) {
            return false;
        }
    }

    if (previous !=
        pool->last_page) {
        return false;
    }

    if (page_count !=
        pool->page_count ||
        capacity !=
            pool->capacity ||
        live_count !=
            pool->live_count ||
        reserved_bytes !=
            pool->reserved_bytes) {
        return false;
    }

    if (pool->live_count >
            pool->capacity ||
        pool->free_count >
            pool->capacity) {
        return false;
    }

    if (pool->free_count !=
        pool->capacity -
        pool->live_count) {
        return false;
    }

    if (pool->peak_live_count <
            pool->live_count ||
        pool->peak_page_count <
            pool->page_count ||
        pool->peak_reserved_bytes <
            pool->reserved_bytes) {
        return false;
    }

    if (pool->config.max_objects !=
            SIZE_MAX &&
        pool->capacity >
            pool->config.max_objects) {
        return false;
    }

    if (pool->config.max_reserved_bytes !=
            SIZE_MAX &&
        pool->reserved_bytes >
            pool->config.max_reserved_bytes) {
        return false;
    }

    /*
     * Validate free-list ownership before cycle traversal.
     *
     * Traversal is bounded by capacity, so malformed cycles cannot cause an
     * infinite loop.
     */
    free_count = 0u;
    node = pool->free_list;

    while (node != NULL) {
        vitte_pool_page_t *owner;
        size_t index;

        if (free_count >=
            pool->capacity) {
            return false;
        }

        owner =
            vitte_pool_find_page(
                pool,
                node);

        if (owner == NULL) {
            return false;
        }

        if (!vitte_pool_page_index_of(
                pool,
                owner,
                node,
                &index)) {
            return false;
        }

        if (vitte_pool_bitmap_get(
                owner,
                index)) {
            return false;
        }

        ++free_count;

        node = node->next;
    }

    if (free_count !=
        pool->free_count) {
        return false;
    }

    if (vitte_pool_free_list_has_cycle(
            pool)) {
        return false;
    }

    /*
     * Exhaustive free-slot membership validation.
     *
     * This intentionally has debug-oriented complexity. It proves:
     *
     *     - every bitmap-free slot occurs in the free-list
     *     - no free slot occurs more than once
     *     - no live slot occurs in the free-list
     *
     * Counting alone cannot prove those properties because one duplicated
     * entry could otherwise hide one omitted slot.
     */
    page = pool->first_page;

    while (page != NULL) {
        size_t index;

        for (index = 0u;
             index < page->slot_count;
             ++index) {
            const unsigned char *slot;
            const vitte_pool_free_node_t *cursor;
            size_t occurrences;
            size_t traversal;

            slot =
                vitte_pool_page_slot_const(
                    pool,
                    page,
                    index);

            if (slot == NULL) {
                return false;
            }

            occurrences = 0u;
            traversal = 0u;
            cursor = pool->free_list;

            while (cursor != NULL) {
                if (traversal >=
                    pool->free_count) {
                    return false;
                }

                if ((const void *)cursor ==
                    (const void *)slot) {
                    ++occurrences;

                    if (occurrences > 1u) {
                        return false;
                    }
                }

                cursor = cursor->next;
                ++traversal;
            }

            if (vitte_pool_bitmap_get(
                    page,
                    index)) {
                if (occurrences != 0u) {
                    return false;
                }
            } else {
                if (occurrences != 1u) {
                    return false;
                }
            }
        }

        page = page->next;
    }

    return true;
}

/* ========================================================================= */
/* End                                                                       */
/* ========================================================================= */