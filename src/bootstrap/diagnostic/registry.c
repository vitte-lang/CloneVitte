#include "registry.h"

#include "diagnostic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Vitte diagnostic registry.
 *
 * Central authoritative registry for stable compiler diagnostic codes.
 *
 * Public ranges:
 *
 *   E0000-E0099  compiler / infrastructure
 *   E0100-E0199  lexer
 *   E0200-E0299  parser
 *   E0300-E0399  modules / imports / packages
 *   E0400-E0499  names / scopes
 *   E0500-E0599  types
 *   E0600-E0699  calls / procedures
 *   E0700-E0799  contracts
 *   E0800-E0899  generics / traits / impl
 *   E0900-E0999  C17 backend
 *   E1000-E1099  IR / lowering
 *   E1100-E1199  control flow
 *   E1200-E1299  memory / references / pointers
 *   E1300-E1399  unsafe / low-level
 *   E1400-E1499  FFI / ABI
 *   E1500-E1599  constant evaluation
 *   E1600-E1699  pattern matching
 *   E1700-E1799  concurrency / async
 *   E1800-E1899  macros / compiler / passes
 *   E1900-E1999  target / platform
 *
 *   Wxxxx        warnings
 *   Nxxxx        informational notes
 *   Hxxxx        help/fix guidance
 *
 * Rules:
 *
 *   - public codes are stable;
 *   - internal symbolic names are stable;
 *   - no runtime hashing determines public codes;
 *   - one public code maps to exactly one registry entry;
 *   - one internal symbolic name maps to exactly one registry entry;
 *   - entries are immutable;
 *   - registry iteration is deterministic;
 *   - aliases, if introduced later, must be explicit.
 */

/* ========================================================================= */
/* Registry construction                                                     */
/* ========================================================================= */

#define ENTRY(                                                         \
    internal_,                                                         \
    public_,                                                           \
    severity_,                                                         \
    origin_,                                                           \
    category_,                                                         \
    title_,                                                            \
    explanation_                                                       \
)                                                                      \
    {                                                                  \
        (internal_),                                                   \
        (public_),                                                     \
        (severity_),                                                   \
        (origin_),                                                     \
        (category_),                                                   \
        (title_),                                                      \
        (explanation_),                                                \
        false                                                          \
    }

#define WARNING_ENTRY(                                                 \
    internal_,                                                         \
    public_,                                                           \
    origin_,                                                           \
    category_,                                                         \
    title_,                                                            \
    explanation_                                                       \
)                                                                      \
    ENTRY(                                                             \
        internal_,                                                     \
        public_,                                                       \
        VITTE_DIAGNOSTIC_WARNING,                                      \
        origin_,                                                       \
        category_,                                                     \
        title_,                                                        \
        explanation_                                                   \
    )

/* ========================================================================= */
/* Registry                                                                  */
/* ========================================================================= */

