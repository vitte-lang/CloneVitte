/*
 * Vitte Compiler
 * src/lexer/lexer.c
 *
 * Lexer implementation.
 *
 * Public contract: lexer.h
 *
 * Goals:
 *   - deterministic ISO C17 lexer;
 *   - byte-accurate source spans;
 *   - line/column tracking;
 *   - UTF-8-aware identifier scanning;
 *   - complete Vitte keyword recognition;
 *   - integer and floating-point literals;
 *   - binary/octal/decimal/hexadecimal integers;
 *   - decimal and hexadecimal floating-point literals;
 *   - underscores in numeric literals;
 *   - strings and character literals;
 *   - escape validation;
 *   - line and nested block comments;
 *   - maximal-munch operators;
 *   - diagnostics with exact source location;
 *   - resource limits;
 *   - deterministic token fingerprinting;
 *   - statistics;
 *   - no filesystem access;
 *   - no parser dependency.
 */

#include "lexer.h"
#include "unicode/unicode.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Private constants                                                         */
/* ========================================================================= */

#define VITTE_LEXER_TAB_WIDTH ((size_t)4u)
#define VITTE_LEXER_MAX_UTF8_BYTES ((size_t)4u)

/* ========================================================================= */
/* Private helpers                                                           */
/* ========================================================================= */

static bool
vitte_lexer_size_add(size_t a, size_t b, size_t *out)
{
    if (out == NULL) {
        return false;
    }

    if (a > SIZE_MAX - b) {
        return false;
    }

    *out = a + b;
    return true;
}

static bool
vitte_lexer_size_mul(size_t a, size_t b, size_t *out)
{
    if (out == NULL) {
        return false;
    }

    if (a != 0u && b > SIZE_MAX / a) {
        return false;
    }

    *out = a * b;
    return true;
}

static uint64_t
vitte_lexer_u64_add_sat(uint64_t a, uint64_t b)
{
    if (UINT64_MAX - a < b) {
        return UINT64_MAX;
    }

    return a + b;
}

static uint64_t
vitte_lexer_hash_bytes(
    uint64_t hash,
    const void *data,
    size_t length)
{
    const unsigned char *bytes;
    size_t i;

    if (data == NULL && length != 0u) {
        return hash;
    }

    bytes = (const unsigned char *)data;

    for (i = 0u; i < length; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= VITTE_LEXER_FNV_PRIME;
    }

    return hash;
}

static uint64_t
vitte_lexer_hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int shift;

    for (shift = 0u; shift < 64u; shift += 8u) {
        hash ^= (value >> shift) & UINT64_C(0xff);
        hash *= VITTE_LEXER_FNV_PRIME;
    }

    return hash;
}

static bool
vitte_lexer_context_valid(const vitte_lexer_t *lexer)
{
    if (lexer == NULL) {
        return false;
    }

    if (lexer->magic != VITTE_LEXER_MAGIC) {
        return false;
    }

    if (lexer->state <= VITTE_LEXER_STATE_INVALID ||
        lexer->state >= VITTE_LEXER_STATE_DESTROYED) {
        return false;
    }

    if (lexer->source == NULL && lexer->source_length != 0u) {
        return false;
    }

    if (lexer->offset > lexer->source_length) {
        return false;
    }

    if (lexer->token_count > lexer->token_capacity) {
        return false;
    }

    if (lexer->token_count != 0u && lexer->tokens == NULL) {
        return false;
    }

    return true;
}

static bool
vitte_lexer_mutable(const vitte_lexer_t *lexer)
{
    return vitte_lexer_context_valid(lexer) &&
           lexer->state == VITTE_LEXER_STATE_READY;
}

static bool
vitte_lexer_fail(
    vitte_lexer_t *lexer,
    vitte_lexer_error_t error,
    size_t begin,
    size_t end)
{
    if (lexer != NULL && lexer->magic == VITTE_LEXER_MAGIC) {
        lexer->last_error = error;

        lexer->error_span.file_id = lexer->file_id;
        lexer->error_span.begin = begin;
        lexer->error_span.end = end;
        lexer->error_span.valid = begin <= end;

        lexer->stats.errors =
            vitte_lexer_u64_add_sat(
                lexer->stats.errors,
                UINT64_C(1));
    }

    return false;
}

static bool
vitte_lexer_eof(const vitte_lexer_t *lexer)
{
    return lexer == NULL || lexer->offset >= lexer->source_length;
}

static unsigned char
vitte_lexer_peek(const vitte_lexer_t *lexer, size_t lookahead)
{
    size_t position;

    if (lexer == NULL ||
        !vitte_lexer_size_add(lexer->offset, lookahead, &position) ||
        position >= lexer->source_length) {
        return 0u;
    }

    return (unsigned char)lexer->source[position];
}

static bool
vitte_lexer_starts_with(
    const vitte_lexer_t *lexer,
    const char *text,
    size_t length)
{
    if (lexer == NULL || text == NULL) {
        return false;
    }

    if (length > lexer->source_length - lexer->offset) {
        return false;
    }

    return memcmp(lexer->source + lexer->offset, text, length) == 0;
}

static void
vitte_lexer_advance_byte(vitte_lexer_t *lexer)
{
    unsigned char c;

    if (lexer == NULL || vitte_lexer_eof(lexer)) {
        return;
    }

    c = (unsigned char)lexer->source[lexer->offset];
    ++lexer->offset;

    if (c == (unsigned char)'\n') {
        ++lexer->line;
        lexer->column = 1u;
    } else if (c == (unsigned char)'\t') {
        size_t next;

        if (vitte_lexer_size_add(
                lexer->column,
                VITTE_LEXER_TAB_WIDTH,
                &next)) {
            lexer->column = next;
        } else {
            lexer->column = SIZE_MAX;
        }
    } else {
        if (lexer->column != SIZE_MAX) {
            ++lexer->column;
        }
    }
}

static void
vitte_lexer_advance_n(vitte_lexer_t *lexer, size_t count)
{
    size_t i;

    for (i = 0u; i < count && !vitte_lexer_eof(lexer); ++i) {
        vitte_lexer_advance_byte(lexer);
    }
}

