#include "help.h"
#include "registry.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Vitte diagnostic contextual-help subsystem.
 *
 * This module turns structured diagnostic information into conservative,
 * actionable remediation guidance.
 *
 * The diagnostic registry remains authoritative for diagnostic identity and
 * metadata. This file owns remediation policy.
 *
 * Principles:
 *
 *   - stable public diagnostic codes drive generic remediation policy;
 *   - explicit HELP annotations always have priority;
 *   - structured semantic information refines generic policy;
 *   - source excerpts are used only when they are safe and exact;
 *   - no source-specific fact is invented;
 *   - no automatic conversion is recommended unless its intent is known;
 *   - no fix-it is fabricated here;
 *   - output remains deterministic;
 *   - malformed diagnostics must not crash help formatting;
 *   - UTF-8 source text must never be split;
 *   - terminal control characters are never copied into generated help;
 *   - C17.
 */

/* ========================================================================= */
/* Constants                                                                 */
/* ========================================================================= */

#define VITTE_HELP_EXCERPT_CAPACITY       ((size_t)160u)
#define VITTE_HELP_REASON_LIMIT           ((size_t)192u)
#define VITTE_HELP_SUBJECT_LIMIT          ((size_t)128u)
#define VITTE_HELP_TYPE_LIMIT             ((size_t)128u)
#define VITTE_HELP_CONTRACT_LIMIT         ((size_t)192u)
#define VITTE_HELP_EXPECTED_TOKEN_LIMIT   ((size_t)96u)

/* ========================================================================= */
/* Static remediation registry                                               */
/* ========================================================================= */

typedef struct vitte_diagnostic_help_rule {
    const char *code;
    const char *help;
} vitte_diagnostic_help_rule_t;

/*
 * Public IDs are stable.
 *
 * Keep exactly one generic remediation policy for every registered diagnostic
 * that is intended to expose user-facing help.
 *
 * Registry coverage tests should fail when a newly registered public
 * diagnostic has no corresponding policy.
 *
 * Policies intentionally remain generic. Source-specific corrections are
 * produced later from structured diagnostic fields.
 */