static const vitte_diagnostic_registry_entry_t
vitte_registry_entries[] = {

    /* ===================================================================== */
    /* E0000-E0099: infrastructure                                           */
    /* ===================================================================== */

    ENTRY(
        VITTE_INFRA_E_FALLBACK,
        "E0000",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "compiler",
        "compiler failure",
        "The compiler failed without a more specific diagnostic."
    ),

    ENTRY(
        VITTE_INFRA_E_INTERNAL,
        "E0001",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "compiler",
        "internal compiler error",
        "An internal compiler invariant or implementation assumption failed."
    ),

    ENTRY(
        VITTE_INFRA_E_INVARIANT,
        "E0002",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "compiler",
        "compiler invariant violated",
        "A compiler data structure or phase invariant was violated."
    ),

    ENTRY(
        VITTE_INFRA_E_IMPOSSIBLE,
        "E0003",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "compiler",
        "unreachable compiler state",
        "The compiler reached a state that should be impossible."
    ),

    ENTRY(
        VITTE_INFRA_E_RESOURCE,
        "E0004",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "resource",
        "compiler resource failure",
        "A required compiler resource could not be allocated or acquired."
    ),

    ENTRY(
        VITTE_INFRA_E_SOURCE_READ,
        "E0005",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IO,
        "source",
        "source file could not be read",
        "The compiler could not load the requested source file."
    ),

    ENTRY(
        VITTE_INFRA_E_ENCODING,
        "E0006",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "encoding",
        "invalid source encoding",
        "The source contains an invalid or unsupported text encoding sequence."
    ),

    ENTRY(
        VITTE_INFRA_E_SOURCE_MAP,
        "E0007",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "source-map",
        "source mapping failure",
        "A generated diagnostic could not be mapped back to its Vitte source."
    ),

    ENTRY(
        VITTE_INFRA_E_DIAGNOSTIC,
        "E0008",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "diagnostic",
        "diagnostic subsystem failure",
        "The compiler diagnostic subsystem encountered an internal failure."
    ),

    ENTRY(
        VITTE_INFRA_E_LIMIT,
        "E0009",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "limit",
        "diagnostic limit reached",
        "Compilation stopped because the configured diagnostic limit was reached."
    ),

    ENTRY(
        VITTE_INFRA_E_FORMAT,
        "E0010",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "format",
        "diagnostic formatting failure",
        "A diagnostic could not be formatted for the selected output format."
    ),

    ENTRY(
        VITTE_INFRA_E_CONFIG,
        "E0011",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "configuration",
        "invalid compiler configuration",
        "The compiler configuration contains an invalid or incompatible option."
    ),

    ENTRY(
        VITTE_INFRA_E_PHASE,
        "E0012",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "phase",
        "compiler phase failure",
        "A compiler phase terminated unexpectedly."
    ),

    ENTRY(
        VITTE_INFRA_E_COMPONENT,
        "E0013",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "component",
        "compiler component failure",
        "A required compiler component failed or was unavailable."
    ),

    ENTRY(
        "VITTE_FRONTEND_E_SOURCE",
        "E0014",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "source",
        "native frontend rejected source",
        "The native frontend rejected the source, but this API call did not provide the detailed parser or semantic diagnostics."
    ),

    /* ===================================================================== */
    /* E0100-E0199: lexer                                                    */
    /* ===================================================================== */

    ENTRY(
        "VITTE_LEXER_E_INVALID_CHARACTER",
        "E0101",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "lexer",
        "invalid character",
        "The source contains a character that is not valid in this lexical context."
    ),

    ENTRY(
        "VITTE_LEXER_E_UNTERMINATED_STRING",
        "E0102",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "lexer",
        "unterminated string literal",
        "A string literal reaches the end of its permitted region without a closing delimiter."
    ),

    ENTRY(
        "VITTE_LEXER_E_INVALID_ESCAPE",
        "E0103",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "lexer",
        "invalid escape sequence",
        "The string or character literal contains an invalid escape sequence."
    ),

    ENTRY(
        "VITTE_LEXER_E_INVALID_NUMBER",
        "E0104",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "lexer",
        "invalid numeric literal",
        "The numeric literal does not conform to Vitte lexical rules."
    ),

    ENTRY(
        "VITTE_LEXER_E_NUMBER_OVERFLOW",
        "E0105",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "lexer",
        "numeric literal overflow",
        "The numeric literal exceeds the range accepted by its lexical representation."
    ),

    ENTRY(
        "VITTE_LEXER_E_UNTERMINATED_COMMENT",
        "E0106",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "lexer",
        "unterminated comment",
        "A block comment was opened but not terminated."
    ),

    ENTRY(
        "VITTE_LEXER_E_INVALID_IDENTIFIER",
        "E0107",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "lexer",
        "invalid identifier",
        "The token cannot form a valid Vitte identifier."
    ),

    ENTRY(
        "VITTE_LEXER_E_INVALID_UNICODE",
        "E0108",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LEXER,
        "unicode",
        "invalid Unicode sequence",
        "The source contains malformed or unsupported Unicode data."
    ),

    /* ===================================================================== */
    /* E0200-E0299: parser                                                   */
    /* ===================================================================== */

    ENTRY(
        "VITTE_PARSER_E_EXPECTED_TOKEN",
        "E0201",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "expected token",
        "The parser expected a token required by the Vitte grammar."
    ),

    ENTRY(
        "VITTE_PARSER_E_UNEXPECTED_TOKEN",
        "E0202",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "unexpected token",
        "The current token is not valid in this syntactic position."
    ),

    ENTRY(
        "VITTE_PARSER_E_EXPECTED_EXPRESSION",
        "E0203",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "expected expression",
        "The grammar requires an expression at this location."
    ),

    ENTRY(
        "VITTE_PARSER_E_EXPECTED_DECLARATION",
        "E0204",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "expected declaration",
        "The grammar requires a declaration at this location."
    ),

    ENTRY(
        "VITTE_PARSER_E_EXPECTED_TYPE",
        "E0205",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "expected type",
        "The grammar requires a type expression at this location."
    ),

    ENTRY(
        "VITTE_PARSER_E_EXPECTED_PATTERN",
        "E0206",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "expected pattern",
        "The grammar requires a pattern at this location."
    ),

    ENTRY(
        "VITTE_PARSER_E_UNCLOSED_DELIMITER",
        "E0207",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "unclosed delimiter",
        "An opening delimiter has no corresponding closing delimiter."
    ),

    ENTRY(
        "VITTE_PARSER_E_MISMATCHED_DELIMITER",
        "E0208",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "mismatched delimiter",
        "A closing delimiter does not match the corresponding opening delimiter."
    ),

    ENTRY(
        "VITTE_PARSER_E_INVALID_MODIFIER",
        "E0209",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "syntax",
        "invalid declaration modifier",
        "A declaration modifier is not permitted in this syntactic context."
    ),

    /* ===================================================================== */
    /* E0300-E0399: module/import/package                                    */
    /* ===================================================================== */

    ENTRY(
        "VITTE_IMPORT_E_MODULE_NOT_FOUND",
        "E0301",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IMPORT,
        "module",
        "module not found",
        "The requested module could not be resolved."
    ),

    ENTRY(
        "VITTE_IMPORT_E_PRIVATE_SYMBOL",
        "E0302",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IMPORT,
        "module",
        "private symbol imported",
        "The requested symbol is not visible outside its defining module."
    ),

    ENTRY(
        "VITTE_IMPORT_E_CYCLE",
        "E0303",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IMPORT,
        "module",
        "cyclic module dependency",
        "The module dependency graph contains a cycle that cannot be resolved."
    ),

    ENTRY(
        "VITTE_IMPORT_E_DUPLICATE",
        "E0304",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IMPORT,
        "module",
        "duplicate import",
        "The same import is declared more than once in an incompatible way."
    ),

    ENTRY(
        "VITTE_IMPORT_E_PACKAGE_NOT_FOUND",
        "E0305",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IMPORT,
        "package",
        "package not found",
        "The requested package could not be located."
    ),

    ENTRY(
        "VITTE_IMPORT_E_VERSION",
        "E0306",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IMPORT,
        "package",
        "package version mismatch",
        "The resolved package version does not satisfy the requested constraint."
    ),

    ENTRY(
        "VITTE_IMPORT_E_INVALID_GRAPH",
        "E0307",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_IMPORT,
        "import",
        "invalid import graph",
        "The resolved import graph failed structural validation, indicating corrupt or inconsistent compiler state."
    ),

    /* ===================================================================== */
    /* E0400-E0499: names/scopes                                             */
    /* ===================================================================== */

    ENTRY(
        "VITTE_RESOLVE_E_UNKNOWN_SYMBOL",
        "E0401",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION,
        "name-resolution",
        "unknown symbol",
        "The referenced name cannot be resolved in the current scope."
    ),

    ENTRY(
        "VITTE_RESOLVE_E_REDECLARATION",
        "E0402",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION,
        "name-resolution",
        "symbol redeclared",
        "A symbol conflicts with an existing declaration in the same namespace."
    ),

    ENTRY(
        "VITTE_RESOLVE_E_AMBIGUOUS_SYMBOL",
        "E0403",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION,
        "name-resolution",
        "ambiguous symbol",
        "More than one visible declaration matches this name."
    ),

    ENTRY(
        "VITTE_RESOLVE_E_PRIVATE",
        "E0404",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION,
        "visibility",
        "symbol is not visible",
        "The declaration exists but is not accessible from this context."
    ),

    ENTRY(
        "VITTE_RESOLVE_E_INVALID_SCOPE",
        "E0405",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION,
        "scope",
        "invalid scope",
        "The referenced declaration is not valid in the current lexical or semantic scope."
    ),

    ENTRY(
        "VITTE_RESOLVE_E_DUPLICATE_BINDING",
        "E0406",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION,
        "scope",
        "duplicate binding",
        "A pattern or declaration introduces the same binding more than once."
    ),

    /* ===================================================================== */
    /* E0500-E0599: types                                                    */
    /* ===================================================================== */

    ENTRY(
        "VITTE_TYPE_E_MISMATCH",
        "E0501",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "type mismatch",
        "The actual type does not satisfy the expected type."
    ),

    ENTRY(
        "VITTE_TYPE_E_CANNOT_INFER",
        "E0502",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "type cannot be inferred",
        "The available constraints are insufficient to determine a unique type."
    ),

    ENTRY(
        "VITTE_TYPE_E_INVALID_CAST",
        "E0503",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "invalid cast",
        "The requested conversion is not permitted between these types."
    ),

    ENTRY(
        "VITTE_TYPE_E_NOT_CALLABLE",
        "E0504",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "value is not callable",
        "The expression does not have a callable type."
    ),

    ENTRY(
        "VITTE_TYPE_E_NOT_INDEXABLE",
        "E0505",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "value is not indexable",
        "The expression does not support indexing."
    ),

    ENTRY(
        "VITTE_TYPE_E_NO_MEMBER",
        "E0506",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "member does not exist",
        "The requested field, property or member is not defined for this type."
    ),

    ENTRY(
        "VITTE_TYPE_E_CONSTRAINT",
        "E0507",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "type constraint unsatisfied",
        "A type does not satisfy a required semantic constraint."
    ),

    ENTRY(
        "VITTE_TYPE_E_INCOMPLETE",
        "E0508",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "incomplete type",
        "The operation requires a complete type definition."
    ),

    ENTRY(
        "VITTE_TYPE_E_RECURSIVE",
        "E0509",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "invalid recursive type",
        "The type definition forms an unsupported recursive structure."
    ),

    ENTRY(
        "VITTE_TYPE_E_UNKNOWN_TYPE",
        "E0510",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "unknown type",
        "The referenced type name cannot be resolved in the current scope."
    ),

    ENTRY(
        "VITTE_TYPE_E_NOT_A_TYPE",
        "E0511",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "name is not a type",
        "The referenced name resolves to a value or procedure rather than a type."
    ),

    ENTRY(
        "VITTE_TYPE_E_INVALID_OPERAND",
        "E0512",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "invalid operand type",
        "The operator does not support the type of this operand or operand pair."
    ),

    ENTRY(
        "VITTE_TYPE_E_NOT_ASSIGNABLE",
        "E0513",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "value is not assignable",
        "The expression does not designate a writable location."
    ),

    ENTRY(
        "VITTE_TYPE_E_INVALID_INDEX",
        "E0514",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "invalid index type",
        "The index expression is not an accepted index type for this value."
    ),

    ENTRY(
        "VITTE_TYPE_E_CONDITION_NOT_BOOL",
        "E0515",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "type",
        "condition is not boolean",
        "A control-flow condition must evaluate to a boolean value."
    ),

    /* ===================================================================== */
    /* E0600-E0699: calls/procedures                                         */
    /* ===================================================================== */

    ENTRY(
        "VITTE_CALL_E_ARITY",
        "E0601",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "call",
        "incorrect argument count",
        "The call supplies a different number of arguments than required."
    ),

    ENTRY(
        "VITTE_CALL_E_ARGUMENT_TYPE",
        "E0602",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "call",
        "invalid argument type",
        "An argument does not satisfy the corresponding parameter type."
    ),

    ENTRY(
        "VITTE_CALL_E_NO_CANDIDATE",
        "E0603",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "call",
        "no matching procedure",
        "No callable candidate satisfies the supplied arguments and constraints."
    ),

    ENTRY(
        "VITTE_CALL_E_AMBIGUOUS",
        "E0604",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "call",
        "ambiguous call",
        "More than one callable candidate matches this invocation."
    ),

    ENTRY(
        "VITTE_CALL_E_RETURN_TYPE",
        "E0605",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "procedure",
        "invalid return type",
        "A returned expression is incompatible with the procedure return type."
    ),

    ENTRY(
        "VITTE_CALL_E_MODIFIER",
        "E0606",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "procedure",
        "invalid procedure modifier",
        "The procedure cannot be used with this modifier or calling context."
    ),

    ENTRY(
        "VITTE_CALL_E_RETURN_OUTSIDE_PROC",
        "E0607",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "procedure",
        "return outside procedure",
        "A return statement is only valid inside a procedure body."
    ),

    /* ===================================================================== */
    /* E0700-E0799: contracts                                                */
    /* ===================================================================== */

    ENTRY(
        "VITTE_CONTRACT_E_INVALID",
        "E0700",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "invalid contract",
        "The contract expression is invalid in its declaration context."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_PRECONDITION",
        "E0701",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "precondition not satisfied",
        "A call does not satisfy a required precondition."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_POSTCONDITION",
        "E0702",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "postcondition failed",
        "A procedure result cannot satisfy a declared postcondition."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_INVARIANT",
        "E0703",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "invariant not satisfied",
        "A declared invariant cannot be established at the required program point."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_REQUIRES",
        "E0704",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "invalid requires clause",
        "A requires clause is malformed or semantically invalid."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_ENSURES",
        "E0705",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "invalid ensures clause",
        "An ensures clause is malformed or semantically invalid."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_OLD_VALUE",
        "E0706",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "invalid old value",
        "A contract attempts to reference a previous value that is not available."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_RESULT",
        "E0707",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "invalid result reference",
        "A contract references a result value where no result is available."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_UNPROVABLE",
        "E0708",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "contract cannot be established",
        "The compiler cannot establish that the contract is satisfied."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_CONTRADICTION",
        "E0709",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "contradictory contract",
        "The declared contract contains mutually incompatible requirements."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_EFFECT",
        "E0710",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "invalid contract effect",
        "The contract performs or depends on an effect that is not allowed."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_CALL",
        "E0711",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "invalid call in contract",
        "The contract invokes an operation that is not valid in contract evaluation."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_OVERRIDE",
        "E0712",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONTRACT,
        "contract",
        "incompatible overridden contract",
        "An implementation weakens or strengthens a contract incompatibly."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_RUNTIME",
        "E0713",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_RUNTIME,
        "contract",
        "runtime contract violation",
        "A runtime contract check evaluated to false."
    ),

    ENTRY(
        "VITTE_CONTRACT_E_INTERNAL",
        "E0714",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "contract",
        "contract checker failure",
        "The compiler contract subsystem encountered an internal failure."
    ),

    /* ===================================================================== */
    /* E0800-E0899: generics/traits                                          */
    /* ===================================================================== */

    ENTRY(
        "VITTE_GENERIC_E_BOUND",
        "E0801",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "generic",
        "generic bound not satisfied",
        "A generic argument does not satisfy a required bound."
    ),

    ENTRY(
        "VITTE_GENERIC_E_ARGUMENT_COUNT",
        "E0802",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "generic",
        "incorrect generic argument count",
        "The generic declaration requires a different number of arguments."
    ),

    ENTRY(
        "VITTE_TRAIT_E_NOT_IMPLEMENTED",
        "E0810",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "trait",
        "trait not implemented",
        "The type does not provide a required trait implementation."
    ),

    ENTRY(
        "VITTE_TRAIT_E_AMBIGUOUS_IMPL",
        "E0811",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "trait",
        "ambiguous implementation",
        "More than one implementation satisfies the requested trait relation."
    ),

    ENTRY(
        "VITTE_TRAIT_E_CONFLICTING_IMPL",
        "E0812",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "trait",
        "conflicting implementation",
        "Two implementations overlap in a way that violates coherence rules."
    ),

    /* ===================================================================== */
    /* E0900-E0999: C17 backend                                              */
    /* ===================================================================== */

    ENTRY(
        "VITTE_BACKEND_E_INVALID_C",
        "E0901",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND,
        "backend",
        "invalid generated C",
        "The backend generated a C17 construct that could not be accepted."
    ),

    ENTRY(
        "VITTE_BACKEND_E_CODEGEN",
        "E0902",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND,
        "backend",
        "code generation failure",
        "The C17 backend could not lower a Vitte construct."
    ),

    ENTRY(
        "VITTE_BACKEND_E_UNSUPPORTED",
        "E0903",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND,
        "backend",
        "unsupported backend construct",
        "The selected C17 backend does not support this lowered construct."
    ),

    ENTRY(
        "VITTE_BACKEND_E_EXTERNAL_COMPILER",
        "E0904",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_C_COMPILER,
        "backend",
        "external C compiler failure",
        "The generated C source was rejected by the external C compiler."
    ),

    ENTRY(
        "VITTE_BACKEND_E_SOURCE_MAP",
        "E0905",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND,
        "source-map",
        "backend source mapping failure",
        "Generated C could not be mapped accurately to the originating Vitte source."
    ),

    ENTRY(
        "VITTE_BACKEND_E_OUTPUT",
        "E0906",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IO,
        "backend",
        "C17 output failure",
        "The C17 backend could not create, write, or close an output file."
    ),

    ENTRY(
        "VITTE_BACKEND_E_LINK",
        "E0907",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_LINKER,
        "linker",
        "native linking failure",
        "The generated object could not be linked into the requested executable."
    ),

    ENTRY(
        "VITTE_BACKEND_E_EXECUTION",
        "E0908",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_RUNTIME,
        "runtime",
        "compiled program execution failure",
        "The generated program was compiled and linked, but exited unsuccessfully."
    ),

    ENTRY(
        "VITTE_BACKEND_E_CONFIG",
        "E0909",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND,
        "backend",
        "invalid backend configuration",
        "The C17 backend configuration is invalid or incompatible with the selected code generation path."
    ),

    /* ===================================================================== */
    /* E1000-E1099: IR/lowering                                              */
    /* ===================================================================== */

    ENTRY(
        "VITTE_IR_E_INVALID",
        "E1001",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_IR,
        "ir",
        "invalid intermediate representation",
        "The compiler produced an invalid IR node or instruction."
    ),

    ENTRY(
        "VITTE_IR_E_LOWERING",
        "E1002",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_IR,
        "lowering",
        "lowering failure",
        "A higher-level compiler construct could not be lowered into IR."
    ),

    ENTRY(
        "VITTE_IR_E_VERIFY",
        "E1003",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_IR,
        "ir",
        "IR verification failed",
        "The intermediate representation violates an internal structural or semantic invariant."
    ),

    ENTRY(
        "VITTE_HIR_E_INVALID",
        "E1010",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_HIR,
        "hir",
        "invalid high-level IR",
        "The compiler produced an invalid HIR construct."
    ),

    ENTRY(
        "VITTE_HIR_E_LOWERING",
        "E1011",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_HIR,
        "lowering",
        "HIR lowering failure",
        "The compiler could not lower or validate a source construct in high-level IR."
    ),

    /* ===================================================================== */
    /* E1100-E1199: control flow                                             */
    /* ===================================================================== */

    ENTRY(
        "VITTE_CONTROL_E_MISSING_RETURN",
        "E1101",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "control-flow",
        "missing return",
        "A procedure can reach its end without producing the required result."
    ),

    ENTRY(
        "VITTE_CONTROL_E_INVALID_BREAK",
        "E1102",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "control-flow",
        "invalid break",
        "A break statement is used outside a valid breakable construct."
    ),

    ENTRY(
        "VITTE_CONTROL_E_INVALID_CONTINUE",
        "E1103",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "control-flow",
        "invalid continue",
        "A continue statement is used outside a valid loop."
    ),

    ENTRY(
        "VITTE_CONTROL_E_UNREACHABLE",
        "E1104",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "control-flow",
        "unreachable operation",
        "A required operation cannot be reached through valid control flow."
    ),

    /* ===================================================================== */
    /* E1200-E1299: memory                                                   */
    /* ===================================================================== */

    ENTRY(
        "VITTE_MEMORY_E_INVALID_ACCESS",
        "E1201",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "invalid memory access",
        "The operation attempts to access memory in an invalid way."
    ),

    ENTRY(
        "VITTE_MEMORY_E_NULL",
        "E1202",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "invalid null operation",
        "The operation is not valid for a null value."
    ),

    ENTRY(
        "VITTE_MEMORY_E_POINTER_TYPE",
        "E1203",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "invalid pointer type",
        "The pointer operation is incompatible with the referenced type."
    ),

    ENTRY(
        "VITTE_MEMORY_E_ALIGNMENT",
        "E1204",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "invalid alignment",
        "The requested memory operation violates alignment requirements."
    ),

    ENTRY(
        "VITTE_MEMORY_E_REFERENCE_ESCAPE",
        "E1205",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "reference lifetime cannot be verified",
        "The owner does not outlive the reference, or the bootstrap cannot determine the reference origin."
    ),

    ENTRY(
        "VITTE_MEMORY_E_BORROW_CONFLICT",
        "E1206",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "conflicting reference borrow",
        "An exclusive reference cannot overlap another live reference to the same value."
    ),

    ENTRY(
        "VITTE_MEMORY_E_BORROWED_MUTATION",
        "E1207",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "access conflicts with a live reference",
        "The value cannot be read or changed while an incompatible reference is live."
    ),

    ENTRY(
        "VITTE_MEMORY_E_MOVE_BORROWED",
        "E1208",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "cannot move a borrowed value",
        "A value cannot be moved while a live reference depends on it."
    ),

    ENTRY(
        "VITTE_MEMORY_E_USE_AFTER_MOVE",
        "E1209",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "memory",
        "use of moved value",
        "The value was moved and must be initialized again before it can be used."
    ),

    /* ===================================================================== */
    /* E1300-E1399: unsafe / low-level                                       */
    /* ===================================================================== */

    ENTRY(
        "VITTE_UNSAFE_E_REQUIRED",
        "E1301",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "unsafe",
        "unsafe context required",
        "This low-level operation requires an explicit unsafe context."
    ),

    ENTRY(
        "VITTE_UNSAFE_E_INVALID_ASM",
        "E1302",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "asm",
        "invalid assembly block",
        "The inline assembly construct is malformed or incompatible with the selected target."
    ),

    ENTRY(
        "VITTE_UNSAFE_E_INTRINSIC",
        "E1303",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "intrinsic",
        "invalid intrinsic use",
        "A compiler intrinsic is used with unsupported arguments or in an invalid context."
    ),

    /* ===================================================================== */
    /* E1400-E1499: FFI / ABI                                                */
    /* ===================================================================== */

    ENTRY(
        "VITTE_FFI_E_ABI",
        "E1401",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "abi",
        "unsupported ABI",
        "The requested application binary interface is unsupported."
    ),

    ENTRY(
        "VITTE_FFI_E_TYPE",
        "E1402",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "ffi",
        "FFI-incompatible type",
        "A type cannot safely or correctly cross the selected foreign interface."
    ),

    ENTRY(
        "VITTE_FFI_E_VARIADIC",
        "E1403",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "ffi",
        "invalid variadic foreign call",
        "The foreign variadic call violates ABI or type requirements."
    ),

    ENTRY(
        "VITTE_FFI_E_LINK_NAME",
        "E1404",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "ffi",
        "invalid foreign symbol",
        "The foreign declaration has an invalid or conflicting link symbol."
    ),

    /* ===================================================================== */
    /* E1500-E1599: constant evaluation                                      */
    /* ===================================================================== */

    ENTRY(
        "VITTE_CONST_E_NOT_CONSTANT",
        "E1501",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONSTANT_EVAL,
        "const-eval",
        "expression is not constant",
        "The expression cannot be evaluated in a constant context."
    ),

    ENTRY(
        "VITTE_CONST_E_OVERFLOW",
        "E1502",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONSTANT_EVAL,
        "const-eval",
        "constant evaluation overflow",
        "A compile-time arithmetic operation exceeds its representable range."
    ),

    ENTRY(
        "VITTE_CONST_E_DIVIDE_BY_ZERO",
        "E1503",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONSTANT_EVAL,
        "const-eval",
        "division by zero",
        "A constant expression attempts to divide by zero."
    ),

    ENTRY(
        "VITTE_CONST_E_CYCLE",
        "E1504",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONSTANT_EVAL,
        "const-eval",
        "constant evaluation cycle",
        "Compile-time evaluation depends recursively on itself."
    ),

    ENTRY(
        "VITTE_CONST_E_LIMIT",
        "E1505",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_CONSTANT_EVAL,
        "const-eval",
        "constant evaluation limit reached",
        "Compile-time evaluation exceeded a configured resource or recursion limit."
    ),

    /* ===================================================================== */
    /* E1600-E1699: patterns                                                 */
    /* ===================================================================== */

    ENTRY(
        "VITTE_PATTERN_E_NON_EXHAUSTIVE",
        "E1601",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "pattern",
        "non-exhaustive match",
        "The match expression does not cover all possible input values."
    ),

    ENTRY(
        "VITTE_PATTERN_E_UNREACHABLE",
        "E1602",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "pattern",
        "unreachable pattern",
        "A previous pattern already covers every value matched by this pattern."
    ),

    ENTRY(
        "VITTE_PATTERN_E_TYPE",
        "E1603",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "pattern",
        "pattern type mismatch",
        "The pattern cannot match a value of the scrutinee type."
    ),

    ENTRY(
        "VITTE_PATTERN_E_BINDING",
        "E1604",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "pattern",
        "inconsistent pattern binding",
        "Alternative patterns do not introduce compatible bindings."
    ),

    /* ===================================================================== */
    /* E1700-E1799: async/concurrency                                        */
    /* ===================================================================== */

    ENTRY(
        "VITTE_ASYNC_E_AWAIT_CONTEXT",
        "E1701",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "async",
        "invalid asynchronous context",
        "An asynchronous operation is used outside a compatible procedure or execution context."
    ),

    ENTRY(
        "VITTE_ASYNC_E_TASK_TYPE",
        "E1702",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "async",
        "invalid task type",
        "The operation requires a task-compatible value."
    ),

    ENTRY(
        "VITTE_CONCURRENCY_E_SEND",
        "E1710",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "concurrency",
        "value cannot cross execution context",
        "The value does not satisfy the requirements for transfer between concurrent execution contexts."
    ),

    ENTRY(
        "VITTE_CONCURRENCY_E_CRITICAL",
        "E1711",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "concurrency",
        "invalid critical section",
        "The operation violates restrictions imposed by a critical section."
    ),

    /* ===================================================================== */
    /* E1800-E1899: macros/compiler/pass                                     */
    /* ===================================================================== */

    ENTRY(
        "VITTE_MACRO_E_EXPANSION",
        "E1801",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "macro",
        "macro expansion failed",
        "A macro could not be expanded into a valid Vitte construct."
    ),

    ENTRY(
        "VITTE_MACRO_E_RECURSION",
        "E1802",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_PARSER,
        "macro",
        "macro recursion limit reached",
        "Macro expansion exceeded the configured recursion limit."
    ),

    ENTRY(
        "VITTE_PASS_E_FAILED",
        "E1810",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "compiler-pass",
        "compiler pass failed",
        "A compiler transformation pass failed unexpectedly."
    ),

    ENTRY(
        "VITTE_PASS_E_VERIFY",
        "E1811",
        VITTE_DIAGNOSTIC_FATAL,
        VITTE_DIAGNOSTIC_ORIGIN_INTERNAL,
        "compiler-pass",
        "compiler pass verification failed",
        "A compiler pass produced an invalid representation."
    ),

    /* ===================================================================== */
    /* E1900-E1999: target/platform                                          */
    /* ===================================================================== */

    ENTRY(
        "VITTE_TARGET_E_UNSUPPORTED",
        "E1901",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "target",
        "unsupported target",
        "The requested compilation target is not supported."
    ),

    ENTRY(
        "VITTE_TARGET_E_ARCH",
        "E1902",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "target",
        "unsupported architecture",
        "The requested CPU architecture is not supported."
    ),

    ENTRY(
        "VITTE_TARGET_E_FEATURE",
        "E1903",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "target",
        "unsupported target feature",
        "The requested target feature is unavailable or incompatible."
    ),

    ENTRY(
        "VITTE_TARGET_E_POINTER_WIDTH",
        "E1904",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "target",
        "unsupported pointer width",
        "The selected target uses a pointer width unsupported by this compilation mode."
    ),

    ENTRY(
        "VITTE_TARGET_E_ENDIAN",
        "E1905",
        VITTE_DIAGNOSTIC_ERROR,
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "target",
        "unsupported byte order",
        "The selected target byte order is unsupported by this operation."
    ),

    /* ===================================================================== */
    /* Warnings                                                              */
    /* ===================================================================== */

    WARNING_ENTRY(
        "VITTE_WARNING_UNUSED_VARIABLE",
        "W0001",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "unused",
        "unused variable",
        "A local variable is declared but never used."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_UNUSED_PARAMETER",
        "W0002",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "unused",
        "unused parameter",
        "A procedure parameter is never used."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_UNUSED_IMPORT",
        "W0003",
        VITTE_DIAGNOSTIC_ORIGIN_IMPORT,
        "unused",
        "unused import",
        "An imported module or symbol is never referenced."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_UNUSED_RESULT",
        "W0004",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "unused",
        "unused result",
        "The result of an operation is ignored even though it may be significant."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_DEPRECATED",
        "W0010",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "deprecated",
        "deprecated construct",
        "The referenced construct is deprecated and may be removed in a future language version."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_UNREACHABLE_CODE",
        "W0020",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "control-flow",
        "unreachable code",
        "This code cannot be reached through normal control flow."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_SHADOWING",
        "W0030",
        VITTE_DIAGNOSTIC_ORIGIN_NAME_RESOLUTION,
        "shadowing",
        "declaration shadows another declaration",
        "This declaration hides a visible declaration with the same name."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_IMPLICIT_CONVERSION",
        "W0040",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "conversion",
        "implicit conversion",
        "An implicit conversion may change the value or representation."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_TRUNCATION",
        "W0041",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "conversion",
        "possible truncation",
        "A conversion may discard significant bits or precision."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_SIGN_CHANGE",
        "W0042",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "conversion",
        "signedness change",
        "A conversion changes signedness and may alter interpretation of the value."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_REDUNDANT_CAST",
        "W0050",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "redundant",
        "redundant cast",
        "The explicit cast does not change the expression type."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_REDUNDANT_CONDITION",
        "W0051",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "redundant",
        "redundant condition",
        "The condition is statically known or duplicates an existing condition."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_SUSPICIOUS_COMPARISON",
        "W0060",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "suspicious",
        "suspicious comparison",
        "The comparison is valid but is likely to be unintended."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_FFI_LAYOUT",
        "W0070",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "ffi",
        "unstable foreign layout",
        "The type layout may not be suitable for a stable foreign interface."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_CONTRACT_RUNTIME",
        "W0080",
        VITTE_DIAGNOSTIC_ORIGIN_TYPE_CHECK,
        "contract",
        "contract requires runtime check",
        "The compiler cannot prove the contract statically and must preserve a runtime check."
    ),

    WARNING_ENTRY(
        "VITTE_WARNING_INFRASTRUCTURE",
        "W0090",
        VITTE_DIAGNOSTIC_ORIGIN_DRIVER,
        "compiler",
        "compiler infrastructure warning",
        "A non-fatal compiler or source I/O operation did not complete cleanly."
    )
};

