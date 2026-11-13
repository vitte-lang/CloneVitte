/*
 * Vitte Compiler
 * src/backend/c17/naming.c
 *
 * Canonical naming and symbol mangling for the ISO C17 backend.
 *
 * Goals:
 *
 *   - deterministic generated identifiers
 *   - collision-resistant mangling
 *   - strict C identifier validity
 *   - protection against C17 keywords
 *   - protection against reserved implementation identifiers
 *   - distinct backend namespaces
 *   - stable module qualification
 *   - generated type/global/function names
 *   - temporaries
 *   - labels
 *   - runtime symbols
 *   - synthetic/internal symbols
 *   - stable hashing
 *   - collision registry
 *
 * The generated namespace deliberately avoids identifiers beginning with:
 *
 *     __
 *     _A ... _Z
 *
 * Canonical generated prefix:
 *
 *     vitte_
 *
 * Encoding:
 *
 *     [A-Za-z0-9]  -> unchanged
 *     '_'          -> _u
 *     other byte   -> _xHH
 *
 * Escaping '_' is essential: without it, a literal source name "_x2D"
 * could collide with the encoded form of "-".
 *
 * This module operates on bytes. UTF-8 source identifiers therefore remain
 * deterministic without requiring locale-dependent Unicode APIs.
 */

#include "naming.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

#ifndef VITTE_C17_NAMING_MAGIC
#define VITTE_C17_NAMING_MAGIC UINT64_C(0x56495454454E414D)
#endif

#ifndef VITTE_C17_NAMING_DEAD_MAGIC
#define VITTE_C17_NAMING_DEAD_MAGIC UINT64_C(0x444541444E414D45)
#endif

#ifndef VITTE_C17_NAMING_PREFIX
#define VITTE_C17_NAMING_PREFIX "vitte_"
#endif

#ifndef VITTE_C17_NAMING_PREFIX_LENGTH
#define VITTE_C17_NAMING_PREFIX_LENGTH ((size_t)6u)
#endif

#ifndef VITTE_C17_NAMING_INITIAL_CAPACITY
#define VITTE_C17_NAMING_INITIAL_CAPACITY ((size_t)64u)
#endif

#ifndef VITTE_C17_NAMING_MAX_IDENTIFIER_BYTES
#define VITTE_C17_NAMING_MAX_IDENTIFIER_BYTES ((size_t)4096u)
#endif

#ifndef VITTE_C17_NAMING_MAX_GENERATED_BYTES
#define VITTE_C17_NAMING_MAX_GENERATED_BYTES ((size_t)(64u * 1024u))
#endif

#ifndef VITTE_C17_NAMING_FNV_OFFSET
#define VITTE_C17_NAMING_FNV_OFFSET UINT64_C(14695981039346656037)
#endif

#ifndef VITTE_C17_NAMING_FNV_PRIME
#define VITTE_C17_NAMING_FNV_PRIME UINT64_C(1099511628211)
#endif

/* ========================================================================= */
/* C17 keywords                                                              */
/* ========================================================================= */

static const char *const
vitte_c17_naming_keywords[] = {
    "_Alignas",
    "_Alignof",
    "_Atomic",
    "_Bool",
    "_Complex",
    "_Generic",
    "_Imaginary",
    "_Noreturn",
    "_Static_assert",
    "_Thread_local",

    "auto",
    "break",
    "case",
    "char",
    "const",
    "continue",
    "default",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "float",
    "for",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "register",
    "restrict",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "struct",
    "switch",
    "typedef",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while"
};

#define VITTE_C17_NAMING_KEYWORD_COUNT \
    (sizeof(vitte_c17_naming_keywords) / \
     sizeof(vitte_c17_naming_keywords[0]))

/* ========================================================================= */
/* Internal arithmetic                                                       */
/* ========================================================================= */