static const vitte_diagnostic_help_rule_t vitte_help_rules[] = {
    {
        "E0000",
        "Capture the failing command and compiler version; this failure has no identified cause and needs a compiler bug report."
    },
    {
        "E0001",
        "Rerun with VITTE_BACKTRACE=1 and report the failing input, command and compiler version."
    },
    {
        "E0002",
        "Capture the violated invariant with VITTE_BACKTRACE=1; report the input that triggers this compiler defect."
    },
    {
        "E0003",
        "Capture the impossible state with VITTE_BACKTRACE=1 and include the smallest reproducing input in a compiler bug report."
    },
    {
        "E0004",
        "Check available memory and process resource limits; retry with fewer compilation jobs if the system is under memory pressure."
    },
    {
        "E0005",
        "Resolve the source path relative to the working directory and ensure that it names an existing, readable regular file."
    },
    {
        "E0006",
        "Save the source as UTF-8 and replace the invalid byte sequence at the reported offset."
    },
    {
        "E0007",
        "Register the original source before mapping its spans; preserve the generated file and source mapping in a compiler bug report."
    },
    {
        "E0008",
        "Report the diagnostic construction failure with its original message, source span and compiler version."
    },
    {
        "E0009",
        "Fix the first reported error before rerunning; later diagnostics were suppressed after the configured diagnostic limit was reached."
    },
    {
        "E0010",
        "Select terminal diagnostic output to inspect the formatting failure; report the failing output format and reproducing input."
    },
    {
        "E0011",
        "Correct the option named in the diagnostic and use `vitte --help` to verify its accepted values and combinations."
    },
    {
        "E0012",
        "Resolve the first diagnostic emitted by the failed compiler phase; if none was emitted, report the phase and reproducing command."
    },
    {
        "E0013",
        "Identify the unavailable compiler component or external tool from the reported cause and install, configure or report that component."
    },
    {
        "E0014",
        "Check the source with the command-line compiler for detailed parser and type diagnostics, then correct the reported source issue."
    },

    {
        "E0101",
        "Remove the invalid character or place it inside a quoted string when it is intended to be text."
    },
    {
        "E0102",
        "Close the string with the matching quote before the line ends; escape a quote that belongs inside the string."
    },
    {
        "E0103",
        "Replace the invalid escape with a supported escape sequence or escape the backslash when a literal backslash is intended."
    },
    {
        "E0104",
        "Use digits valid for the literal's base and place separators only where the numeric grammar permits them; verify the prefix and suffix."
    },
    {
        "E0105",
        "Reduce the numeric literal to a representable value; lexical overflow must be corrected before type conversion can occur."
    },
    {
        "E0106",
        "Add the missing block-comment terminator, including the terminator of any nested comment that remains open."
    },
    {
        "E0107",
        "Start the identifier with a permitted identifier-start character and remove characters that cannot occur in an identifier."
    },
    {
        "E0108",
        "Replace the malformed Unicode sequence with valid UTF-8 source text before compiling again."
    },

    {
        "E0201",
        "Insert the expected delimiter, keyword or syntactic element before the token reported as found."
    },
    {
        "E0202",
        "Remove the unexpected token or finish the preceding construct before beginning this one."
    },
    {
        "E0203",
        "Provide a valid expression at this position, such as a literal, visible name, call or other expression accepted by the grammar."
    },
    {
        "E0204",
        "Introduce a declaration at this position; move executable statements into a procedure or another context that accepts statements."
    },
    {
        "E0205",
        "Provide a valid type at this position, such as a primitive type or a visible declared type."
    },
    {
        "E0206",
        "Provide a pattern compatible with the matched value before the branch body."
    },
    {
        "E0207",
        "Close the opening delimiter with its matching closing delimiter before the enclosing construct ends."
    },
    {
        "E0208",
        "Replace the closing delimiter with the delimiter matching the relevant unclosed opener."
    },
    {
        "E0209",
        "Remove this modifier or apply it only to a declaration kind that accepts it."
    },

    {
        "E0301",
        "Match the module path to a source available under a configured import root, including its spelling and case."
    },
    {
        "E0302",
        "Import an exported symbol or change the declaration's visibility when external access is intentionally part of the module API."
    },
    {
        "E0303",
        "Break the reported import cycle by moving shared declarations into a module that does not depend back on either side of the cycle."
    },
    {
        "E0304",
        "Remove the duplicate import or assign distinct aliases to imported declarations that would otherwise conflict."
    },
    {
        "E0305",
        "Install or declare the missing package and make its source root available to import resolution."
    },
    {
        "E0306",
        "Select a package version satisfying the declared constraint, or update the constraint after checking API compatibility."
    },
    {
        "E0307",
        "Preserve the import graph, failing validation detail and compiler version; this structural failure should be reported as a compiler defect."
    },

    {
        "E0401",
        "Declare or import the unresolved name in this scope and verify its spelling against visible declarations."
    },
    {
        "E0402",
        "Rename this declaration or remove the earlier declaration with the same name in this scope."
    },
    {
        "E0403",
        "Qualify the name with the intended module or use an alias that selects exactly one visible declaration."
    },
    {
        "E0404",
        "Use an exported API or change the declaration's visibility when this access is intentionally permitted."
    },
    {
        "E0405",
        "Move the use into the declaration's valid scope or pass the required value into the procedure that needs it."
    },
    {
        "E0406",
        "Give every binding introduced by this pattern or declaration a distinct name."
    },

    {
        "E0501",
        "Make the expression satisfy the required type; use an explicit conversion only when it preserves the intended value and semantics."
    },
    {
        "E0502",
        "Add an explicit type annotation or provide an initializer whose type determines the declaration unambiguously."
    },
    {
        "E0503",
        "Use a conversion supported by both types; parse or format textual representations explicitly instead of relying on an unrelated cast."
    },
    {
        "E0504",
        "Call a procedure or callable value and check whether another declaration shadows the intended procedure."
    },
    {
        "E0505",
        "Use a value supporting indexed access or access this value through operations provided by its declared type."
    },
    {
        "E0506",
        "Use a member declared by the receiver's type and verify both the receiver type and member spelling."
    },
    {
        "E0507",
        "Change the type argument or implementation so that it satisfies the constraint named by the diagnostic."
    },
    {
        "E0508",
        "Provide the complete type definition before performing an operation that requires its size, layout or members."
    },
    {
        "E0509",
        "Break the recursive by-value type cycle with a supported indirection or redesign the recursive representation."
    },
    {
        "E0510",
        "Declare or import the type in this scope and verify the spelling and visibility of its declaration."
    },
    {
        "E0511",
        "Use a type name at this position instead of a value, variable or procedure name."
    },
    {
        "E0512",
        "Choose an operator defined for these operand types or explicitly convert the operands to compatible types."
    },
    {
        "E0513",
        "Assign only to a writable mutable location such as a mutable binding, field or index."
    },
    {
        "E0514",
        "Use an index of the type required by this collection and verify that the index expression represents an element position."
    },
    {
        "E0515",
        "Use a boolean condition, normally by writing an explicit comparison; Vitte does not infer truthiness from unrelated value types."
    },

    {
        "E0601",
        "Match the number of supplied arguments to the selected procedure's parameter list."
    },
    {
        "E0602",
        "Pass an expression satisfying the declared parameter type at this argument position."
    },
    {
        "E0603",
        "Compare the argument count and types with the available signatures and select a callable candidate that accepts them."
    },
    {
        "E0604",
        "Qualify the procedure or make argument types explicit so that exactly one callable candidate matches."
    },
    {
        "E0605",
        "Return an expression satisfying the procedure's declared result type or correct the result declaration when the API is wrong."
    },
    {
        "E0606",
        "Remove the incompatible call modifier or select a procedure supporting the required calling context."
    },
    {
        "E0607",
        "Move `return` into a procedure body; a top-level declaration cannot return a procedure result."
    },

    {
        "E0700",
        "Use a well-typed boolean expression that is valid in this contract's declaration context."
    },
    {
        "E0701",
        "Adjust the call arguments or caller state so the requires condition holds before entering the procedure."
    },
    {
        "E0702",
        "Correct the returned value or resulting state so every successful return satisfies the ensures condition."
    },
    {
        "E0703",
        "Restore the invariant before this program point and preserve it on every control-flow path reaching the check."
    },
    {
        "E0704",
        "Make the requires clause a boolean expression referring only to values available before the call."
    },
    {
        "E0705",
        "Make the ensures clause a boolean expression valid after the procedure has produced its result."
    },
    {
        "E0706",
        "Capture the required entry value through a supported contract mechanism before referring to its previous value."
    },
    {
        "E0707",
        "Use `result` only in a contract context where a procedure result exists."
    },
    {
        "E0708",
        "Provide the missing precondition or simplify the assertion so the checker has enough information to prove the required relationship."
    },
    {
        "E0709",
        "Remove or correct contradictory contract clauses so at least one valid program state can satisfy the contract."
    },
    {
        "E0710",
        "Move state changes and I/O out of the contract and compute the condition only from permitted side-effect-free values."
    },
    {
        "E0711",
        "Replace the call inside the contract with an operation permitted during contract evaluation."
    },
    {
        "E0712",
        "Align the implementation contract with the declared API: do not strengthen caller requirements or weaken promised results."
    },
    {
        "E0713",
        "Inspect the values reported by the failed runtime contract check and correct the caller or implementation violating the condition."
    },
    {
        "E0714",
        "Report the contract expression, reproducing input and compiler version because contract analysis failed internally."
    },

    {
        "E0801",
        "Choose a generic argument satisfying the named bound or provide the implementation required by that bound."
    },
    {
        "E0802",
        "Supply one type or value argument for every required generic parameter in declaration order."
    },
    {
        "E0810",
        "Implement the required trait for this type or use a type that already provides the required implementation."
    },
    {
        "E0811",
        "Disambiguate the trait relation so exactly one implementation matches the type and generic arguments."
    },
    {
        "E0812",
        "Remove or specialize the overlapping implementation so a trait/type relation resolves to exactly one implementation."
    },

    {
        "E0901",
        "Preserve the generated C and the external compiler's first error, then report the Vitte construct that produced invalid C17."
    },
    {
        "E0902",
        "Reduce the failing construct to a reproducer and report the C17 lowering failure together with its backend detail."
    },
    {
        "E0903",
        "Rewrite the construct using features supported by the selected backend or select a backend that implements the required feature."
    },
    {
        "E0904",
        "Inspect the external C compiler's first error, verify C17 support and preserve the generated C for a reproducible report."
    },
    {
        "E0905",
        "Preserve the generated C and original Vitte source together and report the incorrect source mapping."
    },
    {
        "E0906",
        "Choose a writable output path, check the parent directory permissions and available storage, then retry the output operation."
    },
    {
        "E0907",
        "Verify linker availability, the selected target and library search paths, then inspect the first linker error and command."
    },
    {
        "E0908",
        "Inspect the generated program's exit status and runtime output; compilation and linking completed before this failure."
    },
    {
        "E0909",
        "Verify the selected C17 backend and its configuration values, then report the invalid option and reproducing command."
    },

    {
        "E1001",
        "Report the invalid IR instruction and its originating source because compiler-generated IR must satisfy the verifier."
    },
    {
        "E1002",
        "Report the source construct that failed IR lowering together with the failing phase and compiler version."
    },
    {
        "E1003",
        "Preserve the verifier's first invariant failure and the corresponding IR dump in a compiler bug report."
    },
    {
        "E1010",
        "Report the invalid HIR construct and its originating source so frontend lowering can be corrected."
    },
    {
        "E1011",
        "Report the source construct and compiler version; the native frontend failed while building or validating its high-level representation."
    },

    {
        "E1101",
        "Add `return` with a value satisfying the declared result type on every reachable path, including fallthrough."
    },
    {
        "E1102",
        "Move `break` into a breakable construct or use the appropriate control-flow operation for leaving the current context."
    },
    {
        "E1103",
        "Move `continue` into a loop body; outside a loop there is no next iteration."
    },
    {
        "E1104",
        "Correct the preceding branch, termination or control-flow condition so this operation is reachable when it is intended to execute."
    },

    {
        "E1201",
        "Verify that the referenced allocation remains alive and that the accessed range lies inside the allocation."
    },
    {
        "E1202",
        "Handle the null case before dereferencing or provide a valid initialized reference."
    },
    {
        "E1203",
        "Use a pointer to the required pointee type and verify the pointee representation before performing an explicit conversion."
    },
    {
        "E1204",
        "Allocate or offset the storage so the accessed value satisfies the required alignment."
    },
    {
        "E1205",
        "Keep the referenced value alive for at least as long as the reference, and use a direct local or parameter borrow whose origin the bootstrap can track."
    },
    {
        "E1206",
        "End the existing borrow before creating an overlapping exclusive reference; use shared references only when no mutation is required."
    },
    {
        "E1207",
        "Wait until the incompatible reference is no longer used, or access the value through the active reference with its permitted mutability."
    },
    {
        "E1208",
        "End every reference to the value before moving it, or move a different local value that is not currently borrowed."
    },
    {
        "E1209",
        "Initialize the binding again before using it, or remove the move if the original value is still needed."
    },

    {
        "E1301",
        "Prefer a safe API; when this low-level operation is required, establish its safety invariants and place it in an explicit unsafe context."
    },
    {
        "E1302",
        "Match assembly instructions, operands and constraints to the selected target architecture."
    },
    {
        "E1303",
        "Use the intrinsic's required argument types and compile-time values on a target that supports the intrinsic."
    },

    {
        "E1401",
        "Choose an ABI supported by this target and match the foreign declaration's calling convention."
    },
    {
        "E1402",
        "Use a foreign-compatible representation and explicitly marshal values whose Vitte representation cannot cross the foreign interface."
    },
    {
        "E1403",
        "Match the foreign variadic signature and apply the ABI-required argument promotions to every trailing argument."
    },
    {
        "E1404",
        "Match the link name to the actual exported foreign symbol and remove conflicting foreign declarations."
    },

    {
        "E1501",
        "Replace runtime-dependent values with compile-time constants or move this computation out of the constant-evaluation context."
    },
    {
        "E1502",
        "Reduce the constant operands or use a representable wider type before performing the overflowing operation."
    },
    {
        "E1503",
        "Change the zero divisor or guard the operation so constant evaluation never performs division by zero."
    },
    {
        "E1504",
        "Break the constant dependency cycle by giving at least one value an independent initializer."
    },
    {
        "E1505",
        "Reduce compile-time recursion or work and verify termination before increasing an evaluation limit."
    },

    {
        "E1601",
        "Add branches for the uncovered variants or a valid catch-all pattern when all remaining values should be handled uniformly."
    },
    {
        "E1602",
        "Remove the already covered pattern or place a more specific pattern before the branch that subsumes it."
    },
    {
        "E1603",
        "Use a pattern compatible with the scrutinee type and correct incompatible variants or literals."
    },
    {
        "E1604",
        "Make every alternative introduce the same binding names with compatible binding types."
    },

    {
        "E1701",
        "Move the asynchronous operation into a compatible async procedure or use the supported synchronous operation."
    },
    {
        "E1702",
        "Await a task-compatible result instead of an already computed ordinary value."
    },
    {
        "E1710",
        "Transfer an owned send-compatible value or use a supported synchronization mechanism between concurrent tasks."
    },
    {
        "E1711",
        "Move the forbidden blocking or suspending operation outside the critical section."
    },

    {
        "E1801",
        "Correct the macro arguments or expansion so it produces syntax valid at this invocation site."
    },
    {
        "E1802",
        "Break recursive macro expansion or reduce its nesting before increasing any expansion limit."
    },
    {
        "E1810",
        "Capture the failing compiler pass, its input and compiler version in a compiler bug report."
    },
    {
        "E1811",
        "Preserve the representation immediately before and after the failing pass together with the verifier's first error."
    },

    {
        "E1901",
        "Select a compilation target implemented by this compiler and supported by the installed toolchain."
    },
    {
        "E1902",
        "Choose a supported CPU architecture and ensure it matches the selected target and external toolchain."
    },
    {
        "E1903",
        "Remove the unsupported target feature or select a CPU and toolchain providing that feature."
    },
    {
        "E1904",
        "Select a target with a supported pointer width and verify pointer-sized assumptions in foreign interfaces."
    },
    {
        "E1905",
        "Use an operation supporting the target byte order or explicitly encode and decode the required endianness."
    },

    {
        "W0001",
        "Remove the unused local binding or use its value in the computation that requires it."
    },
    {
        "W0002",
        "Use the parameter in the procedure body or remove it and update callers when it is not part of the required API."
    },
    {
        "W0003",
        "Remove the import when no declaration from it is required."
    },
    {
        "W0004",
        "Handle the returned value, including any error result, before discarding it intentionally."
    },
    {
        "W0010",
        "Replace the deprecated declaration with its documented successor."
    },
    {
        "W0020",
        "Remove statements after the unconditional exit or correct the control flow that makes them unreachable."
    },
    {
        "W0030",
        "Rename the inner binding when hiding the outer declaration is unintended."
    },
    {
        "W0040",
        "Verify that the implicit conversion preserves the intended value; use compatible types or a deliberate checked conversion."
    },
    {
        "W0041",
        "Verify the source value's range before conversion and use a wider representation when significant information would be lost."
    },
    {
        "W0042",
        "Check for negative or out-of-range values before changing signedness."
    },
    {
        "W0050",
        "Remove the cast because the expression already has the requested type."
    },
    {
        "W0051",
        "Remove the redundant condition or correct the expression that makes its result constant."
    },
    {
        "W0060",
        "Check both operands and the comparison operator for a copied name, incompatible range or unintended comparison."
    },
    {
        "W0070",
        "Use a stable foreign representation and verify field offsets, size and alignment against the foreign declaration."
    },
    {
        "W0080",
        "Keep the runtime contract check or provide enough constraints for the compiler to prove the condition statically."
    },
    {
        "W0090",
        "Inspect the reported I/O or compiler operation and restore the required resource before relying on its output."
    }
};