static bool
vitte_lexer_is_ascii_identifier_start(unsigned char c)
{
    return c == (unsigned char)'_' ||
           (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
           (c >= (unsigned char)'a' && c <= (unsigned char)'z');
}

static bool
vitte_lexer_is_ascii_identifier_continue(unsigned char c)
{
    return vitte_lexer_is_ascii_identifier_start(c) ||
           (c >= (unsigned char)'0' && c <= (unsigned char)'9');
}

static bool
vitte_lexer_is_decimal_digit(unsigned char c)
{
    return c >= (unsigned char)'0' && c <= (unsigned char)'9';
}

static bool
vitte_lexer_is_binary_digit(unsigned char c)
{
    return c == (unsigned char)'0' ||
           c == (unsigned char)'1';
}

static bool
vitte_lexer_is_octal_digit(unsigned char c)
{
    return c >= (unsigned char)'0' &&
           c <= (unsigned char)'7';
}

static bool
vitte_lexer_is_hex_digit(unsigned char c)
{
    return vitte_lexer_is_decimal_digit(c) ||
           (c >= (unsigned char)'a' && c <= (unsigned char)'f') ||
           (c >= (unsigned char)'A' && c <= (unsigned char)'F');
}

static bool
vitte_lexer_is_space_no_newline(unsigned char c)
{
    return c == (unsigned char)' ' ||
           c == (unsigned char)'\t' ||
           c == (unsigned char)'\r' ||
           c == (unsigned char)'\f' ||
           c == (unsigned char)'\v';
}

/* ========================================================================= */
/* UTF-8                                                                     */
/* ========================================================================= */

static bool
vitte_lexer_utf8_decode(
    const char *data,
    size_t length,
    uint32_t *codepoint,
    size_t *consumed)
{
    const unsigned char *s;
    uint32_t cp;

    if (data == NULL ||
        codepoint == NULL ||
        consumed == NULL ||
        length == 0u) {
        return false;
    }

    s = (const unsigned char *)data;

    if (s[0] <= 0x7fu) {
        *codepoint = (uint32_t)s[0];
        *consumed = 1u;
        return true;
    }

    if (s[0] >= 0xc2u && s[0] <= 0xdfu) {
        if (length < 2u ||
            (s[1] & 0xc0u) != 0x80u) {
            return false;
        }

        cp =
            ((uint32_t)(s[0] & 0x1fu) << 6u) |
            (uint32_t)(s[1] & 0x3fu);

        *codepoint = cp;
        *consumed = 2u;
        return true;
    }

    if (s[0] >= 0xe0u && s[0] <= 0xefu) {
        if (length < 3u ||
            (s[1] & 0xc0u) != 0x80u ||
            (s[2] & 0xc0u) != 0x80u) {
            return false;
        }

        if (s[0] == 0xe0u && s[1] < 0xa0u) {
            return false;
        }

        if (s[0] == 0xedu && s[1] >= 0xa0u) {
            return false;
        }

        cp =
            ((uint32_t)(s[0] & 0x0fu) << 12u) |
            ((uint32_t)(s[1] & 0x3fu) << 6u) |
            (uint32_t)(s[2] & 0x3fu);

        *codepoint = cp;
        *consumed = 3u;
        return true;
    }

    if (s[0] >= 0xf0u && s[0] <= 0xf4u) {
        if (length < 4u ||
            (s[1] & 0xc0u) != 0x80u ||
            (s[2] & 0xc0u) != 0x80u ||
            (s[3] & 0xc0u) != 0x80u) {
            return false;
        }

        if (s[0] == 0xf0u && s[1] < 0x90u) {
            return false;
        }

        if (s[0] == 0xf4u && s[1] >= 0x90u) {
            return false;
        }

        cp =
            ((uint32_t)(s[0] & 0x07u) << 18u) |
            ((uint32_t)(s[1] & 0x3fu) << 12u) |
            ((uint32_t)(s[2] & 0x3fu) << 6u) |
            (uint32_t)(s[3] & 0x3fu);

        if (cp > UINT32_C(0x10ffff)) {
            return false;
        }

        *codepoint = cp;
        *consumed = 4u;
        return true;
    }

    return false;
}

static bool
vitte_lexer_unicode_identifier_start(uint32_t cp)
{
    return vitte_unicode_is_identifier_start(cp);
}

static bool
vitte_lexer_unicode_identifier_continue(uint32_t cp)
{
    return vitte_unicode_is_identifier_continue(cp);
}

/* ========================================================================= */
/* Token storage                                                             */
/* ========================================================================= */

static bool
vitte_lexer_reserve_tokens(
    vitte_lexer_t *lexer,
    size_t required)
{
    size_t capacity;
    size_t bytes;
    vitte_token_t *tokens;

    if (lexer == NULL) {
        return false;
    }

    if (required <= lexer->token_capacity) {
        return true;
    }

    capacity = lexer->token_capacity;

    if (capacity == 0u) {
        capacity = VITTE_LEXER_DEFAULT_INITIAL_CAPACITY;
    }

    while (capacity < required) {
        size_t doubled;

        if (!vitte_lexer_size_mul(
                capacity,
                (size_t)2u,
                &doubled) ||
            doubled <= capacity) {
            capacity = required;
            break;
        }

        capacity = doubled;
    }

    if (capacity > lexer->max_tokens) {
        capacity = lexer->max_tokens;
    }

    if (capacity < required) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_TOKEN_LIMIT,
            lexer->offset,
            lexer->offset);
    }

    if (!vitte_lexer_size_mul(
            capacity,
            sizeof(*tokens),
            &bytes)) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_OVERFLOW,
            lexer->offset,
            lexer->offset);
    }

    tokens =
        (vitte_token_t *)realloc(lexer->tokens, bytes);

    if (tokens == NULL) {
        lexer->stats.allocation_failures =
            vitte_lexer_u64_add_sat(
                lexer->stats.allocation_failures,
                UINT64_C(1));

        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_OUT_OF_MEMORY,
            lexer->offset,
            lexer->offset);
    }

    if (lexer->tokens == NULL) {
        lexer->stats.allocations =
            vitte_lexer_u64_add_sat(
                lexer->stats.allocations,
                UINT64_C(1));
    } else {
        lexer->stats.reallocations =
            vitte_lexer_u64_add_sat(
                lexer->stats.reallocations,
                UINT64_C(1));
    }

    lexer->tokens = tokens;
    lexer->token_capacity = capacity;

    return true;
}

static bool
vitte_lexer_emit(
    vitte_lexer_t *lexer,
    vitte_token_kind_t kind,
    size_t begin,
    size_t end,
    size_t line,
    size_t column)
{
    size_t required;
    vitte_token_t *token;

    if (lexer == NULL ||
        begin > end ||
        end > lexer->source_length) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_INVALID_TOKEN,
            begin,
            end);
    }

    if (lexer->token_count >= lexer->max_tokens) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_TOKEN_LIMIT,
            begin,
            end);
    }

    if (!vitte_lexer_size_add(
            lexer->token_count,
            1u,
            &required)) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_OVERFLOW,
            begin,
            end);
    }

    if (!vitte_lexer_reserve_tokens(lexer, required)) {
        return false;
    }

    token = &lexer->tokens[lexer->token_count];

    memset(token, 0, sizeof(*token));

    token->kind = kind;

    token->span.file_id = lexer->file_id;
    token->span.begin = begin;
    token->span.end = end;
    token->span.valid = true;

    token->line = line;
    token->column = column;

    token->lexeme =
        lexer->source != NULL
            ? lexer->source + begin
            : NULL;

    token->length = end - begin;

    lexer->token_count = required;

    lexer->stats.tokens_emitted =
        vitte_lexer_u64_add_sat(
            lexer->stats.tokens_emitted,
            UINT64_C(1));

    return true;
}

/* ========================================================================= */
/* Keywords                                                                  */
/* ========================================================================= */

typedef struct vitte_lexer_keyword_entry {
    const char *text;
    size_t length;
    vitte_token_kind_t kind;
} vitte_lexer_keyword_entry_t;

#define VITTE_KW(text_, kind_) \
    { (text_), sizeof(text_) - 1u, (kind_) }