#undef WARNING_ENTRY
#undef ENTRY

/* ========================================================================= */
/* Registry size                                                             */
/* ========================================================================= */

static size_t
vitte_registry_count_internal(void)
{
    return sizeof(vitte_registry_entries) /
           sizeof(vitte_registry_entries[0]);
}

size_t
vitte_diagnostic_registry_count(void)
{
    return vitte_registry_count_internal();
}

/* ========================================================================= */
/* Entry access                                                              */
/* ========================================================================= */

const vitte_diagnostic_registry_entry_t *
vitte_diagnostic_registry_at(
    size_t index
)
{
    if (index >=
        vitte_registry_count_internal()) {
        return NULL;
    }

    return &vitte_registry_entries[index];
}

/* ========================================================================= */
/* Internal name lookup                                                      */
/* ========================================================================= */

const vitte_diagnostic_registry_entry_t *
vitte_diagnostic_registry_find_internal(
    const char *internal_code
)
{
    size_t index;
    size_t count;

    if (internal_code == NULL ||
        internal_code[0] == '\0') {
        return NULL;
    }

    count =
        vitte_registry_count_internal();

    for (index = 0u;
         index < count;
         index++) {

        const vitte_diagnostic_registry_entry_t *entry;

        entry =
            &vitte_registry_entries[index];

        if (strcmp(
                entry->internal_code,
                internal_code
            ) == 0) {
            return entry;
        }
    }

    return NULL;
}