static bool
vitte_c17_naming_add_overflow(
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
vitte_c17_naming_mul_overflow(
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
vitte_c17_naming_saturating_add(
    size_t left,
    size_t right)
{
    size_t result;

    if (vitte_c17_naming_add_overflow(
            left,
            right,
            &result)) {
        return SIZE_MAX;
    }

    return result;
}

/* ========================================================================= */
/* Character helpers                                                         */
/* ========================================================================= */

static bool
vitte_c17_naming_is_ascii_lower(
    unsigned char character)
{
    return
        character >= (unsigned char)'a' &&
        character <= (unsigned char)'z';
}

static bool
vitte_c17_naming_is_ascii_upper(
    unsigned char character)
{
    return
        character >= (unsigned char)'A' &&
        character <= (unsigned char)'Z';
}

static bool
vitte_c17_naming_is_ascii_alpha(
    unsigned char character)
{
    return
        vitte_c17_naming_is_ascii_lower(character) ||
        vitte_c17_naming_is_ascii_upper(character);
}

static bool
vitte_c17_naming_is_ascii_digit(
    unsigned char character)
{
    return
        character >= (unsigned char)'0' &&
        character <= (unsigned char)'9';
}

static bool
vitte_c17_naming_is_identifier_start(
    unsigned char character)
{
    return
        vitte_c17_naming_is_ascii_alpha(character) ||
        character == (unsigned char)'_';
}

static bool
vitte_c17_naming_is_identifier_continue(
    unsigned char character)
{
    return
        vitte_c17_naming_is_identifier_start(character) ||
        vitte_c17_naming_is_ascii_digit(character);
}

static char
vitte_c17_naming_hex_digit(
    unsigned value)
{
    static const char digits[] =
        "0123456789ABCDEF";

    return digits[value & 0x0fu];
}

/* ========================================================================= */
/* Hashing                                                                   */
/* ========================================================================= */

uint64_t
vitte_c17_naming_hash_bytes(
    const void *data,
    size_t length)
{
    const unsigned char *bytes;
    uint64_t hash;
    size_t index;

    hash =
        VITTE_C17_NAMING_FNV_OFFSET;

    if (data == NULL) {
        return hash;
    }

    bytes =
        (const unsigned char *)data;

    for (index = 0u;
         index < length;
         ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= VITTE_C17_NAMING_FNV_PRIME;
    }

    return hash;
}

uint64_t
vitte_c17_naming_hash_string(
    const char *text,
    size_t length)
{
    return
        vitte_c17_naming_hash_bytes(
            text,
            length);
}

static uint64_t
vitte_c17_naming_hash_combine(
    uint64_t hash,
    const void *data,
    size_t length)
{
    const unsigned char *bytes;
    size_t index;

    if (data == NULL) {
        return hash;
    }

    bytes =
        (const unsigned char *)data;

    for (index = 0u;
         index < length;
         ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= VITTE_C17_NAMING_FNV_PRIME;
    }

    return hash;
}

/* ========================================================================= */
/* Keyword checks                                                            */
/* ========================================================================= */

bool
vitte_c17_naming_is_keyword(
    const char *identifier,
    size_t length)
{
    size_t index;

    if (identifier == NULL ||
        length == 0u) {
        return false;
    }

    for (index = 0u;
         index < VITTE_C17_NAMING_KEYWORD_COUNT;
         ++index) {
        const char *keyword;
        size_t keyword_length;

        keyword =
            vitte_c17_naming_keywords[index];

        keyword_length =
            strlen(keyword);

        if (keyword_length == length &&
            memcmp(
                keyword,
                identifier,
                length) == 0) {
            return true;
        }
    }

    return false;
}

/* ========================================================================= */
/* Reserved C identifiers                                                    */
/* ========================================================================= */

bool
vitte_c17_naming_is_reserved_identifier(
    const char *identifier,
    size_t length)
{
    unsigned char first;
    unsigned char second;

    if (identifier == NULL ||
        length == 0u) {
        return false;
    }

    first =
        (unsigned char)identifier[0];

    if (first != (unsigned char)'_') {
        return false;
    }

    /*
     * Any identifier beginning with "__" is reserved.
     */
    if (length >= 2u) {
        second =
            (unsigned char)identifier[1];

        if (second == (unsigned char)'_') {
            return true;
        }

        /*
         * _A ... _Z is reserved.
         */
        if (vitte_c17_naming_is_ascii_upper(
                second)) {
            return true;
        }
    }

    /*
     * Identifiers beginning with '_' have additional reservation rules at
     * file scope. The backend avoids this entire namespace for generated
     * external/file-scope identifiers.
     */
    return true;
}

/* ========================================================================= */
/* Identifier validation                                                     */
/* ========================================================================= */

bool
vitte_c17_naming_is_c_identifier(
    const char *identifier,
    size_t length)
{
    size_t index;

    if (identifier == NULL ||
        length == 0u) {
        return false;
    }

    if (!vitte_c17_naming_is_identifier_start(
            (unsigned char)identifier[0])) {
        return false;
    }

    for (index = 1u;
         index < length;
         ++index) {
        if (!vitte_c17_naming_is_identifier_continue(
                (unsigned char)identifier[index])) {
            return false;
        }
    }

    return true;
}

bool
vitte_c17_naming_is_safe_c_identifier(
    const char *identifier,
    size_t length)
{
    if (!vitte_c17_naming_is_c_identifier(
            identifier,
            length)) {
        return false;
    }

    if (vitte_c17_naming_is_keyword(
            identifier,
            length)) {
        return false;
    }

    if (vitte_c17_naming_is_reserved_identifier(
            identifier,
            length)) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* String ownership                                                          */
/* ========================================================================= */

static char *
vitte_c17_naming_copy_string(
    const char *text,
    size_t length)
{
    size_t allocation_size;
    char *copy;

    if (text == NULL) {
        return NULL;
    }

    if (vitte_c17_naming_add_overflow(
            length,
            1u,
            &allocation_size)) {
        return NULL;
    }

    copy =
        (char *)malloc(allocation_size);

    if (copy == NULL) {
        return NULL;
    }

    if (length != 0u) {
        memcpy(
            copy,
            text,
            length);
    }

    copy[length] = '\0';

    return copy;
}

/* ========================================================================= */
/* Error                                                                     */
/* ========================================================================= */

const char *
vitte_c17_naming_error_name(
    vitte_c17_naming_error_t error)
{
    switch (error) {
        case VITTE_C17_NAMING_ERROR_NONE:
            return "none";

        case VITTE_C17_NAMING_ERROR_INVALID_NAMING:
            return "invalid-naming";

        case VITTE_C17_NAMING_ERROR_INVALID_ARGUMENT:
            return "invalid-argument";

        case VITTE_C17_NAMING_ERROR_INVALID_KIND:
            return "invalid-kind";

        case VITTE_C17_NAMING_ERROR_INVALID_IDENTIFIER:
            return "invalid-identifier";

        case VITTE_C17_NAMING_ERROR_TOO_LONG:
            return "too-long";

        case VITTE_C17_NAMING_ERROR_OVERFLOW:
            return "overflow";

        case VITTE_C17_NAMING_ERROR_OUT_OF_MEMORY:
            return "out-of-memory";

        case VITTE_C17_NAMING_ERROR_COLLISION:
            return "collision";

        case VITTE_C17_NAMING_ERROR_CORRUPTION:
            return "corruption";

        case VITTE_C17_NAMING_ERROR_COUNT:
            return "count";

        default:
            return "unknown";
    }
}

static bool
vitte_c17_naming_fail(
    vitte_c17_naming_t *naming,
    vitte_c17_naming_error_t error)
{
    if (naming != NULL &&
        naming->magic ==
            VITTE_C17_NAMING_MAGIC) {
        naming->last_error = error;
    }

    return false;
}

/* ========================================================================= */
/* Name kind                                                                 */
/* ========================================================================= */

const char *
vitte_c17_name_kind_name(
    vitte_c17_name_kind_t kind)
{
    switch (kind) {
        case VITTE_C17_NAME_MODULE:
            return "module";

        case VITTE_C17_NAME_TYPE:
            return "type";

        case VITTE_C17_NAME_GLOBAL:
            return "global";

        case VITTE_C17_NAME_FUNCTION:
            return "function";

        case VITTE_C17_NAME_PARAMETER:
            return "parameter";

        case VITTE_C17_NAME_LOCAL:
            return "local";

        case VITTE_C17_NAME_TEMPORARY:
            return "temporary";

        case VITTE_C17_NAME_LABEL:
            return "label";

        case VITTE_C17_NAME_FIELD:
            return "field";

        case VITTE_C17_NAME_ENUMERATOR:
            return "enumerator";

        case VITTE_C17_NAME_RUNTIME:
            return "runtime";

        case VITTE_C17_NAME_INTERNAL:
            return "internal";

        case VITTE_C17_NAME_INVALID:
            return "invalid";

        case VITTE_C17_NAME_COUNT:
            return "count";

        default:
            return "invalid";
    }
}

/* ========================================================================= */
/* Kind tags                                                                 */
/* ========================================================================= */

static const char *
vitte_c17_naming_kind_tag(
    vitte_c17_name_kind_t kind,
    size_t *length)
{
    const char *tag;

    switch (kind) {
        case VITTE_C17_NAME_MODULE:
            tag = "m";
            break;

        case VITTE_C17_NAME_TYPE:
            tag = "t";
            break;

        case VITTE_C17_NAME_GLOBAL:
            tag = "g";
            break;

        case VITTE_C17_NAME_FUNCTION:
            tag = "f";
            break;

        case VITTE_C17_NAME_PARAMETER:
            tag = "p";
            break;

        case VITTE_C17_NAME_LOCAL:
            tag = "l";
            break;

        case VITTE_C17_NAME_TEMPORARY:
            tag = "tmp";
            break;

        case VITTE_C17_NAME_LABEL:
            tag = "label";
            break;

        case VITTE_C17_NAME_FIELD:
            tag = "field";
            break;

        case VITTE_C17_NAME_ENUMERATOR:
            tag = "enum";
            break;

        case VITTE_C17_NAME_RUNTIME:
            tag = "rt";
            break;

        case VITTE_C17_NAME_INTERNAL:
            tag = "internal";
            break;

        case VITTE_C17_NAME_INVALID:
        case VITTE_C17_NAME_COUNT:
        default:
            if (length != NULL) {
                *length = 0u;
            }

            return NULL;
    }

    if (length != NULL) {
        *length = strlen(tag);
    }

    return tag;
}

/* ========================================================================= */
/* Dynamic string builder                                                    */
/* ========================================================================= */

typedef struct vitte_c17_naming_builder {
    char *data;
    size_t length;
    size_t capacity;
} vitte_c17_naming_builder_t;

static void
vitte_c17_naming_builder_destroy(
    vitte_c17_naming_builder_t *builder)
{
    if (builder == NULL) {
        return;
    }

    free(builder->data);

    builder->data = NULL;
    builder->length = 0u;
    builder->capacity = 0u;
}

static bool
vitte_c17_naming_builder_reserve(
    vitte_c17_naming_builder_t *builder,
    size_t additional)
{
    size_t required;
    size_t capacity;
    char *replacement;

    if (builder == NULL) {
        return false;
    }

    if (vitte_c17_naming_add_overflow(
            builder->length,
            additional,
            &required) ||
        vitte_c17_naming_add_overflow(
            required,
            1u,
            &required)) {
        return false;
    }

    if (required >
        VITTE_C17_NAMING_MAX_GENERATED_BYTES) {
        return false;
    }

    if (required <= builder->capacity) {
        return true;
    }

    capacity =
        builder->capacity != 0u
            ? builder->capacity
            : (size_t)64u;

    while (capacity < required) {
        if (capacity >
            VITTE_C17_NAMING_MAX_GENERATED_BYTES / 2u) {
            capacity = required;
            break;
        }

        capacity *= 2u;
    }

    if (capacity >
        VITTE_C17_NAMING_MAX_GENERATED_BYTES) {
        return false;
    }

    replacement =
        (char *)realloc(
            builder->data,
            capacity);

    if (replacement == NULL) {
        return false;
    }

    builder->data = replacement;
    builder->capacity = capacity;

    return true;
}

static bool
vitte_c17_naming_builder_append(
    vitte_c17_naming_builder_t *builder,
    const char *text,
    size_t length)
{
    if (builder == NULL ||
        (text == NULL && length != 0u)) {
        return false;
    }

    if (!vitte_c17_naming_builder_reserve(
            builder,
            length)) {
        return false;
    }

    if (length != 0u) {
        memcpy(
            builder->data + builder->length,
            text,
            length);

        builder->length += length;
    }

    builder->data[builder->length] =
        '\0';

    return true;
}

static bool
vitte_c17_naming_builder_char(
    vitte_c17_naming_builder_t *builder,
    char character)
{
    return
        vitte_c17_naming_builder_append(
            builder,
            &character,
            1u);
}

/* ========================================================================= */
/* Integer formatting                                                        */
/* ========================================================================= */

static bool
vitte_c17_naming_builder_u64(
    vitte_c17_naming_builder_t *builder,
    uint64_t value)
{
    char buffer[32];
    size_t length;

    length = 0u;

    do {
        buffer[length++] =
            (char)(
                '0' +
                (value % UINT64_C(10)));

        value /= UINT64_C(10);
    } while (value != 0u);

    while (length != 0u) {
        --length;

        if (!vitte_c17_naming_builder_char(
                builder,
                buffer[length])) {
            return false;
        }
    }

    return true;
}

static bool
vitte_c17_naming_builder_hex64(
    vitte_c17_naming_builder_t *builder,
    uint64_t value)
{
    char buffer[16];
    size_t index;

    for (index = 0u;
         index < sizeof(buffer);
         ++index) {
        unsigned shift;

        shift =
            (unsigned)(
                (sizeof(buffer) - 1u - index) *
                4u);

        buffer[index] =
            vitte_c17_naming_hex_digit(
                (unsigned)(
                    (value >> shift) &
                    UINT64_C(0x0f)));
    }

    return
        vitte_c17_naming_builder_append(
            builder,
            buffer,
            sizeof(buffer));
}

/* ========================================================================= */
/* Collision-free byte encoding                                              */
/* ========================================================================= */

static bool
vitte_c17_naming_builder_encoded(
    vitte_c17_naming_builder_t *builder,
    const char *source,
    size_t length)
{
    size_t index;

    if (builder == NULL ||
        (source == NULL && length != 0u)) {
        return false;
    }

    for (index = 0u;
         index < length;
         ++index) {
        unsigned char byte;

        byte =
            (unsigned char)source[index];

        if (vitte_c17_naming_is_ascii_alpha(byte) ||
            vitte_c17_naming_is_ascii_digit(byte)) {
            if (!vitte_c17_naming_builder_char(
                    builder,
                    (char)byte)) {
                return false;
            }

            continue;
        }

        if (byte == (unsigned char)'_') {
            if (!vitte_c17_naming_builder_append(
                    builder,
                    "_u",
                    2u)) {
                return false;
            }

            continue;
        }

        if (!vitte_c17_naming_builder_append(
                builder,
                "_x",
                2u)) {
            return false;
        }

        if (!vitte_c17_naming_builder_char(
                builder,
                vitte_c17_naming_hex_digit(
                    (unsigned)byte >> 4u))) {
            return false;
        }

        if (!vitte_c17_naming_builder_char(
                builder,
                vitte_c17_naming_hex_digit(
                    (unsigned)byte))) {
            return false;
        }
    }

    return true;
}

/* ========================================================================= */
/* Registry growth                                                           */
/* ========================================================================= */

static bool
vitte_c17_naming_reserve_entries(
    vitte_c17_naming_t *naming,
    size_t minimum)
{
    size_t capacity;
    size_t bytes;
    vitte_c17_name_entry_t *replacement;

    if (minimum <= naming->entry_capacity) {
        return true;
    }

    capacity =
        naming->entry_capacity != 0u
            ? naming->entry_capacity
            : VITTE_C17_NAMING_INITIAL_CAPACITY;

    while (capacity < minimum) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = minimum;
            break;
        }

        capacity *= 2u;
    }

    if (vitte_c17_naming_mul_overflow(
            capacity,
            sizeof(naming->entries[0]),
            &bytes)) {
        return
            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_OVERFLOW);
    }

    replacement =
        (vitte_c17_name_entry_t *)
            realloc(
                naming->entries,
                bytes);

    if (replacement == NULL) {
        return
            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_OUT_OF_MEMORY);
    }

    naming->entries = replacement;
    naming->entry_capacity = capacity;

    return true;
}