static const vitte_lexer_keyword_entry_t vitte_lexer_keywords[] = {
    VITTE_KW("space", VITTE_TOKEN_KW_SPACE),
    VITTE_KW("use", VITTE_TOKEN_KW_USE),

    VITTE_KW("pub", VITTE_TOKEN_KW_PUB),

    VITTE_KW("const", VITTE_TOKEN_KW_CONST),
    VITTE_KW("static", VITTE_TOKEN_KW_STATIC),

    VITTE_KW("type", VITTE_TOKEN_KW_TYPE),
    VITTE_KW("opaque", VITTE_TOKEN_KW_OPAQUE),
    VITTE_KW("form", VITTE_TOKEN_KW_FORM),
    VITTE_KW("pick", VITTE_TOKEN_KW_PICK),

    VITTE_KW("trait", VITTE_TOKEN_KW_TRAIT),
    VITTE_KW("impl", VITTE_TOKEN_KW_IMPL),
    VITTE_KW("where", VITTE_TOKEN_KW_WHERE),

    VITTE_KW("proc", VITTE_TOKEN_KW_PROC),
    VITTE_KW("extern", VITTE_TOKEN_KW_EXTERN),
    VITTE_KW("intrinsic", VITTE_TOKEN_KW_EXTERN),

    VITTE_KW("macro", VITTE_TOKEN_KW_MACRO),
    VITTE_KW("comptime", VITTE_TOKEN_KW_COMPTIME),

    VITTE_KW("test", VITTE_TOKEN_KW_TEST),

    VITTE_KW("let", VITTE_TOKEN_KW_LET),
    VITTE_KW("mut", VITTE_TOKEN_KW_MUT),
    VITTE_KW("set", VITTE_TOKEN_KW_SET),

    VITTE_KW("return", VITTE_TOKEN_KW_RETURN),
    VITTE_KW("defer", VITTE_TOKEN_KW_DEFER),
    VITTE_KW("requires", VITTE_TOKEN_KW_REQUIRES),
    VITTE_KW("ensures", VITTE_TOKEN_KW_ENSURES),
    VITTE_KW("export", VITTE_TOKEN_KW_EXPORT),

    VITTE_KW("if", VITTE_TOKEN_KW_IF),
    VITTE_KW("else", VITTE_TOKEN_KW_ELSE),
    VITTE_KW("elif", VITTE_TOKEN_KW_ELIF),

    VITTE_KW("while", VITTE_TOKEN_KW_WHILE),
    VITTE_KW("loop", VITTE_TOKEN_KW_LOOP),
    VITTE_KW("for", VITTE_TOKEN_KW_FOR),
    VITTE_KW("in", VITTE_TOKEN_KW_IN),

    VITTE_KW("break", VITTE_TOKEN_KW_BREAK),
    VITTE_KW("continue", VITTE_TOKEN_KW_CONTINUE),

    VITTE_KW("match", VITTE_TOKEN_KW_MATCH),
    VITTE_KW("as", VITTE_TOKEN_KW_AS),

    VITTE_KW("async", VITTE_TOKEN_KW_ASYNC),
    VITTE_KW("await", VITTE_TOKEN_KW_AWAIT),

    VITTE_KW("unsafe", VITTE_TOKEN_KW_UNSAFE),
    VITTE_KW("asm", VITTE_TOKEN_KW_ASM),

    VITTE_KW("move", VITTE_TOKEN_KW_MOVE),
    VITTE_KW("ref", VITTE_TOKEN_KW_REF),
    VITTE_KW("self", VITTE_TOKEN_KW_SELF),

    VITTE_KW("and", VITTE_TOKEN_KW_AND),
    VITTE_KW("or", VITTE_TOKEN_KW_OR),
    VITTE_KW("not", VITTE_TOKEN_KW_NOT),

    VITTE_KW("true", VITTE_TOKEN_KW_TRUE),
    VITTE_KW("false", VITTE_TOKEN_KW_FALSE),
    VITTE_KW("null", VITTE_TOKEN_KW_NULL),

    VITTE_KW("assert", VITTE_TOKEN_KW_ASSERT),
    VITTE_KW("print", VITTE_TOKEN_KW_PRINT),
    VITTE_KW("sizeof", VITTE_TOKEN_KW_SIZEOF),
    VITTE_KW("alignof", VITTE_TOKEN_KW_ALIGNOF),

    /* Reserved EBNF words without a dedicated bootstrap production yet. */
    VITTE_KW("share", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("region", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("compiler", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("diagnostic", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("bench", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("entry", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("select", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("when", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("case", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("critical", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("owned", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("borrow", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("is", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("kernel", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("class", VITTE_TOKEN_KW_RESERVED),
    VITTE_KW("flags", VITTE_TOKEN_KW_RESERVED)
};

#undef VITTE_KW

static vitte_token_kind_t
vitte_lexer_keyword_kind(const char *text, size_t length)
{
    size_t i;

    for (i = 0u;
         i < sizeof(vitte_lexer_keywords) /
             sizeof(vitte_lexer_keywords[0]);
         ++i) {
        const vitte_lexer_keyword_entry_t *entry =
            &vitte_lexer_keywords[i];

        if (entry->length == length &&
            memcmp(entry->text, text, length) == 0) {
            return entry->kind;
        }
    }

    return VITTE_TOKEN_IDENTIFIER;
}

/* ========================================================================= */
/* Whitespace/comments                                                       */
/* ========================================================================= */

static void
vitte_lexer_skip_whitespace(vitte_lexer_t *lexer)
{
    for (;;) {
        unsigned char c;

        if (vitte_lexer_eof(lexer)) {
            return;
        }

        c = vitte_lexer_peek(lexer, 0u);

        if (vitte_lexer_is_space_no_newline(c) ||
            c == (unsigned char)'\n') {
            vitte_lexer_advance_byte(lexer);
            lexer->stats.whitespace_bytes =
                vitte_lexer_u64_add_sat(
                    lexer->stats.whitespace_bytes,
                    UINT64_C(1));
            continue;
        }

        return;
    }
}

static bool
vitte_lexer_skip_line_comment(vitte_lexer_t *lexer)
{
    size_t begin;

    if (!vitte_lexer_starts_with(lexer, "//", 2u)) {
        return false;
    }

    begin = lexer->offset;

    vitte_lexer_advance_n(lexer, 2u);

    while (!vitte_lexer_eof(lexer) &&
           vitte_lexer_peek(lexer, 0u) !=
               (unsigned char)'\n') {
        vitte_lexer_advance_byte(lexer);
    }

    lexer->stats.comments =
        vitte_lexer_u64_add_sat(
            lexer->stats.comments,
            UINT64_C(1));

    lexer->stats.comment_bytes =
        vitte_lexer_u64_add_sat(
            lexer->stats.comment_bytes,
            (uint64_t)(lexer->offset - begin));

    return true;
}

static bool
vitte_lexer_skip_block_comment(vitte_lexer_t *lexer)
{
    size_t begin;
    size_t depth;

    if (!vitte_lexer_starts_with(lexer, "/*", 2u)) {
        return false;
    }

    begin = lexer->offset;
    depth = 1u;

    vitte_lexer_advance_n(lexer, 2u);

    while (!vitte_lexer_eof(lexer)) {
        if (vitte_lexer_starts_with(lexer, "/*", 2u)) {
            if (depth == SIZE_MAX) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_OVERFLOW,
                    begin,
                    lexer->offset);
            }

            ++depth;

            if (depth > lexer->max_comment_depth) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_COMMENT_DEPTH,
                    begin,
                    lexer->offset);
            }

            vitte_lexer_advance_n(lexer, 2u);
            continue;
        }

        if (vitte_lexer_starts_with(lexer, "*/", 2u)) {
            vitte_lexer_advance_n(lexer, 2u);

            --depth;

            if (depth == 0u) {
                lexer->stats.comments =
                    vitte_lexer_u64_add_sat(
                        lexer->stats.comments,
                        UINT64_C(1));

                lexer->stats.comment_bytes =
                    vitte_lexer_u64_add_sat(
                        lexer->stats.comment_bytes,
                        (uint64_t)(lexer->offset - begin));

                return true;
            }

            continue;
        }

        vitte_lexer_advance_byte(lexer);
    }

    return vitte_lexer_fail(
        lexer,
        VITTE_LEXER_ERROR_UNTERMINATED_COMMENT,
        begin,
        lexer->offset);
}

static bool
vitte_lexer_skip_trivia(vitte_lexer_t *lexer)
{
    for (;;) {
        size_t before;

        before = lexer->offset;

        vitte_lexer_skip_whitespace(lexer);

        if (vitte_lexer_starts_with(lexer, "//", 2u)) {
            if (!vitte_lexer_skip_line_comment(lexer)) {
                return false;
            }
        } else if (vitte_lexer_starts_with(lexer, "/*", 2u)) {
            if (!vitte_lexer_skip_block_comment(lexer)) {
                return false;
            }
        }

        if (lexer->offset == before) {
            return true;
        }
    }
}

/* ========================================================================= */
/* Identifiers                                                               */
/* ========================================================================= */

static bool
vitte_lexer_scan_identifier(vitte_lexer_t *lexer)
{
    size_t begin;
    size_t line;
    size_t column;
    uint32_t cp;
    size_t consumed;
    vitte_token_kind_t kind;

    begin = lexer->offset;
    line = lexer->line;
    column = lexer->column;

    if (vitte_lexer_eof(lexer)) {
        return false;
    }

    if (vitte_lexer_peek(lexer, 0u) < 0x80u) {
        if (!vitte_lexer_is_ascii_identifier_start(
                vitte_lexer_peek(lexer, 0u))) {
            return false;
        }

        vitte_lexer_advance_byte(lexer);
    } else {
        if (!vitte_lexer_utf8_decode(
                lexer->source + lexer->offset,
                lexer->source_length - lexer->offset,
                &cp,
                &consumed) ||
            !vitte_lexer_unicode_identifier_start(cp)) {
            return false;
        }

        vitte_lexer_advance_n(lexer, consumed);
    }

    for (;;) {
        unsigned char c;

        if (vitte_lexer_eof(lexer)) {
            break;
        }

        c = vitte_lexer_peek(lexer, 0u);

        if (c < 0x80u) {
            if (!vitte_lexer_is_ascii_identifier_continue(c)) {
                break;
            }

            vitte_lexer_advance_byte(lexer);
            continue;
        }

        if (!vitte_lexer_utf8_decode(
                lexer->source + lexer->offset,
                lexer->source_length - lexer->offset,
                &cp,
                &consumed)) {
            return vitte_lexer_fail(
                lexer,
                VITTE_LEXER_ERROR_INVALID_UTF8,
                lexer->offset,
                lexer->offset + 1u);
        }

        if (!vitte_lexer_unicode_identifier_continue(cp)) {
            break;
        }

        vitte_lexer_advance_n(lexer, consumed);
    }

    kind =
        vitte_lexer_keyword_kind(
            lexer->source + begin,
            lexer->offset - begin);

    if (!vitte_lexer_emit(
            lexer,
            kind,
            begin,
            lexer->offset,
            line,
            column)) {
        return false;
    }

    if (kind == VITTE_TOKEN_IDENTIFIER) {
        lexer->stats.identifiers =
            vitte_lexer_u64_add_sat(
                lexer->stats.identifiers,
                UINT64_C(1));
    } else {
        lexer->stats.keywords =
            vitte_lexer_u64_add_sat(
                lexer->stats.keywords,
                UINT64_C(1));
    }

    return true;
}

/* ========================================================================= */
/* Numbers                                                                   */
/* ========================================================================= */

static bool
vitte_lexer_scan_digit_sequence(
    vitte_lexer_t *lexer,
    int base,
    bool *saw_digit)
{
    bool digit;
    bool previous_underscore;

    digit = false;
    previous_underscore = false;

    for (;;) {
        unsigned char c;
        bool valid;

        if (vitte_lexer_eof(lexer)) {
            break;
        }

        c = vitte_lexer_peek(lexer, 0u);

        switch (base) {
            case 2:
                valid = vitte_lexer_is_binary_digit(c);
                break;

            case 8:
                valid = vitte_lexer_is_octal_digit(c);
                break;

            case 10:
                valid = vitte_lexer_is_decimal_digit(c);
                break;

            case 16:
                valid = vitte_lexer_is_hex_digit(c);
                break;

            default:
                return false;
        }

        if (valid) {
            digit = true;
            previous_underscore = false;
            vitte_lexer_advance_byte(lexer);
            continue;
        }

        if (c == (unsigned char)'_') {
            if (!digit || previous_underscore) {
                return false;
            }

            previous_underscore = true;
            vitte_lexer_advance_byte(lexer);
            continue;
        }

        break;
    }

    if (previous_underscore) {
        return false;
    }

    if (saw_digit != NULL) {
        *saw_digit = digit;
    }

    return true;
}

static bool
vitte_lexer_integer_fits_u64(
    const vitte_lexer_t *lexer,
    size_t begin,
    size_t end,
    unsigned int base)
{
    size_t index;
    uint64_t value;

    if (lexer == NULL || lexer->source == NULL || begin > end ||
        end > lexer->source_length || base < 2u || base > 16u) {
        return false;
    }

    index = begin;
    if (base != 10u) {
        if (end - begin < 2u) {
            return false;
        }
        index += 2u;
    }
    if (index > end) {
        return false;
    }

    value = UINT64_C(0);
    for (; index < end; ++index) {
        unsigned int digit;
        unsigned char character;

        character = (unsigned char)lexer->source[index];
        if (character == (unsigned char)'_') {
            continue;
        }
        if (character >= (unsigned char)'0' &&
            character <= (unsigned char)'9') {
            digit = (unsigned int)(character - (unsigned char)'0');
        } else if (character >= (unsigned char)'a' &&
                   character <= (unsigned char)'f') {
            digit = (unsigned int)(character - (unsigned char)'a') + 10u;
        } else if (character >= (unsigned char)'A' &&
                   character <= (unsigned char)'F') {
            digit = (unsigned int)(character - (unsigned char)'A') + 10u;
        } else {
            return false;
        }

        if (digit >= base ||
            value > (UINT64_MAX - (uint64_t)digit) / (uint64_t)base) {
            return false;
        }
        value = value * (uint64_t)base + (uint64_t)digit;
    }

    return true;
}

static bool
vitte_lexer_scan_number(vitte_lexer_t *lexer)
{
    size_t begin;
    size_t line;
    size_t column;
    int base;
    bool is_float;
    bool digits;

    if (!vitte_lexer_is_decimal_digit(
            vitte_lexer_peek(lexer, 0u))) {
        return false;
    }

    begin = lexer->offset;
    line = lexer->line;
    column = lexer->column;

    base = 10;
    is_float = false;

    if (vitte_lexer_peek(lexer, 0u) == (unsigned char)'0') {
        unsigned char next;

        next = vitte_lexer_peek(lexer, 1u);

        if (next == (unsigned char)'x' ||
            next == (unsigned char)'X') {
            base = 16;
            vitte_lexer_advance_n(lexer, 2u);

            if (!vitte_lexer_scan_digit_sequence(
                    lexer,
                    16,
                    &digits) ||
                !digits) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_NUMBER,
                    begin,
                    lexer->offset);
            }

            if (vitte_lexer_peek(lexer, 0u) ==
                    (unsigned char)'.' &&
                vitte_lexer_peek(lexer, 1u) !=
                    (unsigned char)'.') {
                is_float = true;
                vitte_lexer_advance_byte(lexer);

                if (!vitte_lexer_scan_digit_sequence(
                        lexer,
                        16,
                        &digits)) {
                    return vitte_lexer_fail(
                        lexer,
                        VITTE_LEXER_ERROR_INVALID_NUMBER,
                        begin,
                        lexer->offset);
                }
            }

            if (vitte_lexer_peek(lexer, 0u) ==
                    (unsigned char)'p' ||
                vitte_lexer_peek(lexer, 0u) ==
                    (unsigned char)'P') {
                is_float = true;

                vitte_lexer_advance_byte(lexer);

                if (vitte_lexer_peek(lexer, 0u) ==
                        (unsigned char)'+' ||
                    vitte_lexer_peek(lexer, 0u) ==
                        (unsigned char)'-') {
                    vitte_lexer_advance_byte(lexer);
                }

                if (!vitte_lexer_scan_digit_sequence(
                        lexer,
                        10,
                        &digits) ||
                    !digits) {
                    return vitte_lexer_fail(
                        lexer,
                        VITTE_LEXER_ERROR_INVALID_NUMBER,
                        begin,
                        lexer->offset);
                }
            }

            if (is_float &&
                lexer->source[lexer->offset - 1u] != 'p' &&
                lexer->source[lexer->offset - 1u] != 'P') {
                /*
                 * Hex floats require a binary exponent in the C-like
                 * representation used by Vitte.
                 */
                size_t i;
                bool exponent_found;

                exponent_found = false;

                for (i = begin; i < lexer->offset; ++i) {
                    if (lexer->source[i] == 'p' ||
                        lexer->source[i] == 'P') {
                        exponent_found = true;
                        break;
                    }
                }

                if (!exponent_found) {
                    return vitte_lexer_fail(
                        lexer,
                        VITTE_LEXER_ERROR_INVALID_NUMBER,
                        begin,
                        lexer->offset);
                }
            }
        } else if (next == (unsigned char)'b' ||
                   next == (unsigned char)'B') {
            base = 2;
            vitte_lexer_advance_n(lexer, 2u);

            if (!vitte_lexer_scan_digit_sequence(
                    lexer,
                    2,
                    &digits) ||
                !digits) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_NUMBER,
                    begin,
                    lexer->offset);
            }
        } else if (next == (unsigned char)'o' ||
                   next == (unsigned char)'O') {
            base = 8;
            vitte_lexer_advance_n(lexer, 2u);

            if (!vitte_lexer_scan_digit_sequence(
                    lexer,
                    8,
                    &digits) ||
                !digits) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_NUMBER,
                    begin,
                    lexer->offset);
            }
        } else {
            if (!vitte_lexer_scan_digit_sequence(
                    lexer,
                    10,
                    &digits)) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_NUMBER,
                    begin,
                    lexer->offset);
            }
        }
    } else {
        if (!vitte_lexer_scan_digit_sequence(
                lexer,
                10,
                &digits)) {
            return vitte_lexer_fail(
                lexer,
                VITTE_LEXER_ERROR_INVALID_NUMBER,
                begin,
                lexer->offset);
        }
    }

    if (base == 10) {
        if (vitte_lexer_peek(lexer, 0u) ==
                (unsigned char)'.' &&
            vitte_lexer_peek(lexer, 1u) !=
                (unsigned char)'.') {
            is_float = true;
            vitte_lexer_advance_byte(lexer);

            if (!vitte_lexer_scan_digit_sequence(
                    lexer,
                    10,
                    &digits)) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_NUMBER,
                    begin,
                    lexer->offset);
            }
        }

        if (vitte_lexer_peek(lexer, 0u) ==
                (unsigned char)'e' ||
            vitte_lexer_peek(lexer, 0u) ==
                (unsigned char)'E') {
            is_float = true;
            vitte_lexer_advance_byte(lexer);

            if (vitte_lexer_peek(lexer, 0u) ==
                    (unsigned char)'+' ||
                vitte_lexer_peek(lexer, 0u) ==
                    (unsigned char)'-') {
                vitte_lexer_advance_byte(lexer);
            }

            if (!vitte_lexer_scan_digit_sequence(
                    lexer,
                    10,
                    &digits) ||
                !digits) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_NUMBER,
                    begin,
                    lexer->offset);
            }
        }
    }

    /*
     * Reject identifier continuation immediately following a number.
     *
     * This catches malformed forms such as:
     *
     *     123abc
     *     0b102
     *     0xGG
     */
    if (!vitte_lexer_eof(lexer)) {
        unsigned char c;

        c = vitte_lexer_peek(lexer, 0u);

        if (vitte_lexer_is_ascii_identifier_start(c) ||
            c >= 0x80u ||
            (base != 10 &&
             vitte_lexer_is_decimal_digit(c))) {
            return vitte_lexer_fail(
                lexer,
                VITTE_LEXER_ERROR_INVALID_NUMBER,
                begin,
                lexer->offset + 1u);
        }
    }

    if (!is_float &&
        !vitte_lexer_integer_fits_u64(
            lexer,
            begin,
            lexer->offset,
            (unsigned int)base)) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_OVERFLOW,
            begin,
            lexer->offset);
    }

    if (!vitte_lexer_emit(
            lexer,
            is_float
                ? VITTE_TOKEN_FLOAT_LITERAL
                : VITTE_TOKEN_INTEGER_LITERAL,
            begin,
            lexer->offset,
            line,
            column)) {
        return false;
    }

    if (is_float) {
        lexer->stats.float_literals =
            vitte_lexer_u64_add_sat(
                lexer->stats.float_literals,
                UINT64_C(1));
    } else {
        lexer->stats.integer_literals =
            vitte_lexer_u64_add_sat(
                lexer->stats.integer_literals,
                UINT64_C(1));
    }

    return true;
}