/* ========================================================================= */
/* Public code lookup                                                        */
/* ========================================================================= */

const vitte_diagnostic_registry_entry_t *
vitte_diagnostic_registry_find_public(
    const char *public_code
)
{
    size_t index;
    size_t count;

    if (public_code == NULL ||
        public_code[0] == '\0') {
        return NULL;
    }

    count =
        vitte_registry_count_internal();

    for (index = 0u;
         index < count;
         index++) {

        const vitte_diagnostic_registry_entry_t *entry;

        entry =
            &vitte_registry_entries[index];

        if (strcmp(
                entry->public_code,
                public_code
            ) == 0) {
            return entry;
        }
    }

    return NULL;
}

/* ========================================================================= */
/* Generic lookup                                                            */
/* ========================================================================= */

const vitte_diagnostic_registry_entry_t *
vitte_diagnostic_registry_find(
    const char *code
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    if (code == NULL ||
        code[0] == '\0') {
        return NULL;
    }

    entry =
        vitte_diagnostic_registry_find_internal(
            code
        );

    if (entry != NULL) {
        return entry;
    }

    return vitte_diagnostic_registry_find_public(
        code
    );
}

/* ========================================================================= */
/* Convenience metadata                                                      */
/* ========================================================================= */

const char *
vitte_diagnostic_registry_public_code(
    const char *internal_code
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    entry =
        vitte_diagnostic_registry_find_internal(
            internal_code
        );

    return entry != NULL
        ? entry->public_code
        : NULL;
}