#define VITTE_HELP_RULE_COUNT \
    (sizeof(vitte_help_rules) / sizeof(vitte_help_rules[0]))

/* ========================================================================= */
/* Small helpers                                                             */
/* ========================================================================= */

static bool
vitte_help_text_present(
    const char *text
)
{
    return text != NULL &&
           text[0] != '\0';
}

static size_t
vitte_help_min_size(
    size_t left,
    size_t right
)
{
    return left < right
        ? left
        : right;
}

static bool
vitte_help_ascii_equal_case_insensitive(
    const char *left,
    const char *right
)
{
    unsigned char a;
    unsigned char b;

    if (left == NULL ||
        right == NULL) {
        return left == right;
    }

    while (*left != '\0' &&
           *right != '\0') {

        a = (unsigned char)*left;
        b = (unsigned char)*right;

        if (tolower(a) !=
            tolower(b)) {
            return false;
        }

        left++;
        right++;
    }

    return *left == '\0' &&
           *right == '\0';
}

static bool
vitte_help_code_equal(
    const char *left,
    const char *right
)
{
    return vitte_help_ascii_equal_case_insensitive(
        left,
        right
    );
}

/* ========================================================================= */
/* Bounded output builder                                                    */
/* ========================================================================= */

typedef struct vitte_help_builder {
    char *buffer;
    size_t capacity;
    size_t length;
    bool truncated;
    bool failed;
} vitte_help_builder_t;

static void
vitte_help_builder_init(
    vitte_help_builder_t *builder,
    char *buffer,
    size_t capacity
)
{
    if (builder == NULL) {
        return;
    }

    builder->buffer = buffer;
    builder->capacity = capacity;
    builder->length = 0u;
    builder->truncated = false;
    builder->failed = false;

    if (buffer == NULL ||
        capacity == 0u) {
        builder->failed = true;
        return;
    }

    buffer[0] = '\0';
}

static size_t
vitte_help_utf8_safe_prefix(
    const char *text,
    size_t length
)
{
    size_t result;

    if (text == NULL) {
        return 0u;
    }

    result = length;

    /*
     * If truncation ends in UTF-8 continuation bytes, back up to the leading
     * byte of the incomplete sequence.
     */
    while (result > 0u &&
           (((unsigned char)text[result] & 0xC0u) == 0x80u)) {
        result--;
    }

    if (result == length) {
        return result;
    }

    return result;
}

static void
vitte_help_builder_append_bytes(
    vitte_help_builder_t *builder,
    const char *text,
    size_t length
)
{
    size_t available;
    size_t written;

    if (builder == NULL ||
        builder->failed ||
        text == NULL ||
        length == 0u) {
        return;
    }

    if (builder->length >=
        builder->capacity - 1u) {
        builder->truncated = true;
        return;
    }

    available =
        builder->capacity -
        builder->length -
        1u;

    written =
        vitte_help_min_size(
            available,
            length
        );

    if (written < length) {
        written =
            vitte_help_utf8_safe_prefix(
                text,
                written
            );

        builder->truncated = true;
    }

    if (written == 0u) {
        return;
    }

    memcpy(
        builder->buffer + builder->length,
        text,
        written
    );

    builder->length += written;
    builder->buffer[builder->length] = '\0';
}