/* ========================================================================= */
/* Escape sequences                                                          */
/* ========================================================================= */

static bool
vitte_lexer_scan_hex_escape(
    vitte_lexer_t *lexer,
    size_t digits)
{
    size_t i;

    for (i = 0u; i < digits; ++i) {
        if (vitte_lexer_eof(lexer) ||
            !vitte_lexer_is_hex_digit(
                vitte_lexer_peek(lexer, 0u))) {
            return false;
        }

        vitte_lexer_advance_byte(lexer);
    }

    return true;
}

static bool
vitte_lexer_scan_escape(vitte_lexer_t *lexer)
{
    unsigned char c;

    if (vitte_lexer_eof(lexer) ||
        vitte_lexer_peek(lexer, 0u) !=
            (unsigned char)'\\') {
        return false;
    }

    vitte_lexer_advance_byte(lexer);

    if (vitte_lexer_eof(lexer)) {
        return false;
    }

    c = vitte_lexer_peek(lexer, 0u);

    switch (c) {
        case '\\':
        case '\'':
        case '"':
        case '0':
        case 'a':
        case 'b':
        case 'f':
        case 'n':
        case 'r':
        case 't':
        case 'v':
            vitte_lexer_advance_byte(lexer);
            return true;

        case 'x':
            vitte_lexer_advance_byte(lexer);
            return vitte_lexer_scan_hex_escape(lexer, 2u);

        case 'u':
            vitte_lexer_advance_byte(lexer);
            return vitte_lexer_scan_hex_escape(lexer, 4u);

        case 'U':
            vitte_lexer_advance_byte(lexer);
            return vitte_lexer_scan_hex_escape(lexer, 8u);

        default:
            return false;
    }
}