const char *
vitte_diagnostic_registry_internal_code(
    const char *public_code
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    entry =
        vitte_diagnostic_registry_find_public(
            public_code
        );

    return entry != NULL
        ? entry->internal_code
        : NULL;
}

vitte_diagnostic_origin_t
vitte_diagnostic_registry_origin(
    const char *code
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    entry =
        vitte_diagnostic_registry_find(
            code
        );

    return entry != NULL
        ? entry->origin
        : VITTE_DIAGNOSTIC_ORIGIN_UNKNOWN;
}

vitte_diagnostic_severity_t
vitte_diagnostic_registry_severity(
    const char *code,
    vitte_diagnostic_severity_t fallback
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    entry =
        vitte_diagnostic_registry_find(
            code
        );

    return entry != NULL
        ? entry->default_severity
        : fallback;
}

const char *
vitte_diagnostic_registry_category(
    const char *code
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    entry =
        vitte_diagnostic_registry_find(
            code
        );

    return entry != NULL
        ? entry->category
        : NULL;
}

const char *
vitte_diagnostic_registry_title(
    const char *code
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    entry =
        vitte_diagnostic_registry_find(
            code
        );

    return entry != NULL
        ? entry->title
        : NULL;
}

const char *
vitte_diagnostic_registry_explanation(
    const char *code
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    entry =
        vitte_diagnostic_registry_find(
            code
        );

    return entry != NULL
        ? entry->explanation
        : NULL;
}