/* ========================================================================= */
/* Entry lookup                                                              */
/* ========================================================================= */

static ptrdiff_t
vitte_c17_naming_find_generated_index(
    const vitte_c17_naming_t *naming,
    const char *generated,
    size_t generated_length)
{
    size_t index;

    for (index = 0u;
         index < naming->entry_count;
         ++index) {
        const vitte_c17_name_entry_t *entry;

        entry = &naming->entries[index];

        if (entry->generated_length ==
                generated_length &&
            memcmp(
                entry->generated,
                generated,
                generated_length) == 0) {
            return (ptrdiff_t)index;
        }
    }

    return -1;
}

static ptrdiff_t
vitte_c17_naming_find_source_index(
    const vitte_c17_naming_t *naming,
    vitte_c17_name_kind_t kind,
    uint64_t module_id,
    uint64_t source_id,
    const char *source,
    size_t source_length)
{
    size_t index;

    for (index = 0u;
         index < naming->entry_count;
         ++index) {
        const vitte_c17_name_entry_t *entry;

        entry = &naming->entries[index];

        if (entry->kind != kind ||
            entry->module_id != module_id ||
            entry->source_id != source_id ||
            entry->source_length != source_length) {
            continue;
        }

        if (source_length == 0u ||
            memcmp(
                entry->source,
                source,
                source_length) == 0) {
            return (ptrdiff_t)index;
        }
    }

    return -1;
}