/* ========================================================================= */
/* Strings                                                                   */
/* ========================================================================= */

static bool
vitte_lexer_scan_string(vitte_lexer_t *lexer)
{
    size_t begin;
    size_t line;
    size_t column;

    if (vitte_lexer_peek(lexer, 0u) !=
        (unsigned char)'"') {
        return false;
    }

    begin = lexer->offset;
    line = lexer->line;
    column = lexer->column;

    vitte_lexer_advance_byte(lexer);

    while (!vitte_lexer_eof(lexer)) {
        unsigned char c;

        c = vitte_lexer_peek(lexer, 0u);

        if (c == (unsigned char)'"') {
            vitte_lexer_advance_byte(lexer);

            if (!vitte_lexer_emit(
                    lexer,
                    VITTE_TOKEN_STRING_LITERAL,
                    begin,
                    lexer->offset,
                    line,
                    column)) {
                return false;
            }

            lexer->stats.string_literals =
                vitte_lexer_u64_add_sat(
                    lexer->stats.string_literals,
                    UINT64_C(1));

            return true;
        }

        if (c == (unsigned char)'\n' ||
            c == (unsigned char)'\r') {
            return vitte_lexer_fail(
                lexer,
                VITTE_LEXER_ERROR_UNTERMINATED_STRING,
                begin,
                lexer->offset);
        }

        if (c == (unsigned char)'\\') {
            size_t escape_begin;

            escape_begin = lexer->offset;

            if (!vitte_lexer_scan_escape(lexer)) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_ESCAPE,
                    escape_begin,
                    lexer->offset);
            }

            continue;
        }

        if (c >= 0x80u) {
            uint32_t cp;
            size_t consumed;

            if (!vitte_lexer_utf8_decode(
                    lexer->source + lexer->offset,
                    lexer->source_length - lexer->offset,
                    &cp,
                    &consumed)) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_UTF8,
                    lexer->offset,
                    lexer->offset + 1u);
            }

            (void)cp;
            vitte_lexer_advance_n(lexer, consumed);
            continue;
        }

        vitte_lexer_advance_byte(lexer);
    }

    return vitte_lexer_fail(
        lexer,
        VITTE_LEXER_ERROR_UNTERMINATED_STRING,
        begin,
        lexer->offset);
}

/* ========================================================================= */
/* Character literals                                                        */
/* ========================================================================= */

static bool
vitte_lexer_scan_character(vitte_lexer_t *lexer)
{
    size_t begin;
    size_t line;
    size_t column;

    if (vitte_lexer_peek(lexer, 0u) !=
        (unsigned char)'\'') {
        return false;
    }

    begin = lexer->offset;
    line = lexer->line;
    column = lexer->column;

    vitte_lexer_advance_byte(lexer);

    if (vitte_lexer_eof(lexer) ||
        vitte_lexer_peek(lexer, 0u) ==
            (unsigned char)'\n' ||
        vitte_lexer_peek(lexer, 0u) ==
            (unsigned char)'\r' ||
        vitte_lexer_peek(lexer, 0u) ==
            (unsigned char)'\'') {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_INVALID_CHARACTER,
            begin,
            lexer->offset);
    }

    if (vitte_lexer_peek(lexer, 0u) ==
        (unsigned char)'\\') {
        size_t escape_begin;

        escape_begin = lexer->offset;

        if (!vitte_lexer_scan_escape(lexer)) {
            return vitte_lexer_fail(
                lexer,
                VITTE_LEXER_ERROR_INVALID_ESCAPE,
                escape_begin,
                lexer->offset);
        }
    } else if (vitte_lexer_peek(lexer, 0u) >= 0x80u) {
        uint32_t cp;
        size_t consumed;

        if (!vitte_lexer_utf8_decode(
                lexer->source + lexer->offset,
                lexer->source_length - lexer->offset,
                &cp,
                &consumed)) {
            return vitte_lexer_fail(
                lexer,
                VITTE_LEXER_ERROR_INVALID_UTF8,
                lexer->offset,
                lexer->offset + 1u);
        }

        (void)cp;
        vitte_lexer_advance_n(lexer, consumed);
    } else {
        vitte_lexer_advance_byte(lexer);
    }

    if (vitte_lexer_eof(lexer) ||
        vitte_lexer_peek(lexer, 0u) !=
            (unsigned char)'\'') {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_INVALID_CHARACTER,
            begin,
            lexer->offset);
    }

    vitte_lexer_advance_byte(lexer);

    if (!vitte_lexer_emit(
            lexer,
            VITTE_TOKEN_CHARACTER_LITERAL,
            begin,
            lexer->offset,
            line,
            column)) {
        return false;
    }

    lexer->stats.character_literals =
        vitte_lexer_u64_add_sat(
            lexer->stats.character_literals,
            UINT64_C(1));

    return true;
}

/* ========================================================================= */
/* Punctuation/operators                                                     */
/* ========================================================================= */

typedef struct vitte_lexer_operator_entry {
    const char *text;
    size_t length;
    vitte_token_kind_t kind;
} vitte_lexer_operator_entry_t;

#define VITTE_OP(text_, kind_) \
    { (text_), sizeof(text_) - 1u, (kind_) }

/*
 * Longest spellings must appear before their prefixes.
 */
static const vitte_lexer_operator_entry_t vitte_lexer_operators[] = {
    VITTE_OP("<<=", VITTE_TOKEN_SHIFT_LEFT_EQUAL),
    VITTE_OP(">>=", VITTE_TOKEN_SHIFT_RIGHT_EQUAL),
    VITTE_OP("..=", VITTE_TOKEN_DOT_DOT_EQUAL),

    VITTE_OP("::", VITTE_TOKEN_COLON_COLON),
    VITTE_OP("->", VITTE_TOKEN_ARROW),
    VITTE_OP("=>", VITTE_TOKEN_FAT_ARROW),

    VITTE_OP("==", VITTE_TOKEN_EQUAL_EQUAL),
    VITTE_OP("!=", VITTE_TOKEN_BANG_EQUAL),
    VITTE_OP("<=", VITTE_TOKEN_LESS_EQUAL),
    VITTE_OP(">=", VITTE_TOKEN_GREATER_EQUAL),

    VITTE_OP("+=", VITTE_TOKEN_PLUS_EQUAL),
    VITTE_OP("-=", VITTE_TOKEN_MINUS_EQUAL),
    VITTE_OP("*=", VITTE_TOKEN_STAR_EQUAL),
    VITTE_OP("/=", VITTE_TOKEN_SLASH_EQUAL),
    VITTE_OP("%=", VITTE_TOKEN_PERCENT_EQUAL),

    VITTE_OP("&=", VITTE_TOKEN_AMP_EQUAL),
    VITTE_OP("|=", VITTE_TOKEN_PIPE_EQUAL),
    VITTE_OP("^=", VITTE_TOKEN_CARET_EQUAL),

    VITTE_OP("<<", VITTE_TOKEN_SHIFT_LEFT),
    VITTE_OP(">>", VITTE_TOKEN_SHIFT_RIGHT),

    VITTE_OP("&&", VITTE_TOKEN_AMP_AMP),
    VITTE_OP("||", VITTE_TOKEN_PIPE_PIPE),

    VITTE_OP("??", VITTE_TOKEN_QUESTION_QUESTION),

    VITTE_OP("..", VITTE_TOKEN_DOT_DOT),

    VITTE_OP("(", VITTE_TOKEN_LEFT_PAREN),
    VITTE_OP(")", VITTE_TOKEN_RIGHT_PAREN),

    VITTE_OP("{", VITTE_TOKEN_LEFT_BRACE),
    VITTE_OP("}", VITTE_TOKEN_RIGHT_BRACE),

    VITTE_OP("[", VITTE_TOKEN_LEFT_BRACKET),
    VITTE_OP("]", VITTE_TOKEN_RIGHT_BRACKET),

    VITTE_OP(",", VITTE_TOKEN_COMMA),
    VITTE_OP(";", VITTE_TOKEN_SEMICOLON),

    VITTE_OP(":", VITTE_TOKEN_COLON),
    VITTE_OP(".", VITTE_TOKEN_DOT),

    VITTE_OP("!", VITTE_TOKEN_BANG),

    VITTE_OP("+", VITTE_TOKEN_PLUS),
    VITTE_OP("-", VITTE_TOKEN_MINUS),
    VITTE_OP("*", VITTE_TOKEN_STAR),
    VITTE_OP("/", VITTE_TOKEN_SLASH),
    VITTE_OP("%", VITTE_TOKEN_PERCENT),

    VITTE_OP("&", VITTE_TOKEN_AMP),
    VITTE_OP("|", VITTE_TOKEN_PIPE),
    VITTE_OP("^", VITTE_TOKEN_CARET),
    VITTE_OP("~", VITTE_TOKEN_TILDE),

    VITTE_OP("=", VITTE_TOKEN_EQUAL),

    VITTE_OP("<", VITTE_TOKEN_LESS),
    VITTE_OP(">", VITTE_TOKEN_GREATER),

    VITTE_OP("?", VITTE_TOKEN_QUESTION),
    VITTE_OP("#", VITTE_TOKEN_HASH)
};