/* ========================================================================= */
/* Public-code parsing                                                       */
/* ========================================================================= */

bool
vitte_diagnostic_registry_parse_public_code(
    const char *code,
    char *prefix,
    unsigned *number
)
{
    unsigned value;
    size_t index;

    if (code == NULL ||
        code[0] == '\0' ||
        code[1] == '\0') {
        return false;
    }

    if (code[0] != 'E' &&
        code[0] != 'W' &&
        code[0] != 'N' &&
        code[0] != 'H') {
        return false;
    }

    value = 0u;

    for (index = 1u;
         code[index] != '\0';
         index++) {

        unsigned digit;

        if (code[index] < '0' ||
            code[index] > '9') {
            return false;
        }

        digit =
            (unsigned)(
                code[index] - '0'
            );

        if (value >
            (UINT32_MAX - digit) / 10u) {
            return false;
        }

        value =
            value * 10u + digit;
    }

    /*
     * Public Vitte diagnostic codes currently use exactly four digits.
     */
    if (index != 5u) {
        return false;
    }

    if (prefix != NULL) {
        *prefix = code[0];
    }

    if (number != NULL) {
        *number = value;
    }

    return true;
}

/* ========================================================================= */
/* Range classification                                                      */
/* ========================================================================= */

vitte_diagnostic_registry_range_t
vitte_diagnostic_registry_range(
    const char *public_code
)
{
    char prefix;
    unsigned number;

    if (!vitte_diagnostic_registry_parse_public_code(
            public_code,
            &prefix,
            &number
        )) {
        return VITTE_DIAGNOSTIC_RANGE_UNKNOWN;
    }

    if (prefix == 'W') {
        return VITTE_DIAGNOSTIC_RANGE_WARNING;
    }

    if (prefix == 'N') {
        return VITTE_DIAGNOSTIC_RANGE_NOTE;
    }

    if (prefix == 'H') {
        return VITTE_DIAGNOSTIC_RANGE_HELP;
    }

    if (prefix != 'E') {
        return VITTE_DIAGNOSTIC_RANGE_UNKNOWN;
    }

    if (number <= 99u) {
        return VITTE_DIAGNOSTIC_RANGE_INFRASTRUCTURE;
    }

    if (number <= 199u) {
        return VITTE_DIAGNOSTIC_RANGE_LEXER;
    }

    if (number <= 299u) {
        return VITTE_DIAGNOSTIC_RANGE_PARSER;
    }

    if (number <= 399u) {
        return VITTE_DIAGNOSTIC_RANGE_MODULE;
    }

    if (number <= 499u) {
        return VITTE_DIAGNOSTIC_RANGE_NAME;
    }

    if (number <= 599u) {
        return VITTE_DIAGNOSTIC_RANGE_TYPE;
    }

    if (number <= 699u) {
        return VITTE_DIAGNOSTIC_RANGE_CALL;
    }

    if (number <= 799u) {
        return VITTE_DIAGNOSTIC_RANGE_CONTRACT;
    }

    if (number <= 899u) {
        return VITTE_DIAGNOSTIC_RANGE_GENERIC;
    }

    if (number <= 999u) {
        return VITTE_DIAGNOSTIC_RANGE_BACKEND;
    }

    if (number <= 1099u) {
        return VITTE_DIAGNOSTIC_RANGE_IR;
    }

    if (number <= 1199u) {
        return VITTE_DIAGNOSTIC_RANGE_CONTROL_FLOW;
    }

    if (number <= 1299u) {
        return VITTE_DIAGNOSTIC_RANGE_MEMORY;
    }

    if (number <= 1399u) {
        return VITTE_DIAGNOSTIC_RANGE_UNSAFE;
    }

    if (number <= 1499u) {
        return VITTE_DIAGNOSTIC_RANGE_FFI;
    }

    if (number <= 1599u) {
        return VITTE_DIAGNOSTIC_RANGE_CONST_EVAL;
    }

    if (number <= 1699u) {
        return VITTE_DIAGNOSTIC_RANGE_PATTERN;
    }

    if (number <= 1799u) {
        return VITTE_DIAGNOSTIC_RANGE_CONCURRENCY;
    }

    if (number <= 1899u) {
        return VITTE_DIAGNOSTIC_RANGE_MACRO;
    }

    if (number <= 1999u) {
        return VITTE_DIAGNOSTIC_RANGE_TARGET;
    }

    return VITTE_DIAGNOSTIC_RANGE_UNKNOWN;
}

/* ========================================================================= */
/* Range names                                                               */
/* ========================================================================= */

const char *
vitte_diagnostic_registry_range_name(
    vitte_diagnostic_registry_range_t range
)
{
    switch (range) {
        case VITTE_DIAGNOSTIC_RANGE_INFRASTRUCTURE:
            return "infrastructure";

        case VITTE_DIAGNOSTIC_RANGE_LEXER:
            return "lexer";

        case VITTE_DIAGNOSTIC_RANGE_PARSER:
            return "parser";

        case VITTE_DIAGNOSTIC_RANGE_MODULE:
            return "module";

        case VITTE_DIAGNOSTIC_RANGE_NAME:
            return "name-resolution";

        case VITTE_DIAGNOSTIC_RANGE_TYPE:
            return "type";

        case VITTE_DIAGNOSTIC_RANGE_CALL:
            return "call";

        case VITTE_DIAGNOSTIC_RANGE_CONTRACT:
            return "contract";

        case VITTE_DIAGNOSTIC_RANGE_GENERIC:
            return "generic";

        case VITTE_DIAGNOSTIC_RANGE_BACKEND:
            return "backend";

        case VITTE_DIAGNOSTIC_RANGE_IR:
            return "ir";

        case VITTE_DIAGNOSTIC_RANGE_CONTROL_FLOW:
            return "control-flow";

        case VITTE_DIAGNOSTIC_RANGE_MEMORY:
            return "memory";

        case VITTE_DIAGNOSTIC_RANGE_UNSAFE:
            return "unsafe";

        case VITTE_DIAGNOSTIC_RANGE_FFI:
            return "ffi";

        case VITTE_DIAGNOSTIC_RANGE_CONST_EVAL:
            return "const-eval";

        case VITTE_DIAGNOSTIC_RANGE_PATTERN:
            return "pattern";

        case VITTE_DIAGNOSTIC_RANGE_CONCURRENCY:
            return "concurrency";

        case VITTE_DIAGNOSTIC_RANGE_MACRO:
            return "macro";

        case VITTE_DIAGNOSTIC_RANGE_TARGET:
            return "target";

        case VITTE_DIAGNOSTIC_RANGE_WARNING:
            return "warning";

        case VITTE_DIAGNOSTIC_RANGE_NOTE:
            return "note";

        case VITTE_DIAGNOSTIC_RANGE_HELP:
            return "help";

        case VITTE_DIAGNOSTIC_RANGE_UNKNOWN:
        default:
            return "unknown";
    }
}

/* ========================================================================= */
/* Expected origin for public range                                          */
/* ========================================================================= */