static void
vitte_help_builder_append(
    vitte_help_builder_t *builder,
    const char *text
)
{
    if (!vitte_help_text_present(text)) {
        return;
    }

    vitte_help_builder_append_bytes(
        builder,
        text,
        strlen(text)
    );
}

/* ========================================================================= */
/* Safe quoted fragments                                                     */
/* ========================================================================= */

static void
vitte_help_builder_append_quoted_fragment(
    vitte_help_builder_t *builder,
    const char *text,
    size_t maximum
)
{
    size_t index;
    size_t written;

    if (builder == NULL ||
        !vitte_help_text_present(text)) {
        return;
    }

    vitte_help_builder_append(
        builder,
        "`"
    );

    written = 0u;

    for (index = 0u;
         text[index] != '\0' &&
         written < maximum;
         index++) {

        unsigned char character;

        character =
            (unsigned char)text[index];

        if (character < 0x20u ||
            character == 0x7fu) {
            continue;
        }

        if (character == '`') {
            vitte_help_builder_append(
                builder,
                "\\`"
            );

            written++;
            continue;
        }

        vitte_help_builder_append_bytes(
            builder,
            &text[index],
            1u
        );

        written++;
    }

    if (text[index] != '\0') {
        vitte_help_builder_append(
            builder,
            "…"
        );
    }

    vitte_help_builder_append(
        builder,
        "`"
    );
}

/* ========================================================================= */
/* Public help policy lookup                                                 */
/* ========================================================================= */

const char *
vitte_diagnostic_help_for_code(
    const char *code
)
{
    size_t index;

    if (!vitte_help_text_present(code)) {
        return NULL;
    }

    for (index = 0u;
         index < VITTE_HELP_RULE_COUNT;
         index++) {

        if (vitte_help_code_equal(
                code,
                vitte_help_rules[index].code
            )) {

            return vitte_help_rules[index].help;
        }
    }

    return NULL;
}

/* ========================================================================= */
/* Explicit HELP annotations                                                 */
/* ========================================================================= */

bool
vitte_diagnostic_has_help(
    const vitte_diagnostic_t *diagnostic
)
{
    size_t index;

    if (diagnostic == NULL) {
        return false;
    }

    for (index = 0u;
         index < diagnostic->annotation_count;
         index++) {

        const vitte_diagnostic_annotation_t *annotation;

        annotation =
            &diagnostic->annotations[index];

        if (annotation->kind ==
                VITTE_DIAGNOSTIC_ANNOTATION_HELP &&
            vitte_help_text_present(
                annotation->message
            )) {

            return true;
        }
    }

    return false;
}

static const char *
vitte_help_explicit_annotation(
    const vitte_diagnostic_t *diagnostic
)
{
    size_t index;

    if (diagnostic == NULL) {
        return NULL;
    }

    for (index = 0u;
         index < diagnostic->annotation_count;
         index++) {

        const vitte_diagnostic_annotation_t *annotation;

        annotation =
            &diagnostic->annotations[index];

        if (annotation->kind ==
                VITTE_DIAGNOSTIC_ANNOTATION_HELP &&
            vitte_help_text_present(
                annotation->message
            )) {

            return annotation->message;
        }
    }

    return NULL;
}

/* ========================================================================= */
/* Registry resolution                                                       */
/* ========================================================================= */

static const vitte_diagnostic_registry_entry_t *
vitte_help_registry_entry(
    const vitte_diagnostic_t *diagnostic
)
{
    const vitte_diagnostic_registry_entry_t *entry;

    if (diagnostic == NULL) {
        return NULL;
    }

    entry = NULL;

    if (vitte_help_text_present(
            diagnostic->code
        )) {

        entry =
            vitte_diagnostic_registry_find_internal(
                diagnostic->code
            );

        if (entry == NULL) {
            entry =
                vitte_diagnostic_registry_find_public(
                    diagnostic->code
                );
        }
    }

    if (entry == NULL &&
        vitte_help_text_present(
            diagnostic->short_code
        )) {

        entry =
            vitte_diagnostic_registry_find_public(
                diagnostic->short_code
            );

        if (entry == NULL) {
            entry =
                vitte_diagnostic_registry_find_internal(
                    diagnostic->short_code
                );
        }
    }

    return entry;
}

static const char *
vitte_help_public_code(
    const vitte_diagnostic_t *diagnostic,
    const vitte_diagnostic_registry_entry_t *entry
)
{
    if (entry != NULL &&
        vitte_help_text_present(
            entry->public_code
        )) {

        return entry->public_code;
    }

    if (diagnostic != NULL &&
        vitte_help_text_present(
            diagnostic->short_code
        )) {

        return diagnostic->short_code;
    }

    return NULL;
}

/* ========================================================================= */
/* Type classification                                                       */
/* ========================================================================= */

static bool
vitte_help_type_is_one_of(
    const char *name,
    const char *const *types,
    size_t count
)
{
    size_t index;

    if (!vitte_help_text_present(name)) {
        return false;
    }

    for (index = 0u;
         index < count;
         index++) {

        if (strcmp(
                name,
                types[index]
            ) == 0) {

            return true;
        }
    }

    return false;
}

static bool
vitte_help_numeric_type(
    const char *name
)
{
    static const char *const types[] = {
        "int",
        "uint",
        "i8",
        "i16",
        "i32",
        "i64",
        "i128",
        "u8",
        "u16",
        "u32",
        "u64",
        "u128",
        "isize",
        "usize",
        "float",
        "f32",
        "f64"
    };

    return vitte_help_type_is_one_of(
        name,
        types,
        sizeof(types) / sizeof(types[0])
    );
}

static bool
vitte_help_integer_type(
    const char *name
)
{
    static const char *const types[] = {
        "int",
        "uint",
        "i8",
        "i16",
        "i32",
        "i64",
        "i128",
        "u8",
        "u16",
        "u32",
        "u64",
        "u128",
        "isize",
        "usize"
    };

    return vitte_help_type_is_one_of(
        name,
        types,
        sizeof(types) / sizeof(types[0])
    );
}

static bool
vitte_help_float_type(
    const char *name
)
{
    static const char *const types[] = {
        "float",
        "f32",
        "f64"
    };

    return vitte_help_type_is_one_of(
        name,
        types,
        sizeof(types) / sizeof(types[0])
    );
}

static bool
vitte_help_boolean_type(
    const char *name
)
{
    return name != NULL &&
           strcmp(name, "bool") == 0;
}

static bool
vitte_help_string_type(
    const char *name
)
{
    return name != NULL &&
           (
               strcmp(name, "string") == 0 ||
               strcmp(name, "str") == 0
           );
}

/* ========================================================================= */
/* Source excerpt                                                            */
/* ========================================================================= */