/* ========================================================================= */
/* Validity                                                                  */
/* ========================================================================= */

bool
vitte_c17_naming_is_valid(
    const vitte_c17_naming_t *naming)
{
    if (naming == NULL) {
        return false;
    }

    if (naming->magic !=
        VITTE_C17_NAMING_MAGIC) {
        return false;
    }

    if (naming->entry_count >
        naming->entry_capacity) {
        return false;
    }

    if (naming->entry_count != 0u &&
        naming->entries == NULL) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Configuration                                                             */
/* ========================================================================= */

vitte_c17_naming_config_t
vitte_c17_naming_config_default(void)
{
    vitte_c17_naming_config_t config;

    memset(
        &config,
        0,
        sizeof(config));

    config.max_identifier_bytes =
        VITTE_C17_NAMING_MAX_IDENTIFIER_BYTES;

    config.max_generated_bytes =
        VITTE_C17_NAMING_MAX_GENERATED_BYTES;

    config.include_kind_tag = true;
    config.include_module_id = true;
    config.include_source_id = true;
    config.append_hash = true;
    config.reject_collisions = true;

    return config;
}

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

bool
vitte_c17_naming_init(
    vitte_c17_naming_t *naming,
    const vitte_c17_naming_config_t *config)
{
    vitte_c17_naming_config_t effective;

    if (naming == NULL) {
        return false;
    }

    effective =
        config != NULL
            ? *config
            : vitte_c17_naming_config_default();

    if (effective.max_identifier_bytes == 0u ||
        effective.max_generated_bytes == 0u) {
        return false;
    }

    memset(
        naming,
        0,
        sizeof(*naming));

    naming->magic =
        VITTE_C17_NAMING_MAGIC;

    naming->config = effective;

    naming->last_error =
        VITTE_C17_NAMING_ERROR_NONE;

    naming->next_temporary_id =
        UINT64_C(1);

    naming->next_label_id =
        UINT64_C(1);

    naming->next_internal_id =
        UINT64_C(1);

    return true;
}

void
vitte_c17_naming_destroy(
    vitte_c17_naming_t *naming)
{
    size_t index;

    if (!vitte_c17_naming_is_valid(
            naming)) {
        return;
    }

    for (index = 0u;
         index < naming->entry_count;
         ++index) {
        free(naming->entries[index].source);
        free(naming->entries[index].generated);
    }

    free(naming->entries);

    naming->entries = NULL;
    naming->entry_count = 0u;
    naming->entry_capacity = 0u;

    naming->magic =
        VITTE_C17_NAMING_DEAD_MAGIC;
}

/* ========================================================================= */
/* Reset                                                                     */
/* ========================================================================= */

void
vitte_c17_naming_reset(
    vitte_c17_naming_t *naming)
{
    size_t index;

    if (!vitte_c17_naming_is_valid(
            naming)) {
        return;
    }

    for (index = 0u;
         index < naming->entry_count;
         ++index) {
        free(naming->entries[index].source);
        free(naming->entries[index].generated);

        memset(
            &naming->entries[index],
            0,
            sizeof(naming->entries[index]));
    }

    naming->entry_count = 0u;

    naming->next_temporary_id =
        UINT64_C(1);

    naming->next_label_id =
        UINT64_C(1);

    naming->next_internal_id =
        UINT64_C(1);

    naming->collision_count = 0u;
    naming->generated_count = 0u;

    naming->last_error =
        VITTE_C17_NAMING_ERROR_NONE;
}

/* ========================================================================= */
/* Canonical mangling                                                        */
/* ========================================================================= */

static bool
vitte_c17_naming_build_name(
    vitte_c17_naming_t *naming,
    vitte_c17_name_kind_t kind,
    uint64_t module_id,
    uint64_t source_id,
    const char *source,
    size_t source_length,
    char **result,
    size_t *result_length)
{
    vitte_c17_naming_builder_t builder;
    const char *kind_tag;
    size_t kind_tag_length;
    uint64_t hash;

    if (result == NULL ||
        result_length == NULL) {
        return
            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_INVALID_ARGUMENT);
    }

    *result = NULL;
    *result_length = 0u;

    if (source == NULL ||
        source_length == 0u) {
        return
            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_INVALID_IDENTIFIER);
    }

    if (source_length >
        naming->config.max_identifier_bytes) {
        return
            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_TOO_LONG);
    }

    kind_tag =
        vitte_c17_naming_kind_tag(
            kind,
            &kind_tag_length);

    if (kind_tag == NULL) {
        return
            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_INVALID_KIND);
    }

    memset(
        &builder,
        0,
        sizeof(builder));

    if (!vitte_c17_naming_builder_append(
            &builder,
            VITTE_C17_NAMING_PREFIX,
            VITTE_C17_NAMING_PREFIX_LENGTH)) {
        goto allocation_failure;
    }

    if (naming->config.include_kind_tag) {
        if (!vitte_c17_naming_builder_append(
                &builder,
                kind_tag,
                kind_tag_length) ||
            !vitte_c17_naming_builder_char(
                &builder,
                '_')) {
            goto allocation_failure;
        }
    }

    if (naming->config.include_module_id) {
        if (!vitte_c17_naming_builder_append(
                &builder,
                "m",
                1u) ||
            !vitte_c17_naming_builder_u64(
                &builder,
                module_id) ||
            !vitte_c17_naming_builder_char(
                &builder,
                '_')) {
            goto allocation_failure;
        }
    }

    if (!vitte_c17_naming_builder_encoded(
            &builder,
            source,
            source_length)) {
        goto allocation_failure;
    }

    if (naming->config.include_source_id) {
        if (!vitte_c17_naming_builder_append(
                &builder,
                "_s",
                2u) ||
            !vitte_c17_naming_builder_u64(
                &builder,
                source_id)) {
            goto allocation_failure;
        }
    }

    if (naming->config.append_hash) {
        hash =
            VITTE_C17_NAMING_FNV_OFFSET;

        hash =
            vitte_c17_naming_hash_combine(
                hash,
                &kind,
                sizeof(kind));

        hash =
            vitte_c17_naming_hash_combine(
                hash,
                &module_id,
                sizeof(module_id));

        hash =
            vitte_c17_naming_hash_combine(
                hash,
                &source_id,
                sizeof(source_id));

        hash =
            vitte_c17_naming_hash_combine(
                hash,
                source,
                source_length);

        if (!vitte_c17_naming_builder_append(
                &builder,
                "_h",
                2u) ||
            !vitte_c17_naming_builder_hex64(
                &builder,
                hash)) {
            goto allocation_failure;
        }
    }

    if (builder.length >
            naming->config.max_generated_bytes ||
        builder.length >
            VITTE_C17_NAMING_MAX_GENERATED_BYTES) {
        vitte_c17_naming_builder_destroy(
            &builder);

        return
            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_TOO_LONG);
    }

    if (!vitte_c17_naming_is_safe_c_identifier(
            builder.data,
            builder.length)) {
        vitte_c17_naming_builder_destroy(
            &builder);

        return
            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_INVALID_IDENTIFIER);
    }

    *result = builder.data;
    *result_length = builder.length;

    return true;