static bool
vitte_diagnostic_registry_origin_matches_range(
    const vitte_diagnostic_registry_entry_t *entry
)
{
    vitte_diagnostic_registry_range_t range;

    if (entry == NULL) {
        return false;
    }

    range =
        vitte_diagnostic_registry_range(
            entry->public_code
        );

    switch (range) {
        case VITTE_DIAGNOSTIC_RANGE_LEXER:
            return entry->origin ==
                   VITTE_DIAGNOSTIC_ORIGIN_LEXER;

        case VITTE_DIAGNOSTIC_RANGE_PARSER:
            return entry->origin ==
                   VITTE_DIAGNOSTIC_ORIGIN_PARSER;

        case VITTE_DIAGNOSTIC_RANGE_MODULE:
            return entry->origin ==
                   VITTE_DIAGNOSTIC_ORIGIN_IMPORT;

        case VITTE_DIAGNOSTIC_RANGE_BACKEND:
            return entry->origin ==
                       VITTE_DIAGNOSTIC_ORIGIN_C17_BACKEND ||
                   entry->origin ==
                       VITTE_DIAGNOSTIC_ORIGIN_C_COMPILER ||
                   entry->origin ==
                       VITTE_DIAGNOSTIC_ORIGIN_IO ||
                   entry->origin ==
                       VITTE_DIAGNOSTIC_ORIGIN_LINKER ||
                   entry->origin ==
                       VITTE_DIAGNOSTIC_ORIGIN_RUNTIME;

        case VITTE_DIAGNOSTIC_RANGE_IR:
            return entry->origin ==
                       VITTE_DIAGNOSTIC_ORIGIN_IR ||
                   entry->origin ==
                       VITTE_DIAGNOSTIC_ORIGIN_HIR;

        /*
         * The remaining ranges intentionally permit more than one compiler
         * phase. For example, target diagnostics may originate in driver,
         * backend or FFI validation.
         */
        default:
            return true;
    }
}

/* ========================================================================= */
/* Entry validation                                                          */
/* ========================================================================= */

static bool
vitte_diagnostic_registry_entry_valid(
    const vitte_diagnostic_registry_entry_t *entry
)
{
    char prefix;
    unsigned number;

    (void)number;

    if (entry == NULL ||
        entry->internal_code == NULL ||
        entry->internal_code[0] == '\0' ||
        entry->public_code == NULL ||
        entry->public_code[0] == '\0' ||
        entry->category == NULL ||
        entry->category[0] == '\0' ||
        entry->title == NULL ||
        entry->title[0] == '\0' ||
        entry->explanation == NULL ||
        entry->explanation[0] == '\0') {
        return false;
    }

    if (!vitte_diagnostic_severity_is_valid(
            entry->default_severity
        )) {
        return false;
    }

    if (!vitte_diagnostic_origin_is_valid(
            entry->origin
        )) {
        return false;
    }

    if (!vitte_diagnostic_registry_parse_public_code(
            entry->public_code,
            &prefix,
            &number
        )) {
        return false;
    }

    if (prefix == 'E' &&
        entry->default_severity !=
            VITTE_DIAGNOSTIC_ERROR &&
        entry->default_severity !=
            VITTE_DIAGNOSTIC_FATAL) {
        return false;
    }

    if (prefix == 'W' &&
        entry->default_severity !=
            VITTE_DIAGNOSTIC_WARNING) {
        return false;
    }

    if (prefix == 'N' &&
        entry->default_severity !=
            VITTE_DIAGNOSTIC_NOTE) {
        return false;
    }

    if (prefix == 'H' &&
        entry->default_severity !=
            VITTE_DIAGNOSTIC_HELP) {
        return false;
    }

    if (!vitte_diagnostic_registry_origin_matches_range(
            entry
        )) {
        return false;
    }

    return true;
}

/* ========================================================================= */
/* Duplicate detection                                                       */
/* ========================================================================= */

static bool
vitte_diagnostic_registry_has_duplicate_internal(
    size_t index
)
{
    size_t other;

    for (other = 0u;
         other < index;
         other++) {

        if (strcmp(
                vitte_registry_entries[index].internal_code,
                vitte_registry_entries[other].internal_code
            ) == 0) {
            return true;
        }
    }

    return false;
}

static bool
vitte_diagnostic_registry_has_duplicate_public(
    size_t index
)
{
    size_t other;

    for (other = 0u;
         other < index;
         other++) {

        if (strcmp(
                vitte_registry_entries[index].public_code,
                vitte_registry_entries[other].public_code
            ) == 0) {
            return true;
        }
    }

    return false;
}

/* ========================================================================= */
/* Full validation                                                           */
/* ========================================================================= */

vitte_status_t
vitte_diagnostic_registry_validate(
    vitte_diagnostic_registry_validation_t *validation
)
{
    size_t index;
    size_t count;

    vitte_diagnostic_registry_validation_t local;

    memset(
        &local,
        0,
        sizeof(local)
    );

    count =
        vitte_registry_count_internal();

    local.entry_count = count;
    local.valid = true;

    for (index = 0u;
         index < count;
         index++) {

        const vitte_diagnostic_registry_entry_t *entry;

        entry =
            &vitte_registry_entries[index];

        if (!vitte_diagnostic_registry_entry_valid(
                entry
            )) {

            local.invalid_entry_count++;

            if (local.first_invalid_index ==
                0u &&
                index != 0u) {

                local.first_invalid_index =
                    index;
            }

            local.valid = false;
        }

        if (vitte_diagnostic_registry_has_duplicate_internal(
                index
            )) {

            local.duplicate_internal_count++;
            local.valid = false;
        }

        if (vitte_diagnostic_registry_has_duplicate_public(
                index
            )) {

            local.duplicate_public_count++;
            local.valid = false;
        }
    }

    if (validation != NULL) {
        *validation = local;
    }

    return local.valid
        ? VITTE_STATUS_OK
        : VITTE_STATUS_ERROR_INVALID_STATE;
}

/* ========================================================================= */
/* Counting by range                                                         */
/* ========================================================================= */

size_t
vitte_diagnostic_registry_count_range(
    vitte_diagnostic_registry_range_t range
)
{
    size_t index;
    size_t count;
    size_t matches;

    count =
        vitte_registry_count_internal();

    matches = 0u;

    for (index = 0u;
         index < count;
         index++) {

        if (vitte_diagnostic_registry_range(
                vitte_registry_entries[index].public_code
            ) == range) {
            matches++;
        }
    }

    return matches;
}

/* ========================================================================= */
/* Counting by severity                                                      */
/* ========================================================================= */

size_t
vitte_diagnostic_registry_count_severity(
    vitte_diagnostic_severity_t severity
)
{
    size_t index;
    size_t count;
    size_t matches;

    if (!vitte_diagnostic_severity_is_valid(
            severity
        )) {
        return 0u;
    }

    count =
        vitte_registry_count_internal();

    matches = 0u;

    for (index = 0u;
         index < count;
         index++) {

        if (vitte_registry_entries[index].default_severity ==
            severity) {
            matches++;
        }
    }

    return matches;
}

/* ========================================================================= */
/* Counting by origin                                                        */
/* ========================================================================= */

size_t
vitte_diagnostic_registry_count_origin(
    vitte_diagnostic_origin_t origin
)
{
    size_t index;
    size_t count;
    size_t matches;

    if (!vitte_diagnostic_origin_is_valid(
            origin
        )) {
        return 0u;
    }

    count =
        vitte_registry_count_internal();

    matches = 0u;

    for (index = 0u;
         index < count;
         index++) {

        if (vitte_registry_entries[index].origin ==
            origin) {
            matches++;
        }
    }

    return matches;
}

/* ========================================================================= */
/* Documentation                                                             */
/* ========================================================================= */