static bool
vitte_help_utf8_sequence_valid(
    const unsigned char *bytes,
    size_t remaining,
    size_t *width
)
{
    unsigned char first;

    if (bytes == NULL ||
        remaining == 0u ||
        width == NULL) {
        return false;
    }

    first = bytes[0];

    if (first <= 0x7fu) {
        *width = 1u;
        return true;
    }

    if (first >= 0xc2u &&
        first <= 0xdfu) {

        if (remaining < 2u ||
            (bytes[1] & 0xc0u) != 0x80u) {
            return false;
        }

        *width = 2u;
        return true;
    }

    if (first == 0xe0u) {
        if (remaining < 3u ||
            bytes[1] < 0xa0u ||
            bytes[1] > 0xbfu ||
            (bytes[2] & 0xc0u) != 0x80u) {
            return false;
        }

        *width = 3u;
        return true;
    }

    if ((first >= 0xe1u &&
         first <= 0xecu) ||
        (first >= 0xeeu &&
         first <= 0xefu)) {

        if (remaining < 3u ||
            (bytes[1] & 0xc0u) != 0x80u ||
            (bytes[2] & 0xc0u) != 0x80u) {
            return false;
        }

        *width = 3u;
        return true;
    }

    if (first == 0xedu) {
        if (remaining < 3u ||
            bytes[1] < 0x80u ||
            bytes[1] > 0x9fu ||
            (bytes[2] & 0xc0u) != 0x80u) {
            return false;
        }

        *width = 3u;
        return true;
    }

    if (first == 0xf0u) {
        if (remaining < 4u ||
            bytes[1] < 0x90u ||
            bytes[1] > 0xbfu ||
            (bytes[2] & 0xc0u) != 0x80u ||
            (bytes[3] & 0xc0u) != 0x80u) {
            return false;
        }

        *width = 4u;
        return true;
    }

    if (first >= 0xf1u &&
        first <= 0xf3u) {

        if (remaining < 4u ||
            (bytes[1] & 0xc0u) != 0x80u ||
            (bytes[2] & 0xc0u) != 0x80u ||
            (bytes[3] & 0xc0u) != 0x80u) {
            return false;
        }

        *width = 4u;
        return true;
    }

    if (first == 0xf4u) {
        if (remaining < 4u ||
            bytes[1] < 0x80u ||
            bytes[1] > 0x8fu ||
            (bytes[2] & 0xc0u) != 0x80u ||
            (bytes[3] & 0xc0u) != 0x80u) {
            return false;
        }

        *width = 4u;
        return true;
    }

    return false;
}

static bool
vitte_help_source_excerpt(
    const vitte_diagnostic_t *diagnostic,
    const vitte_source_context_t *sources,
    char *text,
    size_t capacity
)
{
    const vitte_source_entry_t *source;
    const unsigned char *bytes;
    size_t length;
    size_t offset;

    if (text == NULL ||
        capacity == 0u) {
        return false;
    }

    text[0] = '\0';

    if (diagnostic == NULL ||
        sources == NULL ||
        !diagnostic->has_span ||
        diagnostic->source_id ==
            VITTE_SOURCE_ID_INVALID ||
        diagnostic->end_offset <=
            diagnostic->start_offset) {

        return false;
    }

    source =
        vitte_source_get(
            sources,
            diagnostic->source_id
        );

    if (source == NULL ||
        source->data == NULL ||
        diagnostic->start_offset >
            source->length ||
        diagnostic->end_offset >
            source->length) {

        return false;
    }

    length =
        diagnostic->end_offset -
        diagnostic->start_offset;

    if (length == 0u ||
        length >= capacity) {
        return false;
    }

    bytes =
        (const unsigned char *)
        source->data +
        diagnostic->start_offset;

    offset = 0u;

    while (offset < length) {
        size_t width;
        unsigned char character;

        character = bytes[offset];

        /*
         * Help text is deliberately single-line and terminal-safe.
         */
        if (character < 0x20u ||
            character == 0x7fu ||
            character == '`') {

            return false;
        }

        if (!vitte_help_utf8_sequence_valid(
                bytes + offset,
                length - offset,
                &width
            )) {

            return false;
        }

        offset += width;
    }

    memcpy(
        text,
        source->data +
            diagnostic->start_offset,
        length
    );

    text[length] = '\0';

    return true;
}

/* ========================================================================= */
/* Diagnostic detail extraction                                              */
/* ========================================================================= */

static const char *
vitte_help_primary_label(
    const vitte_diagnostic_t *diagnostic
)
{
    size_t index;

    if (diagnostic == NULL) {
        return NULL;
    }

    for (index = 0u;
         index < diagnostic->label_count;
         index++) {

        const vitte_diagnostic_label_t *label;

        label =
            &diagnostic->labels[index];

        if (label->style ==
                VITTE_DIAGNOSTIC_LABEL_PRIMARY &&
            vitte_help_text_present(
                label->message
            )) {

            return label->message;
        }
    }

    return NULL;
}

static const char *
vitte_help_first_cause(
    const vitte_diagnostic_t *diagnostic
)
{
    size_t index;

    if (diagnostic == NULL) {
        return NULL;
    }

    for (index = 0u;
         index < diagnostic->cause_count;
         index++) {

        if (vitte_help_text_present(
                diagnostic->causes[index].message
            )) {

            return diagnostic->causes[index].message;
        }
    }

    return NULL;
}

static const char *
vitte_help_detail(
    const vitte_diagnostic_t *diagnostic
)
{
    const char *label;
    const char *cause;

    if (diagnostic == NULL) {
        return NULL;
    }

    if (vitte_help_text_present(
            diagnostic->details
        )) {

        return diagnostic->details;
    }

    label =
        vitte_help_primary_label(
            diagnostic
        );

    if (vitte_help_text_present(label)) {
        return label;
    }

    cause =
        vitte_help_first_cause(
            diagnostic
        );

    if (vitte_help_text_present(cause)) {
        return cause;
    }

    return NULL;
}

/* ========================================================================= */
/* Expected-token extraction                                                 */
/* ========================================================================= */

static bool
vitte_help_extract_expected_token(
    const char *reason,
    char *expected,
    size_t expected_capacity,
    char *found,
    size_t found_capacity
)
{
    const char *start;
    const char *separator;
    const char *end;
    size_t length;

    if (expected != NULL &&
        expected_capacity != 0u) {
        expected[0] = '\0';
    }

    if (found != NULL &&
        found_capacity != 0u) {
        found[0] = '\0';
    }

    if (!vitte_help_text_present(reason) ||
        expected == NULL ||
        expected_capacity == 0u) {
        return false;
    }

    start = reason;

    while (*start != '\0' &&
           isspace((unsigned char)*start)) {
        start++;
    }

    if (strncmp(
            start,
            "expected ",
            9u
        ) != 0) {

        return false;
    }

    start += 9u;

    separator =
        strstr(
            start,
            ", found "
        );

    end =
        separator != NULL
        ? separator
        : start + strlen(start);

    while (end > start &&
           isspace(
               (unsigned char)end[-1]
           )) {

        end--;
    }

    length =
        (size_t)(end - start);

    if (length == 0u) {
        return false;
    }

    if (length >= expected_capacity) {
        length =
            expected_capacity - 1u;
    }

    memcpy(
        expected,
        start,
        length
    );

    expected[length] = '\0';

    if (separator != NULL &&
        found != NULL &&
        found_capacity != 0u) {

        const char *found_start;
        const char *found_end;

        found_start =
            separator + 8u;

        while (*found_start != '\0' &&
               isspace(
                   (unsigned char)*found_start
               )) {

            found_start++;
        }

        found_end =
            found_start +
            strlen(found_start);

        while (found_end > found_start &&
               isspace(
                   (unsigned char)found_end[-1]
               )) {

            found_end--;
        }

        length =
            (size_t)(found_end -
                     found_start);

        if (length >= found_capacity) {
            length =
                found_capacity - 1u;
        }

        memcpy(
            found,
            found_start,
            length
        );

        found[length] = '\0';
    }

    return true;
}

/* ========================================================================= */
/* Specialized remediation                                                   */
/* ========================================================================= */