allocation_failure:

    vitte_c17_naming_builder_destroy(
        &builder);

    return
        vitte_c17_naming_fail(
            naming,
            VITTE_C17_NAMING_ERROR_OUT_OF_MEMORY);
}

/* ========================================================================= */
/* Registry insertion                                                        */
/* ========================================================================= */

static const vitte_c17_name_entry_t *
vitte_c17_naming_register_owned(
    vitte_c17_naming_t *naming,
    vitte_c17_name_kind_t kind,
    uint64_t module_id,
    uint64_t source_id,
    const char *source,
    size_t source_length,
    char *generated,
    size_t generated_length)
{
    ptrdiff_t existing_source;
    ptrdiff_t existing_generated;
    vitte_c17_name_entry_t *entry;
    char *source_copy;

    existing_source =
        vitte_c17_naming_find_source_index(
            naming,
            kind,
            module_id,
            source_id,
            source,
            source_length);

    if (existing_source >= 0) {
        free(generated);

        return
            &naming->entries[
                (size_t)existing_source];
    }

    existing_generated =
        vitte_c17_naming_find_generated_index(
            naming,
            generated,
            generated_length);

    if (existing_generated >= 0) {
        const vitte_c17_name_entry_t *collision;

        collision =
            &naming->entries[
                (size_t)existing_generated];

        ++naming->collision_count;

        if (naming->config.reject_collisions) {
            free(generated);

            vitte_c17_naming_fail(
                naming,
                VITTE_C17_NAMING_ERROR_COLLISION);

            return NULL;
        }

        /*
         * Even with collision rejection disabled, never register two
         * semantically different entries with the same generated C name.
         *
         * Returning the existing symbol would silently alias definitions.
         */
        free(generated);

        naming->last_error =
            VITTE_C17_NAMING_ERROR_COLLISION;

        return collision;
    }

    if (naming->entry_count == SIZE_MAX) {
        free(generated);

        vitte_c17_naming_fail(
            naming,
            VITTE_C17_NAMING_ERROR_OVERFLOW);

        return NULL;
    }

    if (!vitte_c17_naming_reserve_entries(
            naming,
            naming->entry_count + 1u)) {
        free(generated);
        return NULL;
    }

    source_copy =
        vitte_c17_naming_copy_string(
            source,
            source_length);

    if (source_copy == NULL) {
        free(generated);

        vitte_c17_naming_fail(
            naming,
            VITTE_C17_NAMING_ERROR_OUT_OF_MEMORY);

        return NULL;
    }

    entry =
        &naming->entries[
            naming->entry_count];

    memset(
        entry,
        0,
        sizeof(*entry));

    entry->kind = kind;
    entry->module_id = module_id;
    entry->source_id = source_id;

    entry->source = source_copy;
    entry->source_length = source_length;

    entry->generated = generated;
    entry->generated_length =
        generated_length;

    entry->hash =
        vitte_c17_naming_hash_string(
            generated,
            generated_length);

    ++naming->entry_count;

    naming->generated_count =
        vitte_c17_naming_saturating_add(
            naming->generated_count,
            1u);

    naming->last_error =
        VITTE_C17_NAMING_ERROR_NONE;

    return entry;
}