#undef VITTE_OP

static bool
vitte_lexer_scan_operator(vitte_lexer_t *lexer)
{
    size_t i;
    size_t begin;
    size_t line;
    size_t column;

    begin = lexer->offset;
    line = lexer->line;
    column = lexer->column;

    for (i = 0u;
         i < sizeof(vitte_lexer_operators) /
             sizeof(vitte_lexer_operators[0]);
         ++i) {
        const vitte_lexer_operator_entry_t *entry =
            &vitte_lexer_operators[i];

        if (!vitte_lexer_starts_with(
                lexer,
                entry->text,
                entry->length)) {
            continue;
        }

        vitte_lexer_advance_n(lexer, entry->length);

        if (!vitte_lexer_emit(
                lexer,
                entry->kind,
                begin,
                lexer->offset,
                line,
                column)) {
            return false;
        }

        lexer->stats.operators =
            vitte_lexer_u64_add_sat(
                lexer->stats.operators,
                UINT64_C(1));

        return true;
    }

    return false;
}

/* ========================================================================= */
/* Public token names                                                        */
/* ========================================================================= */

const char *
vitte_token_kind_name(vitte_token_kind_t kind)
{
    switch (kind) {
        case VITTE_TOKEN_INVALID: return "invalid";
        case VITTE_TOKEN_EOF: return "eof";

        case VITTE_TOKEN_IDENTIFIER: return "identifier";
        case VITTE_TOKEN_INTEGER_LITERAL: return "integer_literal";
        case VITTE_TOKEN_FLOAT_LITERAL: return "float_literal";
        case VITTE_TOKEN_STRING_LITERAL: return "string_literal";
        case VITTE_TOKEN_CHARACTER_LITERAL: return "character_literal";

        case VITTE_TOKEN_KW_SPACE: return "space";
        case VITTE_TOKEN_KW_USE: return "use";
        case VITTE_TOKEN_KW_PUB: return "pub";
        case VITTE_TOKEN_KW_CONST: return "const";
        case VITTE_TOKEN_KW_STATIC: return "static";
        case VITTE_TOKEN_KW_TYPE: return "type";
        case VITTE_TOKEN_KW_OPAQUE: return "opaque";
        case VITTE_TOKEN_KW_FORM: return "form";
        case VITTE_TOKEN_KW_PICK: return "pick";
        case VITTE_TOKEN_KW_TRAIT: return "trait";
        case VITTE_TOKEN_KW_IMPL: return "impl";
        case VITTE_TOKEN_KW_DYN: return "dyn";
        case VITTE_TOKEN_KW_WHERE: return "where";
        case VITTE_TOKEN_KW_PROC: return "proc";
        case VITTE_TOKEN_KW_EXTERN: return "extern";
        case VITTE_TOKEN_KW_MACRO: return "macro";
        case VITTE_TOKEN_KW_COMPTIME: return "comptime";
        case VITTE_TOKEN_KW_TEST: return "test";
        case VITTE_TOKEN_KW_LET: return "let";
        case VITTE_TOKEN_KW_MUT: return "mut";
        case VITTE_TOKEN_KW_SET: return "set";
        case VITTE_TOKEN_KW_RETURN: return "return";
        case VITTE_TOKEN_KW_DEFER: return "defer";
        case VITTE_TOKEN_KW_REQUIRES: return "requires";
        case VITTE_TOKEN_KW_ENSURES: return "ensures";
        case VITTE_TOKEN_KW_EXPORT: return "export";
        case VITTE_TOKEN_KW_IF: return "if";
        case VITTE_TOKEN_KW_ELSE: return "else";
        case VITTE_TOKEN_KW_ELIF: return "elif";
        case VITTE_TOKEN_KW_WHILE: return "while";
        case VITTE_TOKEN_KW_LOOP: return "loop";
        case VITTE_TOKEN_KW_FOR: return "for";
        case VITTE_TOKEN_KW_IN: return "in";
        case VITTE_TOKEN_KW_BREAK: return "break";
        case VITTE_TOKEN_KW_CONTINUE: return "continue";
        case VITTE_TOKEN_KW_MATCH: return "match";
        case VITTE_TOKEN_KW_AS: return "as";
        case VITTE_TOKEN_KW_ASYNC: return "async";
        case VITTE_TOKEN_KW_AWAIT: return "await";
        case VITTE_TOKEN_KW_UNSAFE: return "unsafe";
        case VITTE_TOKEN_KW_ASM: return "asm";
        case VITTE_TOKEN_KW_MOVE: return "move";
        case VITTE_TOKEN_KW_REF: return "ref";
        case VITTE_TOKEN_KW_SELF: return "self";
        case VITTE_TOKEN_KW_AND: return "and";
        case VITTE_TOKEN_KW_OR: return "or";
        case VITTE_TOKEN_KW_NOT: return "not";
        case VITTE_TOKEN_KW_TRUE: return "true";
        case VITTE_TOKEN_KW_FALSE: return "false";
        case VITTE_TOKEN_KW_NULL: return "null";
        case VITTE_TOKEN_KW_ASSERT: return "assert";
        case VITTE_TOKEN_KW_MAP: return "map";
        case VITTE_TOKEN_KW_SIZEOF: return "sizeof";
        case VITTE_TOKEN_KW_ALIGNOF: return "alignof";
        case VITTE_TOKEN_KW_OFFSETOF: return "offsetof";
        case VITTE_TOKEN_KW_TYPEOF: return "typeof";
        case VITTE_TOKEN_KW_PRINT: return "print";
        case VITTE_TOKEN_KW_RESERVED: return "reserved_keyword";

        case VITTE_TOKEN_LEFT_PAREN: return "(";
        case VITTE_TOKEN_RIGHT_PAREN: return ")";
        case VITTE_TOKEN_LEFT_BRACE: return "{";
        case VITTE_TOKEN_RIGHT_BRACE: return "}";
        case VITTE_TOKEN_LEFT_BRACKET: return "[";
        case VITTE_TOKEN_RIGHT_BRACKET: return "]";
        case VITTE_TOKEN_COMMA: return ",";
        case VITTE_TOKEN_SEMICOLON: return ";";
        case VITTE_TOKEN_COLON: return ":";
        case VITTE_TOKEN_COLON_COLON: return "::";
        case VITTE_TOKEN_DOT: return ".";
        case VITTE_TOKEN_DOT_DOT: return "..";
        case VITTE_TOKEN_DOT_DOT_EQUAL: return "..=";
        case VITTE_TOKEN_ARROW: return "->";
        case VITTE_TOKEN_FAT_ARROW: return "=>";
        case VITTE_TOKEN_PLUS: return "+";
        case VITTE_TOKEN_MINUS: return "-";
        case VITTE_TOKEN_STAR: return "*";
        case VITTE_TOKEN_SLASH: return "/";
        case VITTE_TOKEN_PERCENT: return "%";
        case VITTE_TOKEN_AMP: return "&";
        case VITTE_TOKEN_PIPE: return "|";
        case VITTE_TOKEN_CARET: return "^";
        case VITTE_TOKEN_TILDE: return "~";
        case VITTE_TOKEN_BANG: return "!";
        case VITTE_TOKEN_EQUAL: return "=";
        case VITTE_TOKEN_EQUAL_EQUAL: return "==";
        case VITTE_TOKEN_BANG_EQUAL: return "!=";
        case VITTE_TOKEN_LESS: return "<";
        case VITTE_TOKEN_LESS_EQUAL: return "<=";
        case VITTE_TOKEN_GREATER: return ">";
        case VITTE_TOKEN_GREATER_EQUAL: return ">=";
        case VITTE_TOKEN_SHIFT_LEFT: return "<<";
        case VITTE_TOKEN_SHIFT_RIGHT: return ">>";
        case VITTE_TOKEN_PLUS_EQUAL: return "+=";
        case VITTE_TOKEN_MINUS_EQUAL: return "-=";
        case VITTE_TOKEN_STAR_EQUAL: return "*=";
        case VITTE_TOKEN_SLASH_EQUAL: return "/=";
        case VITTE_TOKEN_PERCENT_EQUAL: return "%=";
        case VITTE_TOKEN_AMP_EQUAL: return "&=";
        case VITTE_TOKEN_PIPE_EQUAL: return "|=";
        case VITTE_TOKEN_CARET_EQUAL: return "^=";
        case VITTE_TOKEN_SHIFT_LEFT_EQUAL: return "<<=";
        case VITTE_TOKEN_SHIFT_RIGHT_EQUAL: return ">>=";
        case VITTE_TOKEN_AMP_AMP: return "&&";
        case VITTE_TOKEN_PIPE_PIPE: return "||";
        case VITTE_TOKEN_QUESTION: return "?";
        case VITTE_TOKEN_QUESTION_QUESTION: return "??";
        case VITTE_TOKEN_AT: return "@";
        case VITTE_TOKEN_HASH: return "#";
        case VITTE_TOKEN_DOLLAR: return "$";

        case VITTE_TOKEN_COUNT: return "count";
    }

    return "unknown";
}