static bool
vitte_help_format_type_mismatch(
    vitte_help_builder_t *builder,
    const vitte_diagnostic_t *diagnostic,
    const char *source_text
)
{
    if (builder == NULL ||
        diagnostic == NULL ||
        !vitte_help_text_present(
            diagnostic->expected_type
        ) ||
        !vitte_help_text_present(
            diagnostic->actual_type
        )) {

        return false;
    }

    vitte_help_builder_append(
        builder,
        "Provide a value of type "
    );

    vitte_help_builder_append_quoted_fragment(
        builder,
        diagnostic->expected_type,
        VITTE_HELP_TYPE_LIMIT
    );

    vitte_help_builder_append(
        builder,
        " instead of "
    );

    vitte_help_builder_append_quoted_fragment(
        builder,
        diagnostic->actual_type,
        VITTE_HELP_TYPE_LIMIT
    );

    if (vitte_help_text_present(
            source_text
        )) {

        vitte_help_builder_append(
            builder,
            " for "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            source_text,
            VITTE_HELP_SUBJECT_LIMIT
        );
    }

    vitte_help_builder_append(
        builder,
        ". "
    );

    if (vitte_help_string_type(
            diagnostic->actual_type
        ) &&
        vitte_help_numeric_type(
            diagnostic->expected_type
        )) {

        vitte_help_builder_append(
            builder,
            "If the text represents user or file input, parse it as a numeric value and handle invalid input before using the result."
        );

        return true;
    }

    if (vitte_help_numeric_type(
            diagnostic->actual_type
        ) &&
        vitte_help_string_type(
            diagnostic->expected_type
        )) {

        vitte_help_builder_append(
            builder,
            "Format the numeric value explicitly when textual representation is intended instead of relying on an unrelated cast."
        );

        return true;
    }

    if (vitte_help_integer_type(
            diagnostic->actual_type
        ) &&
        vitte_help_integer_type(
            diagnostic->expected_type
        )) {

        vitte_help_builder_append(
            builder,
            "Check the value range and signedness before applying an explicit integer conversion."
        );

        return true;
    }

    if (vitte_help_float_type(
            diagnostic->actual_type
        ) &&
        vitte_help_integer_type(
            diagnostic->expected_type
        )) {

        vitte_help_builder_append(
            builder,
            "An integer conversion can discard a fractional component or exceed the destination range; make that behavior explicit only when it is intended."
        );

        return true;
    }

    if (vitte_help_integer_type(
            diagnostic->actual_type
        ) &&
        vitte_help_float_type(
            diagnostic->expected_type
        )) {

        vitte_help_builder_append(
            builder,
            "Use an explicit numeric conversion when the destination representation is intentional and its precision is acceptable."
        );

        return true;
    }

    if (vitte_help_boolean_type(
            diagnostic->expected_type
        )) {

        vitte_help_builder_append(
            builder,
            "Produce a boolean expression explicitly, normally with a comparison or another operation whose result type is `bool`."
        );

        return true;
    }

    vitte_help_builder_append(
        builder,
        "Check the declaration establishing the required type and convert explicitly only when the conversion preserves the intended semantics."
    );

    return true;
}

static bool
vitte_help_format_parser_expectation(
    vitte_help_builder_t *builder,
    const char *reason
)
{
    char expected[
        VITTE_HELP_EXPECTED_TOKEN_LIMIT
    ];

    char found[
        VITTE_HELP_EXPECTED_TOKEN_LIMIT
    ];

    if (builder == NULL ||
        !vitte_help_extract_expected_token(
            reason,
            expected,
            sizeof(expected),
            found,
            sizeof(found)
        )) {

        return false;
    }

    vitte_help_builder_append(
        builder,
        "Insert or restore "
    );

    vitte_help_builder_append_quoted_fragment(
        builder,
        expected,
        VITTE_HELP_EXPECTED_TOKEN_LIMIT
    );

    if (vitte_help_text_present(found)) {
        vitte_help_builder_append(
            builder,
            " before "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            found,
            VITTE_HELP_EXPECTED_TOKEN_LIMIT
        );
    }

    vitte_help_builder_append(
        builder,
        ". Match the expected element to the surrounding grammar and the nearest corresponding opening construct."
    );

    return true;
}

static bool
vitte_help_format_unresolved_name(
    vitte_help_builder_t *builder,
    const char *subject
)
{
    if (builder == NULL ||
        !vitte_help_text_present(subject)) {
        return false;
    }

    vitte_help_builder_append(
        builder,
        "Declare or import "
    );

    vitte_help_builder_append_quoted_fragment(
        builder,
        subject,
        VITTE_HELP_SUBJECT_LIMIT
    );

    vitte_help_builder_append(
        builder,
        " in this scope, or correct its spelling to match a visible declaration."
    );

    return true;
}

static bool
vitte_help_format_unknown_type(
    vitte_help_builder_t *builder,
    const char *subject
)
{
    if (builder == NULL ||
        !vitte_help_text_present(subject)) {
        return false;
    }

    vitte_help_builder_append(
        builder,
        "Declare or import the type "
    );

    vitte_help_builder_append_quoted_fragment(
        builder,
        subject,
        VITTE_HELP_SUBJECT_LIMIT
    );

    vitte_help_builder_append(
        builder,
        " before using it here; verify the type's spelling, module and visibility."
    );

    return true;
}

static bool
vitte_help_format_boolean_condition(
    vitte_help_builder_t *builder,
    const vitte_diagnostic_t *diagnostic
)
{
    if (builder == NULL ||
        diagnostic == NULL ||
        !vitte_help_text_present(
            diagnostic->actual_type
        )) {

        return false;
    }

    vitte_help_builder_append(
        builder,
        "Convert the condition into an explicit boolean expression instead of using a value of type "
    );

    vitte_help_builder_append_quoted_fragment(
        builder,
        diagnostic->actual_type,
        VITTE_HELP_TYPE_LIMIT
    );

    vitte_help_builder_append(
        builder,
        ". Choose a comparison or predicate that expresses the intended condition."
    );

    return true;
}

static bool
vitte_help_format_contract(
    vitte_help_builder_t *builder,
    const vitte_diagnostic_t *diagnostic,
    const char *policy
)
{
    if (builder == NULL ||
        diagnostic == NULL ||
        !diagnostic->has_contract ||
        !vitte_help_text_present(
            diagnostic->contract.expression
        )) {

        return false;
    }

    if (vitte_help_text_present(policy)) {
        vitte_help_builder_append(
            builder,
            policy
        );

        vitte_help_builder_append(
            builder,
            " "
        );
    }

    vitte_help_builder_append(
        builder,
        "Contract condition: "
    );

    vitte_help_builder_append_quoted_fragment(
        builder,
        diagnostic->contract.expression,
        VITTE_HELP_CONTRACT_LIMIT
    );

    vitte_help_builder_append(
        builder,
        "."
    );

    if (vitte_help_text_present(
            diagnostic->contract.reason
        )) {

        vitte_help_builder_append(
            builder,
            " Reported reason: "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            diagnostic->contract.reason,
            VITTE_HELP_REASON_LIMIT
        );

        vitte_help_builder_append(
            builder,
            "."
        );
    }

    return true;
}

/* ========================================================================= */
/* Context enrichment                                                        */
/* ========================================================================= */

static void
vitte_help_append_semantic_context(
    vitte_help_builder_t *builder,
    const vitte_diagnostic_t *diagnostic,
    bool include_types
)
{
    if (builder == NULL ||
        diagnostic == NULL) {
        return;
    }

    if (vitte_help_text_present(
            diagnostic->symbol
        )) {

        vitte_help_builder_append(
            builder,
            " Concerned symbol: "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            diagnostic->symbol,
            VITTE_HELP_SUBJECT_LIMIT
        );

        vitte_help_builder_append(
            builder,
            "."
        );
    }

    if (include_types) {
        if (vitte_help_text_present(
                diagnostic->expected_type
            )) {

            vitte_help_builder_append(
                builder,
                " Required type: "
            );

            vitte_help_builder_append_quoted_fragment(
                builder,
                diagnostic->expected_type,
                VITTE_HELP_TYPE_LIMIT
            );

            vitte_help_builder_append(
                builder,
                "."
            );
        }

        if (vitte_help_text_present(
                diagnostic->actual_type
            )) {

            vitte_help_builder_append(
                builder,
                " Actual type: "
            );

            vitte_help_builder_append_quoted_fragment(
                builder,
                diagnostic->actual_type,
                VITTE_HELP_TYPE_LIMIT
            );

            vitte_help_builder_append(
                builder,
                "."
            );
        }
    }
}