/* ========================================================================= */
/* Public mangling                                                           */
/* ========================================================================= */

const vitte_c17_name_entry_t *
vitte_c17_naming_mangle(
    vitte_c17_naming_t *naming,
    vitte_c17_name_kind_t kind,
    uint64_t module_id,
    uint64_t source_id,
    const char *source,
    size_t source_length)
{
    ptrdiff_t existing;
    char *generated;
    size_t generated_length;

    if (!vitte_c17_naming_is_valid(
            naming)) {
        return NULL;
    }

    if (kind <= VITTE_C17_NAME_INVALID ||
        kind >= VITTE_C17_NAME_COUNT) {
        vitte_c17_naming_fail(
            naming,
            VITTE_C17_NAMING_ERROR_INVALID_KIND);

        return NULL;
    }

    if (source == NULL ||
        source_length == 0u) {
        vitte_c17_naming_fail(
            naming,
            VITTE_C17_NAMING_ERROR_INVALID_IDENTIFIER);

        return NULL;
    }

    existing =
        vitte_c17_naming_find_source_index(
            naming,
            kind,
            module_id,
            source_id,
            source,
            source_length);

    if (existing >= 0) {
        naming->last_error =
            VITTE_C17_NAMING_ERROR_NONE;

        return
            &naming->entries[
                (size_t)existing];
    }

    generated = NULL;
    generated_length = 0u;

    if (!vitte_c17_naming_build_name(
            naming,
            kind,
            module_id,
            source_id,
            source,
            source_length,
            &generated,
            &generated_length)) {
        return NULL;
    }

    return
        vitte_c17_naming_register_owned(
            naming,
            kind,
            module_id,
            source_id,
            source,
            source_length,
            generated,
            generated_length);
}