vitte_status_t
vitte_diagnostic_registry_format_explanation(
    const char *code,
    char *buffer,
    size_t capacity
)
{
    const vitte_diagnostic_registry_entry_t *entry;
    int written;

    if (code == NULL ||
        buffer == NULL ||
        capacity == 0u) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    entry =
        vitte_diagnostic_registry_find(
            code
        );

    if (entry == NULL) {
        buffer[0] = '\0';

        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    written =
        snprintf(
            buffer,
            capacity,
            "%s: %s\n"
            "\n"
            "%s\n"
            "\n"
            "category: %s\n"
            "phase: %s\n"
            "severity: %s\n"
            "internal: %s\n",
            entry->public_code,
            entry->title,
            entry->explanation,
            entry->category,
            vitte_diagnostic_origin_name(
                entry->origin
            ),
            vitte_diagnostic_severity_name(
                entry->default_severity
            ),
            entry->internal_code
        );

    if (written < 0) {
        buffer[0] = '\0';

        return VITTE_STATUS_ERROR_INVALID_STATE;
    }

    if ((size_t)written >= capacity) {
        buffer[capacity - 1u] = '\0';

        return VITTE_STATUS_ERROR_UNSUPPORTED;
    }

    return VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Dump                                                                      */
/* ========================================================================= */

vitte_status_t
vitte_diagnostic_registry_dump(
    FILE *stream
)
{
    size_t index;
    size_t count;

    if (stream == NULL) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    count =
        vitte_registry_count_internal();

    for (index = 0u;
         index < count;
         index++) {

        const vitte_diagnostic_registry_entry_t *entry;

        entry =
            &vitte_registry_entries[index];

        if (fprintf(
                stream,
                "%-6s %-8s %-18s %-20s %s\n",
                entry->public_code,
                vitte_diagnostic_severity_name(
                    entry->default_severity
                ),
                vitte_diagnostic_origin_name(
                    entry->origin
                ),
                entry->category,
                entry->title
            ) < 0) {

            return VITTE_STATUS_ERROR_INVALID_STATE;
        }
    }

    return VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Machine-readable dump                                                     */
/* ========================================================================= */

static void
vitte_registry_json_string(
    FILE *stream,
    const char *text
)
{
    const unsigned char *cursor;

    if (stream == NULL) {
        return;
    }

    if (text == NULL) {
        fputs("null", stream);
        return;
    }

    fputc('"', stream);

    cursor =
        (const unsigned char *)text;

    while (*cursor != '\0') {
        switch (*cursor) {
            case '"':
                fputs("\\\"", stream);
                break;

            case '\\':
                fputs("\\\\", stream);
                break;

            case '\b':
                fputs("\\b", stream);
                break;

            case '\f':
                fputs("\\f", stream);
                break;

            case '\n':
                fputs("\\n", stream);
                break;

            case '\r':
                fputs("\\r", stream);
                break;

            case '\t':
                fputs("\\t", stream);
                break;

            default:
                if (*cursor < 0x20u) {
                    fprintf(
                        stream,
                        "\\u%04x",
                        (unsigned)*cursor
                    );
                } else {
                    fputc(
                        (int)*cursor,
                        stream
                    );
                }

                break;
        }

        cursor++;
    }

    fputc('"', stream);
}

vitte_status_t
vitte_diagnostic_registry_dump_json(
    FILE *stream
)
{
    size_t index;
    size_t count;

    if (stream == NULL) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    count =
        vitte_registry_count_internal();

    fputs("{\"diagnostics\":[", stream);

    for (index = 0u;
         index < count;
         index++) {

        const vitte_diagnostic_registry_entry_t *entry;

        entry =
            &vitte_registry_entries[index];

        if (index != 0u) {
            fputc(',', stream);
        }

        fputc('{', stream);

        fputs("\"code\":", stream);
        vitte_registry_json_string(
            stream,
            entry->public_code
        );

        fputs(",\"internal\":", stream);
        vitte_registry_json_string(
            stream,
            entry->internal_code
        );

        fputs(",\"severity\":", stream);
        vitte_registry_json_string(
            stream,
            vitte_diagnostic_severity_name(
                entry->default_severity
            )
        );

        fputs(",\"phase\":", stream);
        vitte_registry_json_string(
            stream,
            vitte_diagnostic_origin_name(
                entry->origin
            )
        );

        fputs(",\"category\":", stream);
        vitte_registry_json_string(
            stream,
            entry->category
        );

        fputs(",\"title\":", stream);
        vitte_registry_json_string(
            stream,
            entry->title
        );

        fputs(",\"explanation\":", stream);
        vitte_registry_json_string(
            stream,
            entry->explanation
        );

        fputs(",\"deprecated\":", stream);

        fputs(
            entry->deprecated
                ? "true"
                : "false",
            stream
        );

        fputc('}', stream);
    }

    fputs("]}", stream);

    if (ferror(stream)) {
        return VITTE_STATUS_ERROR_INVALID_STATE;
    }

    return VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Explain                                                                   */
/* ========================================================================= */

vitte_status_t
vitte_diagnostic_registry_explain(
    FILE *stream,
    const char *code
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    if (stream == NULL ||
        code == NULL) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    entry =
        vitte_diagnostic_registry_find(
            code
        );

    if (entry == NULL) {
        return VITTE_STATUS_ERROR_INVALID_ARGUMENT;
    }

    if (fprintf(
            stream,
            "%s: %s\n\n"
            "%s\n\n"
            "Category: %s\n"
            "Phase: %s\n"
            "Severity: %s\n"
            "Internal identifier: %s\n",
            entry->public_code,
            entry->title,
            entry->explanation,
            entry->category,
            vitte_diagnostic_origin_name(
                entry->origin
            ),
            vitte_diagnostic_severity_name(
                entry->default_severity
            ),
            entry->internal_code
        ) < 0) {

        return VITTE_STATUS_ERROR_INVALID_STATE;
    }

    return VITTE_STATUS_OK;
}

/* ========================================================================= */
/* Registry fingerprint                                                      */
/* ========================================================================= */

/*
 * Stable registry fingerprint useful for:
 *
 *   - compiler build metadata;
 *   - diagnostic protocol compatibility;
 *   - LSP cache invalidation;
 *   - test fixtures;
 *   - generated documentation validation.
 *
 * This is not used as a public diagnostic code.
 */

#define VITTE_REGISTRY_FNV_OFFSET \
    UINT64_C(14695981039346656037)

#define VITTE_REGISTRY_FNV_PRIME \
    UINT64_C(1099511628211)

static uint64_t
vitte_registry_hash_string(
    uint64_t hash,
    const char *text
)
{
    const unsigned char *cursor;

    if (text == NULL) {
        hash ^= UINT64_C(0xff);
        hash *= VITTE_REGISTRY_FNV_PRIME;

        return hash;
    }

    cursor =
        (const unsigned char *)text;

    while (*cursor != '\0') {
        hash ^= (uint64_t)*cursor;
        hash *= VITTE_REGISTRY_FNV_PRIME;

        cursor++;
    }

    hash ^= UINT64_C(0);
    hash *= VITTE_REGISTRY_FNV_PRIME;

    return hash;
}

uint64_t
vitte_diagnostic_registry_fingerprint(void)
{
    uint64_t hash;
    size_t index;
    size_t count;

    hash =
        VITTE_REGISTRY_FNV_OFFSET;

    count =
        vitte_registry_count_internal();

    for (index = 0u;
         index < count;
         index++) {

        const vitte_diagnostic_registry_entry_t *entry;

        entry =
            &vitte_registry_entries[index];

        hash =
            vitte_registry_hash_string(
                hash,
                entry->internal_code
            );

        hash =
            vitte_registry_hash_string(
                hash,
                entry->public_code
            );

        hash =
            vitte_registry_hash_string(
                hash,
                entry->category
            );

        hash =
            vitte_registry_hash_string(
                hash,
                entry->title
            );

        hash =
            vitte_registry_hash_string(
                hash,
                entry->explanation
            );

        hash ^= (uint64_t)entry->default_severity;
        hash *= VITTE_REGISTRY_FNV_PRIME;

        hash ^= (uint64_t)entry->origin;
        hash *= VITTE_REGISTRY_FNV_PRIME;

        hash ^= entry->deprecated
            ? UINT64_C(1)
            : UINT64_C(0);

        hash *= VITTE_REGISTRY_FNV_PRIME;
    }

    return hash;
}

#undef VITTE_REGISTRY_FNV_PRIME
#undef VITTE_REGISTRY_FNV_OFFSET