static void
vitte_help_append_compilation_context(
    vitte_help_builder_t *builder,
    const vitte_diagnostic_t *diagnostic
)
{
    if (builder == NULL ||
        diagnostic == NULL) {
        return;
    }

    if (vitte_help_text_present(
            diagnostic->procedure
        )) {

        vitte_help_builder_append(
            builder,
            " In procedure "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            diagnostic->procedure,
            VITTE_DIAGNOSTIC_PROCEDURE_CAPACITY
        );

        vitte_help_builder_append(
            builder,
            "."
        );

        return;
    }

    if (vitte_help_text_present(
            diagnostic->module
        )) {

        vitte_help_builder_append(
            builder,
            " In module "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            diagnostic->module,
            VITTE_DIAGNOSTIC_MODULE_CAPACITY
        );

        vitte_help_builder_append(
            builder,
            "."
        );
    }
}

static void
vitte_help_append_reference(
    vitte_help_builder_t *builder,
    const char *public_code
)
{
    if (builder == NULL ||
        !vitte_help_text_present(
            public_code
        )) {

        return;
    }

    vitte_help_builder_append(
        builder,
        " See `vitte explain "
    );

    vitte_help_builder_append(
        builder,
        public_code
    );

    vitte_help_builder_append(
        builder,
        "`."
    );
}

/* ========================================================================= */
/* Fallback help                                                             */
/* ========================================================================= */

static void
vitte_help_format_fallback(
    vitte_help_builder_t *builder,
    const vitte_diagnostic_t *diagnostic,
    const char *reason
)
{
    const char *origin_name;
    const char *identifier;

    if (builder == NULL ||
        diagnostic == NULL) {
        return;
    }

    identifier =
        vitte_help_text_present(
            diagnostic->short_code
        )
        ? diagnostic->short_code
        : diagnostic->code;

    origin_name =
        vitte_diagnostic_origin_name(
            diagnostic->origin
        );

    vitte_help_builder_append(
        builder,
        "No verified correction is registered for "
    );

    if (vitte_help_text_present(identifier)) {
        vitte_help_builder_append_quoted_fragment(
            builder,
            identifier,
            VITTE_DIAGNOSTIC_CODE_CAPACITY
        );
    } else {
        vitte_help_builder_append(
            builder,
            "this unclassified diagnostic"
        );
    }

    if (vitte_help_text_present(origin_name)) {
        vitte_help_builder_append(
            builder,
            " from compiler phase "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            origin_name,
            64u
        );
    }

    vitte_help_builder_append(
        builder,
        "."
    );

    if (vitte_help_text_present(
            diagnostic->message
        )) {

        vitte_help_builder_append(
            builder,
            " Failure: "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            diagnostic->message,
            VITTE_HELP_REASON_LIMIT
        );

        vitte_help_builder_append(
            builder,
            "."
        );
    }

    if (vitte_help_text_present(reason)) {
        vitte_help_builder_append(
            builder,
            " Detail: "
        );

        vitte_help_builder_append_quoted_fragment(
            builder,
            reason,
            VITTE_HELP_REASON_LIMIT
        );

        vitte_help_builder_append(
            builder,
            "."
        );
    }

    if (vitte_help_text_present(
            vitte_help_first_cause(
                diagnostic
            )
        )) {

        const char *cause;

        cause =
            vitte_help_first_cause(
                diagnostic
            );

        if (cause != reason) {
            vitte_help_builder_append(
                builder,
                " Investigate the reported cause: "
            );

            vitte_help_builder_append_quoted_fragment(
                builder,
                cause,
                VITTE_HELP_REASON_LIMIT
            );

            vitte_help_builder_append(
                builder,
                "."
            );
        }
    }

    if (diagnostic->origin ==
            VITTE_DIAGNOSTIC_ORIGIN_INTERNAL ||
        diagnostic->severity ==
            VITTE_DIAGNOSTIC_FATAL) {

        vitte_help_builder_append(
            builder,
            " Preserve the failing command, compiler version and smallest reproducing input for a compiler report."
        );
    }
}

/* ========================================================================= */
/* Contextual help formatter                                                 */
/* ========================================================================= */