/* ========================================================================= */
/* Common symbol wrappers                                                    */
/* ========================================================================= */

const vitte_c17_name_entry_t *
vitte_c17_naming_module(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_MODULE,
            module_id,
            module_id,
            name,
            length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_type(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    uint64_t source_id,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_TYPE,
            module_id,
            source_id,
            name,
            length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_global(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    uint64_t source_id,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_GLOBAL,
            module_id,
            source_id,
            name,
            length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_function(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    uint64_t source_id,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_FUNCTION,
            module_id,
            source_id,
            name,
            length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_parameter(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    uint64_t source_id,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_PARAMETER,
            module_id,
            source_id,
            name,
            length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_local(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    uint64_t source_id,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_LOCAL,
            module_id,
            source_id,
            name,
            length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_field(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    uint64_t source_id,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_FIELD,
            module_id,
            source_id,
            name,
            length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_enumerator(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    uint64_t source_id,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_ENUMERATOR,
            module_id,
            source_id,
            name,
            length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_runtime(
    vitte_c17_naming_t *naming,
    const char *name,
    size_t length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_RUNTIME,
            UINT64_C(0),
            UINT64_C(0),
            name,
            length);
}

/* ========================================================================= */
/* Synthetic names                                                           */
/* ========================================================================= */

static const vitte_c17_name_entry_t *
vitte_c17_naming_generated_counter(
    vitte_c17_naming_t *naming,
    vitte_c17_name_kind_t kind,
    uint64_t module_id,
    uint64_t counter,
    const char *base,
    size_t base_length)
{
    return
        vitte_c17_naming_mangle(
            naming,
            kind,
            module_id,
            counter,
            base,
            base_length);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_temporary(
    vitte_c17_naming_t *naming,
    uint64_t module_id)
{
    uint64_t id;

    if (!vitte_c17_naming_is_valid(
            naming)) {
        return NULL;
    }

    id =
        naming->next_temporary_id;

    if (id == UINT64_MAX) {
        vitte_c17_naming_fail(
            naming,
            VITTE_C17_NAMING_ERROR_OVERFLOW);

        return NULL;
    }

    ++naming->next_temporary_id;

    return
        vitte_c17_naming_generated_counter(
            naming,
            VITTE_C17_NAME_TEMPORARY,
            module_id,
            id,
            "tmp",
            3u);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_label(
    vitte_c17_naming_t *naming,
    uint64_t module_id)
{
    uint64_t id;

    if (!vitte_c17_naming_is_valid(
            naming)) {
        return NULL;
    }

    id =
        naming->next_label_id;

    if (id == UINT64_MAX) {
        vitte_c17_naming_fail(
            naming,
            VITTE_C17_NAMING_ERROR_OVERFLOW);

        return NULL;
    }

    ++naming->next_label_id;

    return
        vitte_c17_naming_generated_counter(
            naming,
            VITTE_C17_NAME_LABEL,
            module_id,
            id,
            "label",
            5u);
}

const vitte_c17_name_entry_t *
vitte_c17_naming_internal(
    vitte_c17_naming_t *naming,
    uint64_t module_id,
    const char *purpose,
    size_t purpose_length)
{
    uint64_t id;

    if (!vitte_c17_naming_is_valid(
            naming)) {
        return NULL;
    }

    if (purpose == NULL ||
        purpose_length == 0u) {
        purpose = "internal";
        purpose_length = 8u;
    }

    id =
        naming->next_internal_id;

    if (id == UINT64_MAX) {
        vitte_c17_naming_fail(
            naming,
            VITTE_C17_NAMING_ERROR_OVERFLOW);

        return NULL;
    }

    ++naming->next_internal_id;

    return
        vitte_c17_naming_mangle(
            naming,
            VITTE_C17_NAME_INTERNAL,
            module_id,
            id,
            purpose,
            purpose_length);
}

/* ========================================================================= */
/* Lookup                                                                    */
/* ========================================================================= */

const vitte_c17_name_entry_t *
vitte_c17_naming_find_generated(
    const vitte_c17_naming_t *naming,
    const char *generated,
    size_t generated_length)
{
    ptrdiff_t index;

    if (!vitte_c17_naming_is_valid(
            naming) ||
        generated == NULL) {
        return NULL;
    }

    index =
        vitte_c17_naming_find_generated_index(
            naming,
            generated,
            generated_length);

    if (index < 0) {
        return NULL;
    }

    return
        &naming->entries[
            (size_t)index];
}

const vitte_c17_name_entry_t *
vitte_c17_naming_find_source(
    const vitte_c17_naming_t *naming,
    vitte_c17_name_kind_t kind,
    uint64_t module_id,
    uint64_t source_id,
    const char *source,
    size_t source_length)
{
    ptrdiff_t index;

    if (!vitte_c17_naming_is_valid(
            naming) ||
        source == NULL) {
        return NULL;
    }

    index =
        vitte_c17_naming_find_source_index(
            naming,
            kind,
            module_id,
            source_id,
            source,
            source_length);

    if (index < 0) {
        return NULL;
    }

    return
        &naming->entries[
            (size_t)index];
}

/* ========================================================================= */
/* Entry access                                                              */
/* ========================================================================= */

size_t
vitte_c17_naming_count(
    const vitte_c17_naming_t *naming)
{
    if (!vitte_c17_naming_is_valid(
            naming)) {
        return 0u;
    }

    return naming->entry_count;
}

const vitte_c17_name_entry_t *
vitte_c17_naming_at(
    const vitte_c17_naming_t *naming,
    size_t index)
{
    if (!vitte_c17_naming_is_valid(
            naming) ||
        index >= naming->entry_count) {
        return NULL;
    }

    return &naming->entries[index];
}

/* ========================================================================= */
/* Errors/accessors                                                          */
/* ========================================================================= */

vitte_c17_naming_error_t
vitte_c17_naming_last_error(
    const vitte_c17_naming_t *naming)
{
    if (!vitte_c17_naming_is_valid(
            naming)) {
        return
            VITTE_C17_NAMING_ERROR_INVALID_NAMING;
    }

    return naming->last_error;
}

void
vitte_c17_naming_clear_error(
    vitte_c17_naming_t *naming)
{
    if (!vitte_c17_naming_is_valid(
            naming)) {
        return;
    }

    naming->last_error =
        VITTE_C17_NAMING_ERROR_NONE;
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_c17_naming_stats_t
vitte_c17_naming_stats(
    const vitte_c17_naming_t *naming)
{
    vitte_c17_naming_stats_t stats;
    size_t index;

    memset(
        &stats,
        0,
        sizeof(stats));

    if (!vitte_c17_naming_is_valid(
            naming)) {
        return stats;
    }

    stats.entry_count =
        naming->entry_count;

    stats.entry_capacity =
        naming->entry_capacity;

    stats.generated_count =
        naming->generated_count;

    stats.collision_count =
        naming->collision_count;

    for (index = 0u;
         index < naming->entry_count;
         ++index) {
        const vitte_c17_name_entry_t *entry;

        entry =
            &naming->entries[index];

        stats.source_bytes =
            vitte_c17_naming_saturating_add(
                stats.source_bytes,
                entry->source_length);

        stats.generated_bytes =
            vitte_c17_naming_saturating_add(
                stats.generated_bytes,
                entry->generated_length);

        if (entry->generated_length >
            stats.maximum_generated_length) {
            stats.maximum_generated_length =
                entry->generated_length;
        }

        if (entry->kind >
                VITTE_C17_NAME_INVALID &&
            entry->kind <
                VITTE_C17_NAME_COUNT) {
            size_t kind_index;

            kind_index =
                (size_t)entry->kind;

            stats.by_kind[kind_index] =
                vitte_c17_naming_saturating_add(
                    stats.by_kind[kind_index],
                    1u);
        }
    }

    return stats;
}

/* ========================================================================= */
/* Deep validation                                                          */
/* ========================================================================= */

bool
vitte_c17_naming_validate(
    const vitte_c17_naming_t *naming)
{
    size_t index;
    size_t other;

    if (!vitte_c17_naming_is_valid(
            naming)) {
        return false;
    }

    if (naming->config.max_identifier_bytes == 0u ||
        naming->config.max_generated_bytes == 0u) {
        return false;
    }

    for (index = 0u;
         index < naming->entry_count;
         ++index) {
        const vitte_c17_name_entry_t *entry;

        entry =
            &naming->entries[index];

        if (entry->kind <=
                VITTE_C17_NAME_INVALID ||
            entry->kind >=
                VITTE_C17_NAME_COUNT) {
            return false;
        }

        if (entry->source == NULL ||
            entry->source_length == 0u) {
            return false;
        }

        if (entry->generated == NULL ||
            entry->generated_length == 0u) {
            return false;
        }

        if (entry->source_length >
            naming->config.max_identifier_bytes) {
            return false;
        }

        if (entry->generated_length >
            naming->config.max_generated_bytes) {
            return false;
        }

        if (!vitte_c17_naming_is_safe_c_identifier(
                entry->generated,
                entry->generated_length)) {
            return false;
        }

        if (entry->hash !=
            vitte_c17_naming_hash_string(
                entry->generated,
                entry->generated_length)) {
            return false;
        }

        for (other = index + 1u;
             other < naming->entry_count;
             ++other) {
            const vitte_c17_name_entry_t *candidate;

            candidate =
                &naming->entries[other];

            /*
             * No generated C identifier may alias another registry entry.
             */
            if (entry->generated_length ==
                    candidate->generated_length &&
                memcmp(
                    entry->generated,
                    candidate->generated,
                    entry->generated_length) == 0) {
                return false;
            }

            /*
             * No duplicate semantic key.
             */
            if (entry->kind ==
                    candidate->kind &&
                entry->module_id ==
                    candidate->module_id &&
                entry->source_id ==
                    candidate->source_id &&
                entry->source_length ==
                    candidate->source_length &&
                memcmp(
                    entry->source,
                    candidate->source,
                    entry->source_length) == 0) {
                return false;
            }
        }
    }

    return true;
}

/* ========================================================================= */
/* End                                                                       */
/* ========================================================================= */