const char *
vitte_lexer_error_name(vitte_lexer_error_t error)
{
    switch (error) {
        case VITTE_LEXER_ERROR_NONE: return "none";
        case VITTE_LEXER_ERROR_INVALID_ARGUMENT: return "invalid_argument";
        case VITTE_LEXER_ERROR_INVALID_CONTEXT: return "invalid_context";
        case VITTE_LEXER_ERROR_INVALID_STATE: return "invalid_state";
        case VITTE_LEXER_ERROR_INVALID_UTF8: return "invalid_utf8";
        case VITTE_LEXER_ERROR_INVALID_TOKEN: return "invalid_token";
        case VITTE_LEXER_ERROR_INVALID_NUMBER: return "invalid_number";
        case VITTE_LEXER_ERROR_INVALID_ESCAPE: return "invalid_escape";
        case VITTE_LEXER_ERROR_INVALID_CHARACTER: return "invalid_character";
        case VITTE_LEXER_ERROR_UNTERMINATED_STRING: return "unterminated_string";
        case VITTE_LEXER_ERROR_UNTERMINATED_COMMENT: return "unterminated_comment";
        case VITTE_LEXER_ERROR_COMMENT_DEPTH: return "comment_depth";
        case VITTE_LEXER_ERROR_TOKEN_LIMIT: return "token_limit";
        case VITTE_LEXER_ERROR_SOURCE_TOO_LARGE: return "source_too_large";
        case VITTE_LEXER_ERROR_OVERFLOW: return "overflow";
        case VITTE_LEXER_ERROR_OUT_OF_MEMORY: return "out_of_memory";
        case VITTE_LEXER_ERROR_VALIDATION: return "validation";
        case VITTE_LEXER_ERROR_INTERNAL: return "internal";
        case VITTE_LEXER_ERROR_COUNT: return "count";
    }

    return "unknown";
}

const char *
vitte_lexer_state_name(vitte_lexer_state_t state)
{
    switch (state) {
        case VITTE_LEXER_STATE_INVALID: return "invalid";
        case VITTE_LEXER_STATE_READY: return "ready";
        case VITTE_LEXER_STATE_LEXING: return "lexing";
        case VITTE_LEXER_STATE_DONE: return "done";
        case VITTE_LEXER_STATE_FAILED: return "failed";
        case VITTE_LEXER_STATE_DESTROYED: return "destroyed";
        case VITTE_LEXER_STATE_COUNT: return "count";
    }

    return "unknown";
}

/* ========================================================================= */
/* Lex one token                                                             */
/* ========================================================================= */

static bool
vitte_lexer_scan_one(vitte_lexer_t *lexer)
{
    unsigned char c;
    size_t begin;
    size_t line;
    size_t column;

    if (!vitte_lexer_skip_trivia(lexer)) {
        return false;
    }

    if (vitte_lexer_eof(lexer)) {
        begin = lexer->offset;
        line = lexer->line;
        column = lexer->column;

        return vitte_lexer_emit(
            lexer,
            VITTE_TOKEN_EOF,
            begin,
            begin,
            line,
            column);
    }

    c = vitte_lexer_peek(lexer, 0u);

    if (vitte_lexer_is_ascii_identifier_start(c) ||
        c >= 0x80u) {
        if (c >= 0x80u) {
            uint32_t cp;
            size_t consumed;

            if (!vitte_lexer_utf8_decode(
                    lexer->source + lexer->offset,
                    lexer->source_length - lexer->offset,
                    &cp,
                    &consumed)) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_UTF8,
                    lexer->offset,
                    lexer->offset + 1u);
            }

            if (!vitte_lexer_unicode_identifier_start(cp)) {
                return vitte_lexer_fail(
                    lexer,
                    VITTE_LEXER_ERROR_INVALID_TOKEN,
                    lexer->offset,
                    lexer->offset + consumed);
            }
        }

        return vitte_lexer_scan_identifier(lexer);
    }

    if (vitte_lexer_is_decimal_digit(c)) {
        return vitte_lexer_scan_number(lexer);
    }

    if (c == (unsigned char)'"') {
        return vitte_lexer_scan_string(lexer);
    }

    if (c == (unsigned char)'\'') {
        return vitte_lexer_scan_character(lexer);
    }

    if (vitte_lexer_scan_operator(lexer)) {
        return true;
    }

    return vitte_lexer_fail(
        lexer,
        VITTE_LEXER_ERROR_INVALID_TOKEN,
        lexer->offset,
        lexer->offset + 1u);
}

/* ========================================================================= */
/* Lifecycle                                                                 */
/* ========================================================================= */

bool
vitte_lexer_init(
    vitte_lexer_t *lexer,
    const char *source,
    size_t source_length,
    uint32_t file_id)
{
    if (lexer == NULL ||
        (source == NULL && source_length != 0u)) {
        return false;
    }

    memset(lexer, 0, sizeof(*lexer));

    lexer->magic = VITTE_LEXER_MAGIC;
    lexer->state = VITTE_LEXER_STATE_READY;
    lexer->last_error = VITTE_LEXER_ERROR_NONE;

    lexer->source = source;
    lexer->source_length = source_length;
    lexer->file_id = file_id;

    lexer->offset = 0u;
    lexer->line = 1u;
    lexer->column = 1u;

    lexer->max_tokens = VITTE_LEXER_DEFAULT_MAX_TOKENS;
    lexer->max_source_bytes = VITTE_LEXER_DEFAULT_MAX_SOURCE_BYTES;
    lexer->max_comment_depth = VITTE_LEXER_DEFAULT_MAX_COMMENT_DEPTH;

    lexer->generation = UINT64_C(1);

    lexer->error_span.file_id = file_id;
    lexer->error_span.begin = 0u;
    lexer->error_span.end = 0u;
    lexer->error_span.valid = false;

    if (source_length > lexer->max_source_bytes) {
        lexer->state = VITTE_LEXER_STATE_FAILED;
        lexer->last_error = VITTE_LEXER_ERROR_SOURCE_TOO_LARGE;
        return false;
    }

    return true;
}

void
vitte_lexer_destroy(vitte_lexer_t *lexer)
{
    if (lexer == NULL) {
        return;
    }

    if (lexer->magic == VITTE_LEXER_MAGIC) {
        free(lexer->tokens);
    }

    memset(lexer, 0, sizeof(*lexer));

    lexer->magic = VITTE_LEXER_DEAD_MAGIC;
    lexer->state = VITTE_LEXER_STATE_DESTROYED;
}

bool
vitte_lexer_reset(
    vitte_lexer_t *lexer,
    const char *source,
    size_t source_length,
    uint32_t file_id)
{
    size_t max_tokens;
    size_t max_source_bytes;
    size_t max_comment_depth;
    uint64_t generation;

    if (lexer == NULL ||
        lexer->magic != VITTE_LEXER_MAGIC ||
        (source == NULL && source_length != 0u)) {
        return false;
    }

    max_tokens = lexer->max_tokens;
    max_source_bytes = lexer->max_source_bytes;
    max_comment_depth = lexer->max_comment_depth;
    generation = lexer->generation;

    free(lexer->tokens);

    lexer->tokens = NULL;
    lexer->token_count = 0u;
    lexer->token_capacity = 0u;

    memset(&lexer->stats, 0, sizeof(lexer->stats));

    lexer->source = source;
    lexer->source_length = source_length;
    lexer->file_id = file_id;

    lexer->offset = 0u;
    lexer->line = 1u;
    lexer->column = 1u;

    lexer->max_tokens = max_tokens;
    lexer->max_source_bytes = max_source_bytes;
    lexer->max_comment_depth = max_comment_depth;

    lexer->state = VITTE_LEXER_STATE_READY;
    lexer->last_error = VITTE_LEXER_ERROR_NONE;

    lexer->error_span.file_id = file_id;
    lexer->error_span.begin = 0u;
    lexer->error_span.end = 0u;
    lexer->error_span.valid = false;

    lexer->generation =
        generation == UINT64_MAX
            ? UINT64_MAX
            : generation + UINT64_C(1);

    if (source_length > max_source_bytes) {
        lexer->state = VITTE_LEXER_STATE_FAILED;
        lexer->last_error = VITTE_LEXER_ERROR_SOURCE_TOO_LARGE;
        return false;
    }

    return true;
}