bool
vitte_diagnostic_format_help(
    const vitte_diagnostic_t *diagnostic,
    const vitte_source_context_t *sources,
    char *buffer,
    size_t capacity
)
{
    vitte_help_builder_t builder;

    const vitte_diagnostic_registry_entry_t *entry;

    const char *explicit_help;
    const char *public_code;
    const char *policy;
    const char *reason;
    const char *subject;
    char parser_reason[
        (VITTE_HELP_EXPECTED_TOKEN_LIMIT * 2u) + 32u
    ];

    char source_text[
        VITTE_HELP_EXCERPT_CAPACITY
    ];

    bool specialized;
    bool include_types;

    if (buffer == NULL ||
        capacity == 0u) {
        return false;
    }

    vitte_help_builder_init(
        &builder,
        buffer,
        capacity
    );

    if (diagnostic == NULL) {
        return false;
    }

    /*
     * Explicit source-specific help always wins.
     *
     * A producer that knows the exact remediation has more information than
     * the generic code policy.
     */
    explicit_help =
        vitte_help_explicit_annotation(
            diagnostic
        );

    if (vitte_help_text_present(
            explicit_help
        )) {

        vitte_help_builder_append(
            &builder,
            explicit_help
        );

        return !builder.failed &&
               builder.length != 0u;
    }

    /*
     * NOTE and HELP diagnostics should not recursively generate generic help.
     *
     * Warnings, errors and fatals may.
     */
    if (diagnostic->severity !=
            VITTE_DIAGNOSTIC_WARNING &&
        diagnostic->severity !=
            VITTE_DIAGNOSTIC_ERROR &&
        diagnostic->severity !=
            VITTE_DIAGNOSTIC_FATAL) {

        return false;
    }

    entry =
        vitte_help_registry_entry(
            diagnostic
        );

    public_code =
        vitte_help_public_code(
            diagnostic,
            entry
        );

    policy =
        vitte_diagnostic_help_for_code(
            public_code
        );

    reason =
        vitte_help_detail(
            diagnostic
        );

    /*
     * Native frontend diagnostics carry expected/found as structured fields
     * and may not include the bootstrap parser's prose detail. Reconstruct
     * the stable parser phrase so contextual help remains equally specific.
     */
    if (public_code != NULL &&
        (strcmp(public_code, "E0201") == 0 ||
         strcmp(public_code, "E0207") == 0 ||
         strcmp(public_code, "E0208") == 0) &&
        vitte_help_text_present(diagnostic->expected)) {
        if (vitte_help_text_present(diagnostic->found)) {
            (void)snprintf(
                parser_reason,
                sizeof(parser_reason),
                "expected %s, found %s",
                diagnostic->expected,
                diagnostic->found
            );
        } else {
            (void)snprintf(
                parser_reason,
                sizeof(parser_reason),
                "expected %s",
                diagnostic->expected
            );
        }
        reason = parser_reason;
    }

    source_text[0] = '\0';

    (void)vitte_help_source_excerpt(
        diagnostic,
        sources,
        source_text,
        sizeof(source_text)
    );

    subject =
        vitte_help_text_present(
            diagnostic->symbol
        )
        ? diagnostic->symbol
        : (
            vitte_help_text_present(
                source_text
            )
            ? source_text
            : NULL
        );

    specialized = false;
    include_types = true;

    /* --------------------------------------------------------------------- */
    /* Type mismatch / argument / return diagnostics                         */
    /* --------------------------------------------------------------------- */

    if (public_code != NULL &&
        (
            strcmp(public_code, "E0501") == 0 ||
            strcmp(public_code, "E0602") == 0 ||
            strcmp(public_code, "E0605") == 0
        )) {

        specialized =
            vitte_help_format_type_mismatch(
                &builder,
                diagnostic,
                source_text
            );

        if (specialized) {
            include_types = false;
        }
    }

    /* --------------------------------------------------------------------- */
    /* Parser expectation diagnostics                                        */
    /* --------------------------------------------------------------------- */

    if (!specialized &&
        public_code != NULL &&
        (
            strcmp(public_code, "E0201") == 0 ||
            strcmp(public_code, "E0207") == 0 ||
            strcmp(public_code, "E0208") == 0
        )) {

        specialized =
            vitte_help_format_parser_expectation(
                &builder,
                reason
            );
    }

    /* --------------------------------------------------------------------- */
    /* Name resolution                                                       */
    /* --------------------------------------------------------------------- */

    if (!specialized &&
        public_code != NULL &&
        strcmp(public_code, "E0401") == 0) {

        specialized =
            vitte_help_format_unresolved_name(
                &builder,
                subject
            );
    }

    /* --------------------------------------------------------------------- */
    /* Unknown type                                                          */
    /* --------------------------------------------------------------------- */

    if (!specialized &&
        public_code != NULL &&
        strcmp(public_code, "E0510") == 0) {

        specialized =
            vitte_help_format_unknown_type(
                &builder,
                subject
            );
    }

    /* --------------------------------------------------------------------- */
    /* Boolean condition                                                     */
    /* --------------------------------------------------------------------- */

    if (!specialized &&
        public_code != NULL &&
        strcmp(public_code, "E0515") == 0) {

        specialized =
            vitte_help_format_boolean_condition(
                &builder,
                diagnostic
            );

        if (specialized) {
            include_types = false;
        }
    }

    /* --------------------------------------------------------------------- */
    /* Contract diagnostics                                                  */
    /* --------------------------------------------------------------------- */

    if (!specialized &&
        diagnostic->has_contract) {

        specialized =
            vitte_help_format_contract(
                &builder,
                diagnostic,
                policy
            );
    }

    /* --------------------------------------------------------------------- */
    /* Generic registered remediation                                        */
    /* --------------------------------------------------------------------- */

    if (!specialized &&
        vitte_help_text_present(
            policy
        )) {

        vitte_help_builder_append(
            &builder,
            policy
        );

        specialized = true;
    }

    /* --------------------------------------------------------------------- */
    /* Unknown diagnostic fallback                                           */
    /* --------------------------------------------------------------------- */

    if (!specialized) {
        vitte_help_format_fallback(
            &builder,
            diagnostic,
            reason
        );
    } else {
        /*
         * Structured information augments generic help only when it has not
         * already been incorporated by a specialized formatter.
         */
        if (public_code != NULL &&
            (
                strcmp(public_code, "E0005") == 0 ||
                strcmp(public_code, "E0011") == 0 ||
                strcmp(public_code, "E0013") == 0
            ) &&
            vitte_help_text_present(reason)) {

            vitte_help_builder_append(
                &builder,
                " Reported failure: "
            );

            vitte_help_builder_append_quoted_fragment(
                &builder,
                reason,
                VITTE_HELP_REASON_LIMIT
            );

            vitte_help_builder_append(
                &builder,
                "."
            );
        }

        /*
         * Do not duplicate symbol information when the specialized unresolved
         * name/type formatter already used it directly.
         */
        if (!(public_code != NULL &&
              (
                  strcmp(public_code, "E0401") == 0 ||
                  strcmp(public_code, "E0510") == 0
              ))) {

            vitte_help_append_semantic_context(
                &builder,
                diagnostic,
                include_types
            );
        }
    }

    vitte_help_append_compilation_context(
        &builder,
        diagnostic
    );

    vitte_help_append_reference(
        &builder,
        public_code
    );

    return !builder.failed &&
           builder.length != 0u;
}

/* ========================================================================= */
/* Rule-table introspection                                                   */
/* ========================================================================= */

size_t
vitte_diagnostic_help_rule_count(void)
{
    return VITTE_HELP_RULE_COUNT;
}

const char *
vitte_diagnostic_help_rule_code(
    size_t index
)
{
    if (index >=
        VITTE_HELP_RULE_COUNT) {
        return NULL;
    }

    return vitte_help_rules[index].code;
}

const char *
vitte_diagnostic_help_rule_text(
    size_t index
)
{
    if (index >=
        VITTE_HELP_RULE_COUNT) {
        return NULL;
    }

    return vitte_help_rules[index].help;
}

/* ========================================================================= */
/* Coverage / validation                                                     */
/* ========================================================================= */

bool
vitte_diagnostic_help_validate_rules(
    size_t *missing_count,
    size_t *orphan_count,
    size_t *duplicate_count
)
{
    size_t missing;
    size_t orphan;
    size_t duplicate;

    size_t registry_index;
    size_t rule_index;

    size_t registry_count;

    missing = 0u;
    orphan = 0u;
    duplicate = 0u;

    registry_count =
        vitte_diagnostic_registry_count();

    /*
     * Every registered warning/error/fatal diagnostic must have a help rule.
     *
     * NOTE/HELP registry entries are informational by nature and are not
     * required to recursively produce remediation guidance.
     */
    for (registry_index = 0u;
         registry_index < registry_count;
         registry_index++) {

        const vitte_diagnostic_registry_entry_t *entry;
        bool found;

        entry =
            vitte_diagnostic_registry_at(
                registry_index
            );

        if (entry == NULL ||
            !vitte_help_text_present(
                entry->public_code
            )) {

            continue;
        }

        if (entry->default_severity ==
                VITTE_DIAGNOSTIC_NOTE ||
            entry->default_severity ==
                VITTE_DIAGNOSTIC_HELP) {

            continue;
        }

        found = false;

        for (rule_index = 0u;
             rule_index < VITTE_HELP_RULE_COUNT;
             rule_index++) {

            if (strcmp(
                    entry->public_code,
                    vitte_help_rules[
                        rule_index
                    ].code
                ) == 0) {

                found = true;
                break;
            }
        }

        if (!found) {
            missing++;
        }
    }

    /*
     * Every rule must refer to an existing registry diagnostic.
     */
    for (rule_index = 0u;
         rule_index < VITTE_HELP_RULE_COUNT;
         rule_index++) {

        if (vitte_diagnostic_registry_find_public(
                vitte_help_rules[
                    rule_index
                ].code
            ) == NULL) {

            orphan++;
        }
    }

    /*
     * Duplicate help policies would make behavior depend on table order.
     */
    for (rule_index = 0u;
         rule_index < VITTE_HELP_RULE_COUNT;
         rule_index++) {

        size_t other;

        for (other = rule_index + 1u;
             other < VITTE_HELP_RULE_COUNT;
             other++) {

            if (strcmp(
                    vitte_help_rules[
                        rule_index
                    ].code,
                    vitte_help_rules[
                        other
                    ].code
                ) == 0) {

                duplicate++;
            }
        }
    }

    if (missing_count != NULL) {
        *missing_count = missing;
    }

    if (orphan_count != NULL) {
        *orphan_count = orphan;
    }

    if (duplicate_count != NULL) {
        *duplicate_count = duplicate;
    }

    return missing == 0u &&
           orphan == 0u &&
           duplicate == 0u;
}