bool
vitte_lexer_is_valid(const vitte_lexer_t *lexer)
{
    return vitte_lexer_context_valid(lexer);
}

/* ========================================================================= */
/* Lexing                                                                    */
/* ========================================================================= */

bool
vitte_lexer_run(vitte_lexer_t *lexer)
{
    if (!vitte_lexer_mutable(lexer)) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_INVALID_STATE,
            0u,
            0u);
    }

    if (lexer->source_length > lexer->max_source_bytes) {
        lexer->state = VITTE_LEXER_STATE_FAILED;

        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_SOURCE_TOO_LARGE,
            0u,
            lexer->source_length);
    }

    lexer->state = VITTE_LEXER_STATE_LEXING;

    lexer->stats.runs =
        vitte_lexer_u64_add_sat(
            lexer->stats.runs,
            UINT64_C(1));

    for (;;) {
        size_t before;
        size_t previous_count;

        before = lexer->offset;
        previous_count = lexer->token_count;

        if (!vitte_lexer_scan_one(lexer)) {
            lexer->state = VITTE_LEXER_STATE_FAILED;
            lexer->stats.failed_runs =
                vitte_lexer_u64_add_sat(
                    lexer->stats.failed_runs,
                    UINT64_C(1));
            return false;
        }

        if (lexer->token_count > previous_count &&
            lexer->tokens[lexer->token_count - 1u].kind ==
                VITTE_TOKEN_EOF) {
            break;
        }

        if (lexer->offset <= before) {
            lexer->state = VITTE_LEXER_STATE_FAILED;

            vitte_lexer_fail(
                lexer,
                VITTE_LEXER_ERROR_INTERNAL,
                before,
                lexer->offset);

            lexer->stats.failed_runs =
                vitte_lexer_u64_add_sat(
                    lexer->stats.failed_runs,
                    UINT64_C(1));

            return false;
        }
    }

    lexer->state = VITTE_LEXER_STATE_DONE;
    lexer->last_error = VITTE_LEXER_ERROR_NONE;

    lexer->stats.source_bytes =
        vitte_lexer_u64_add_sat(
            lexer->stats.source_bytes,
            (uint64_t)lexer->source_length);

    return true;
}

/* ========================================================================= */
/* Token access                                                              */
/* ========================================================================= */

const vitte_token_t *
vitte_lexer_token_at(
    const vitte_lexer_t *lexer,
    size_t index)
{
    if (!vitte_lexer_context_valid(lexer) ||
        index >= lexer->token_count) {
        return NULL;
    }

    return &lexer->tokens[index];
}

size_t
vitte_lexer_token_count(const vitte_lexer_t *lexer)
{
    if (!vitte_lexer_context_valid(lexer)) {
        return 0u;
    }

    return lexer->token_count;
}

/* ========================================================================= */
/* Token text                                                                */
/* ========================================================================= */

bool
vitte_token_text_equal(
    const vitte_token_t *token,
    const char *text,
    size_t length)
{
    if (token == NULL ||
        (text == NULL && length != 0u) ||
        token->length != length) {
        return false;
    }

    if (length == 0u) {
        return true;
    }

    if (token->lexeme == NULL) {
        return false;
    }

    return memcmp(token->lexeme, text, length) == 0;
}

/* ========================================================================= */
/* Validation                                                                */
/* ========================================================================= */

bool
vitte_lexer_validate(vitte_lexer_t *lexer)
{
    size_t i;
    size_t previous_end;

    if (!vitte_lexer_context_valid(lexer)) {
        return false;
    }

    lexer->stats.validation_runs =
        vitte_lexer_u64_add_sat(
            lexer->stats.validation_runs,
            UINT64_C(1));

    previous_end = 0u;

    for (i = 0u; i < lexer->token_count; ++i) {
        const vitte_token_t *token;

        token = &lexer->tokens[i];

        if (token->kind <= VITTE_TOKEN_INVALID ||
            token->kind >= VITTE_TOKEN_COUNT) {
            goto failure;
        }

        if (!token->span.valid ||
            token->span.file_id != lexer->file_id ||
            token->span.begin > token->span.end ||
            token->span.end > lexer->source_length) {
            goto failure;
        }

        if (token->span.begin < previous_end) {
            goto failure;
        }

        if (token->length !=
            token->span.end - token->span.begin) {
            goto failure;
        }

        if (token->lexeme !=
            (lexer->source != NULL
                ? lexer->source + token->span.begin
                : NULL)) {
            goto failure;
        }

        if (token->line == 0u ||
            token->column == 0u) {
            goto failure;
        }

        if (token->kind == VITTE_TOKEN_EOF) {
            if (i + 1u != lexer->token_count ||
                token->span.begin != lexer->source_length ||
                token->span.end != lexer->source_length ||
                token->length != 0u) {
                goto failure;
            }
        }

        previous_end = token->span.end;
    }

    if (lexer->state == VITTE_LEXER_STATE_DONE) {
        if (lexer->token_count == 0u ||
            lexer->tokens[lexer->token_count - 1u].kind !=
                VITTE_TOKEN_EOF) {
            goto failure;
        }
    }

    lexer->last_error = VITTE_LEXER_ERROR_NONE;
    return true;

failure:

    lexer->stats.validation_failures =
        vitte_lexer_u64_add_sat(
            lexer->stats.validation_failures,
            UINT64_C(1));

    lexer->last_error = VITTE_LEXER_ERROR_VALIDATION;

    return false;
}

/* ========================================================================= */
/* Fingerprint                                                               */
/* ========================================================================= */

uint64_t
vitte_lexer_fingerprint(vitte_lexer_t *lexer)
{
    uint64_t hash;
    size_t i;

    if (!vitte_lexer_context_valid(lexer)) {
        return UINT64_C(0);
    }

    lexer->stats.hash_runs =
        vitte_lexer_u64_add_sat(
            lexer->stats.hash_runs,
            UINT64_C(1));

    hash = VITTE_LEXER_FNV_OFFSET;

    hash = vitte_lexer_hash_u64(
        hash,
        (uint64_t)VITTE_LEXER_API_VERSION_MAJOR);

    hash = vitte_lexer_hash_u64(
        hash,
        (uint64_t)VITTE_LEXER_API_VERSION_MINOR);

    hash = vitte_lexer_hash_u64(
        hash,
        (uint64_t)VITTE_LEXER_API_VERSION_PATCH);

    hash = vitte_lexer_hash_u64(
        hash,
        (uint64_t)lexer->file_id);

    hash = vitte_lexer_hash_u64(
        hash,
        (uint64_t)lexer->token_count);

    for (i = 0u; i < lexer->token_count; ++i) {
        const vitte_token_t *token;

        token = &lexer->tokens[i];

        hash = vitte_lexer_hash_u64(
            hash,
            (uint64_t)token->kind);

        hash = vitte_lexer_hash_u64(
            hash,
            (uint64_t)token->span.begin);

        hash = vitte_lexer_hash_u64(
            hash,
            (uint64_t)token->span.end);

        hash = vitte_lexer_hash_u64(
            hash,
            (uint64_t)token->line);

        hash = vitte_lexer_hash_u64(
            hash,
            (uint64_t)token->column);

        hash = vitte_lexer_hash_bytes(
            hash,
            token->lexeme,
            token->length);
    }

    return hash;
}

/* ========================================================================= */
/* Statistics                                                                */
/* ========================================================================= */

vitte_lexer_stats_t
vitte_lexer_stats(const vitte_lexer_t *lexer)
{
    vitte_lexer_stats_t result;

    memset(&result, 0, sizeof(result));

    if (!vitte_lexer_context_valid(lexer)) {
        return result;
    }

    return lexer->stats;
}

/* ========================================================================= */
/* Limits                                                                    */
/* ========================================================================= */

bool
vitte_lexer_set_limits(
    vitte_lexer_t *lexer,
    size_t max_tokens,
    size_t max_source_bytes,
    size_t max_comment_depth)
{
    if (!vitte_lexer_mutable(lexer)) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_INVALID_STATE,
            lexer != NULL ? lexer->offset : 0u,
            lexer != NULL ? lexer->offset : 0u);
    }

    if (max_tokens == 0u ||
        max_source_bytes == 0u ||
        max_comment_depth == 0u) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_INVALID_ARGUMENT,
            lexer->offset,
            lexer->offset);
    }

    if (max_tokens < lexer->token_count ||
        max_source_bytes < lexer->source_length) {
        return vitte_lexer_fail(
            lexer,
            VITTE_LEXER_ERROR_INVALID_ARGUMENT,
            lexer->offset,
            lexer->offset);
    }

    lexer->max_tokens = max_tokens;
    lexer->max_source_bytes = max_source_bytes;
    lexer->max_comment_depth = max_comment_depth;

    lexer->last_error = VITTE_LEXER_ERROR_NONE;

    return true;
}

/* ========================================================================= */
/* Translation-unit anchor                                                   */
/* ========================================================================= */

void
vitte_lexer_translation_unit_anchor(void)
{
    /*
     * Intentionally empty.
     *
     * Allows build systems to force this translation unit into static
     * archives and provides a stable linkage probe.
     */
}
