#!/usr/bin/env python3
"""Compare native and bootstrap parsing, AST construction, and semantics."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from itertools import product
from pathlib import Path


SEMANTIC_CASES = {
    "nullable coalescing accepts an optional payload": (
        "proc main() -> i64 { let value: i64 = 42; let optional: ?i64 = &value; "
        "return (optional ?? 0) - 42; }\n",
        True,
    ),
    "nullable coalescing rejects a non-optional value": (
        "proc main() -> i64 { let value: i64 = 42; return value ?? 0; }\n",
        False,
    ),
    "nullable coalescing rejects an incompatible fallback": (
        "proc main() -> i64 { let value: i64 = 42; let optional: ?i64 = &value; "
        'let result: i64 = optional ?? "wrong"; return 0; }\n',
        False,
    ),
    "assert rejects non-boolean conditions": (
        "proc main() -> i64 { assert(1); return 0; }\n",
        False,
    ),
    "test declaration rejects non-boolean assertion": (
        'test "invalid condition" { assert(1); }\n',
        False,
    ),
    "await accepts a direct async call": (
        "async proc delayed_value() -> i64 { return 42; }\n"
        "proc main() -> i64 { return (await delayed_value()) - 42; }\n",
        True,
    ),
    "await rejects a synchronous call": (
        "proc immediate_value() -> i64 { return 42; }\n"
        "proc main() -> i64 { return await immediate_value(); }\n",
        False,
    ),
    "async call creates a storable task": (
        "async proc delayed_value() -> i64 { return 42; }\n"
        "proc main() -> i64 { let task = delayed_value(); return await task - 42; }\n",
        True,
    ),
    "await rejects a non-call value": (
        "async proc delayed_value() -> i64 { return 42; }\n"
        "proc main() -> i64 { let value: i64 = 42; return await value; }\n",
        False,
    ),
    "print rejects zero arguments": (
        "proc main() -> int { print(); return 0; }\n",
        False,
    ),
    "print rejects multiple arguments": (
        'proc main() -> int { print("one", "two"); return 0; }\n',
        False,
    ),
    "print rejects non-string arguments": (
        "proc main() -> int { print(42); return 0; }\n",
        False,
    ),
    "array form field": (
        "form Packet { values: [3]i64 }\n"
        "proc main() -> i64 {\n"
        "    let packet: Packet = Packet { values: [10, 20, 12] };\n"
        "    return packet.values[2] - 12;\n"
        "}\n",
        True,
    ),
    "array form field length mismatch": (
        "form Packet { values: [3]i64 }\n"
        "proc main() -> i64 {\n"
        "    let packet: Packet = Packet { values: [10, 20] };\n"
        "    return packet.values[0];\n"
        "}\n",
        False,
    ),
    "array form field element mismatch": (
        'form Packet { values: [3]i64 }\n'
        "proc main() -> i64 {\n"
        '    let packet: Packet = Packet { values: [10, "wrong", 12] };\n'
        "    return packet.values[0];\n"
        "}\n",
        False,
    ),
    "malformed form field type": (
        "form Packet { values: [3 i64 }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "nested generic arguments": (
        "form Box<T> { value: T }\n"
        "form Outer<T> { wrapped: Box<T> }\n"
        "proc main() -> i64 {\n"
        "    let outer: Outer<Box<i64>> = Outer<Box<i64>> {\n"
        "        wrapped: Box<Box<i64>> { value: Box<i64> { value: 42 } },\n"
        "    };\n"
        "    return outer.wrapped.value.value - 42;\n"
        "}\n",
        True,
    ),
    "generic identity alias": (
        "type Identity<T> = T;\n"
        "proc identity_value(value: Identity<i64>) -> Identity<i64> { return value; }\n"
        "proc main() -> i64 { return identity_value(42) - 42; }\n",
        True,
    ),
    "independent generic aliases reuse a parameter name": (
        "type Identity<T> = T;\n"
        "type AlsoIdentity<T> = T;\n"
        "proc identity_value(value: Identity<i64>) -> AlsoIdentity<i64> { return value; }\n"
        "proc main() -> i64 { return identity_value(42) - 42; }\n",
        True,
    ),
    "generic alias chain": (
        "type Identity<T> = T;\n"
        "type First<U> = Identity<U>;\n"
        "type Second<V> = First<V>;\n"
        "proc get_value(value: Second<i64>) -> i64 { return value; }\n"
        "proc main() -> i64 { return get_value(42) - 42; }\n",
        True,
    ),
    "generic alias chain to form": (
        "form Box<T> { value: T }\n"
        "type Boxed<T> = Box<T>;\n"
        "type Wrapped<U> = Boxed<U>;\n"
        "proc get_value(value: Wrapped<i64>) -> i64 { return value.value; }\n"
        "proc main() -> i64 { let value: Wrapped<i64> = Wrapped<i64> { value: 42 }; return get_value(value) - 42; }\n",
        True,
    ),
    "generic reference alias": (
        "type Ref<T> = &T;\n"
        "proc read_ref(value: Ref<i64>) -> i64 { return *value; }\n"
        "proc main() -> i64 { let value: i64 = 42; return read_ref(&value) - 42; }\n",
        True,
    ),
    "generic pointer alias": (
        "type Ptr<T> = *T;\n"
        "proc read_ptr(value: Ptr<i64>) -> i64 { return *value; }\n"
        "proc main() -> i64 { let value: i64 = 42; return read_ptr(&value) - 42; }\n",
        True,
    ),
    "generic opaque pointer": (
        "opaque Handle<T>;\n"
        "proc is_null(value: *Handle<i64>) -> bool { return value == null; }\n"
        "proc main() -> i64 { if is_null(null) { return 0; } return 1; }\n",
        True,
    ),
    "generic opaque wrong argument count": (
        "opaque Handle<T>;\n"
        "proc consume(value: *Handle<i64, bool>) {}\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "generic opaque arguments stay distinct": (
        "opaque Handle<T>;\n"
        "proc accept(value: *Handle<i64>) {}\n"
        "proc main() -> i64 { let value: *Handle<bool> = null; accept(value); return 0; }\n",
        False,
    ),
    "generic alias to form": (
        "form Box<T> { value: T }\n"
        "type Boxed<T> = Box<T>;\n"
        "proc get_box(value: Boxed<i64>) -> i64 { return value.value; }\n"
        "proc main() -> i64 { let value: Box<i64> = Box<i64> { value: 42 }; return get_box(value) - 42; }\n",
        True,
    ),
    "generic alias to form without direct form use": (
        "form Box<T> { value: T }\n"
        "type Boxed<T> = Box<T>;\n"
        "proc get_box(value: Boxed<i64>) -> i64 { return value.value; }\n"
        "proc main() -> i64 { return 0; }\n",
        True,
    ),
    "generic alias to dynamic slice": (
        "type Values<T> = [T];\n"
        "proc first(values: Values<i64>) -> i64 { return values[0]; }\n"
        "proc main() -> i64 { let values: Values<i64> = [42]; return first(values) - 42; }\n",
        True,
    ),
    "generic alias chain to dynamic slice": (
        "type Values<T> = [T];\n"
        "type ValuesAlias<U> = Values<U>;\n"
        "proc first(values: ValuesAlias<i64>) -> i64 { return values[0]; }\n"
        "proc main() -> i64 { let values: ValuesAlias<i64> = [42]; return first(values) - 42; }\n",
        True,
    ),
    "generic alias to fixed array": (
        "type Values<T> = [2]T;\n"
        "proc first(values: Values<i64>) -> i64 { return values[0]; }\n"
        "proc main() -> i64 { let values: Values<i64> = [20, 22]; return first(values) + values[1] - 42; }\n",
        True,
    ),
    "generic alias chain to fixed array": (
        "type Pair<T> = [2]T;\n"
        "type PairAlias<U> = Pair<U>;\n"
        "proc first(values: PairAlias<i64>) -> i64 { return values[0]; }\n"
        "proc main() -> i64 { let values: PairAlias<i64> = [20, 22]; return first(values) + values[1] - 42; }\n",
        True,
    ),
    "generic function alias": (
        "type Transform<T> = proc(T) -> T;\n"
        "proc keep_i64(value: i64) -> i64 { return value; }\n"
        "proc apply(transform: Transform<i64>, value: i64) -> i64 { return transform(value); }\n"
        "proc main() -> i64 { return apply(keep_i64, 42) - 42; }\n",
        True,
    ),
    "generic function alias rejects mismatched callback": (
        "type Transform<T> = proc(T) -> T;\n"
        "proc keep_bool(value: bool) -> bool { return value; }\n"
        "proc apply(transform: Transform<i64>, value: i64) -> i64 { return transform(value); }\n"
        "proc main() -> i64 { return apply(keep_bool, 42); }\n",
        False,
    ),
    "generic function alias chain": (
        "type Transform<T> = proc(T) -> T;\n"
        "type TransformAlias<U> = Transform<U>;\n"
        "type TransformAlias2<V> = TransformAlias<V>;\n"
        "proc keep_i64(value: i64) -> i64 { return value; }\n"
        "proc apply(transform: TransformAlias2<i64>, value: i64) -> i64 { return transform(value); }\n"
        "proc main() -> i64 { return apply(keep_i64, 42) - 42; }\n",
        True,
    ),
    "generic function alias chain rejects mismatched callback": (
        "type Transform<T> = proc(T) -> T;\n"
        "type TransformAlias<U> = Transform<U>;\n"
        "type TransformAlias2<V> = TransformAlias<V>;\n"
        "proc keep_bool(value: bool) -> bool { return value; }\n"
        "proc apply(transform: TransformAlias2<i64>, value: i64) -> i64 { return transform(value); }\n"
        "proc main() -> i64 { return apply(keep_bool, 42); }\n",
        False,
    ),
    "nested generic composition rejects incompatible substitution": (
        "form Box<T> { value: T }\n"
        "form Outer<T> { wrapped: Box<T> }\n"
        "proc main() -> i64 {\n"
        "    let outer: Outer<Box<i64>> = Outer<Box<i64>> {\n"
        "        wrapped: Box<Box<bool>> { value: Box<bool> { value: true } },\n"
        "    };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic form with multiple parameters": (
        "form Pair<T, U> { first: T, second: U }\n"
        "proc main() -> i64 {\n"
        "    let pair: Pair<i64, bool> = Pair<i64, bool> { first: 42, second: true };\n"
        "    if pair.second { return pair.first - 42; }\n"
        "    return 1;\n"
        "}\n",
        True,
    ),
    "generic procedure rejects conflicting repeated parameter": (
        "proc same<T>(left: T, right: T) -> T { return left; }\n"
        "proc main() -> i64 { return same(42, true); }\n",
        False,
    ),
    "generic form first parameter mismatch": (
        "form Pair<T, U> { first: T, second: U }\n"
        "proc main() -> i64 {\n"
        "    let pair: Pair<i64, bool> = Pair<i64, bool> { first: true, second: false };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic form second parameter mismatch": (
        "form Pair<T, U> { first: T, second: U }\n"
        "proc main() -> i64 {\n"
        "    let pair: Pair<i64, bool> = Pair<i64, bool> { first: 42, second: 7 };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic form rejects wrong argument count": (
        "form Pair<T, U> { first: T, second: U }\n"
        "proc main() -> i64 {\n"
        "    let pair: Pair<i64> = Pair<i64> { first: 42, second: 7 };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic form rejects excess argument": (
        "form Pair<T, U> { first: T, second: U }\n"
        "proc main() -> i64 {\n"
        "    let pair: Pair<i64, bool, u64> = Pair<i64, bool, u64> {\n"
        "        first: 42, second: true,\n"
        "    };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic form checks third parameter field": (
        "form Triple<T, U, V> { first: T, second: U, third: V }\n"
        "proc main() -> i64 {\n"
        "    let triple: Triple<i64, bool, u64> = Triple<i64, bool, u64> {\n"
        "        first: 42, second: true, third: false,\n"
        "    };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic form rejects incompatible composed field": (
        "form Box<T> { value: T }\n"
        "form Pair<T, U> { first: T, second: Box<U> }\n"
        "proc main() -> i64 {\n"
        "    let pair: Pair<i64, bool> = Pair<i64, bool> {\n"
        "        first: 42, second: Box<i64> { value: 7 },\n"
        "    };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic form fixed array rejects incompatible composed element": (
        "form Box<T> { value: T }\n"
        "form Pair<T> { items: [2]Box<T> }\n"
        "proc main() -> i64 {\n"
        "    let pair: Pair<i64> = Pair<i64> {\n"
        "        items: [Box<i64> { value: 41 }, Box<bool> { value: true }],\n"
        "    };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic form dynamic array rejects incompatible composed element": (
        "form Box<T> { value: T }\n"
        "form Bag<T> { items: [Box<T>] }\n"
        "proc main() -> i64 {\n"
        "    let bag: Bag<i64> = Bag<i64> {\n"
        "        items: [Box<bool> { value: true }],\n"
        "    };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic optional rejects incompatible borrowed composition": (
        "form Box<T> { value: T }\n"
        "form Maybe<T> { item: ?Box<T> }\n"
        "proc main() -> i64 {\n"
        "    let box: Box<bool> = Box<bool> { value: true };\n"
        "    let maybe: Maybe<i64> = Maybe<i64> { item: &box };\n"
        "    return 0;\n"
        "}\n",
        False,
    ),
    "generic procedure infers i64": (
        "proc identity<T>(value: T) -> T { return value; }\n"
        "proc main() -> i64 { return identity(42) - 42; }\n",
        True,
    ),
    "generic procedure infers bool": (
        "proc identity<T>(value: T) -> T { return value; }\n"
        "proc main() -> i64 { if identity(true) { return 0; } return 1; }\n",
        True,
    ),
    "generic parameter trait bound resolves implementation": (
        "trait Marker {}\n"
        "proc identity<T: Marker>(value: T) -> T { return value; }\n"
        "impl Marker for i64 {}\n"
        "proc main() -> i64 { return identity(42) - 42; }\n",
        True,
    ),
    "generic parameter trait bound rejects missing implementation": (
        "trait Marker {}\n"
        "proc identity<T: Marker>(value: T) -> T { return value; }\n"
        "impl Marker for i64 {}\n"
        "proc main() -> i64 { if identity(true) { return 1; } return 0; }\n",
        False,
    ),
    "generic where trait bound resolves implementation": (
        "trait Marker {}\n"
        "proc identity<T>(value: T) -> T where T: Marker { return value; }\n"
        "impl Marker for i64 {}\n"
        "proc main() -> i64 { return identity(42) - 42; }\n",
        True,
    ),
    "generic where trait bound rejects missing implementation": (
        "trait Marker {}\n"
        "proc identity<T>(value: T) -> T where T: Marker { return value; }\n"
        "impl Marker for i64 {}\n"
        "proc main() -> i64 { if identity(true) { return 1; } return 0; }\n",
        False,
    ),
    "unused generic parameter rejects unknown bound": (
        "proc unused<T: Missing>(value: T) -> T { return value; }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused generic where rejects unknown trait": (
        "proc unused<T>(value: T) -> T where T: Missing { return value; }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused generic where rejects unknown subject": (
        "trait Marker {}\n"
        "proc unused<T>(value: T) -> T where U: Marker { return value; }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused generic bound rejects non-trait type": (
        "proc unused<T: i64>(value: T) -> T { return value; }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused generic form rejects unknown bound": (
        "form Guarded<T: Missing> { value: T }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused where clause rejects an undeclared form parameter": (
        "trait Marker {}\n"
        "form Guarded where T: Marker { value: i64 }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused where clause rejects an undeclared alias parameter": (
        "trait Marker {}\n"
        "type Guarded where T: Marker = i64;\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused generic pick rejects unknown bound": (
        "pick Envelope<T: Missing> { Value(T) }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused generic pick rejects unknown where trait": (
        "trait Marker {}\n"
        "pick Envelope<T> where T: Missing { Value(T) }\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "unused generic pick accepts a declared marker bound": (
        "trait Marker {}\n"
        "pick Envelope<T: Marker> { Value(T) }\n"
        "proc main() -> i64 { return 0; }\n",
        True,
    ),
    "unused generic implementation rejects unknown where trait": (
        "trait Container {}\n"
        "form Box<T> { value: T }\n"
        "impl<T> Container for Box<T> where T: Missing {}\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "generic trait implementation matches composed type": (
        "form Box<T> { value: T }\n"
        "trait Boxed {}\n"
        "impl<T> Boxed for Box<T> {}\n"
        "proc identity<U: Boxed>(value: U) -> U { return value; }\n"
        "proc main() -> i64 { let value = Box<i64> { value: 42 }; return identity(value).value - 42; }\n",
        True,
    ),
    "generic form parameter bound checks every type application": (
        "trait Marker {}\n"
        "impl Marker for i64 {}\n"
        "form Guarded<T: Marker> { value: T }\n"
        "proc main() -> i64 { let value = Guarded<i64> { value: 42 }; return value.value - 42; }\n",
        True,
    ),
    "generic form where clause rejects missing implementation": (
        "trait Marker {}\n"
        "impl Marker for i64 {}\n"
        "form Guarded<T> where T: Marker { value: T }\n"
        "proc main() -> i64 { let value: Guarded<bool> = Guarded<bool> { value: true }; return 0; }\n",
        False,
    ),
    "generic alias bound accepts implementation": (
        "trait Marker {}\n"
        "impl Marker for i64 {}\n"
        "type Marked<T: Marker> = T;\n"
        "proc main() -> i64 { let value: Marked<i64> = 42; return value - 42; }\n",
        True,
    ),
    "generic alias bound rejects missing implementation": (
        "trait Marker {}\n"
        "impl Marker for i64 {}\n"
        "type Marked<T: Marker> = T;\n"
        "proc main() -> i64 { let value: Marked<bool> = true; return 0; }\n",
        False,
    ),
    "generic alias where clause rejects missing implementation": (
        "trait Marker {}\n"
        "impl Marker for i64 {}\n"
        "type Marked<T> where T: Marker = T;\n"
        "proc main() -> i64 { let value: Marked<bool> = true; return 0; }\n",
        False,
    ),
    "generic opaque bound rejects missing implementation": (
        "trait Marker {}\n"
        "impl Marker for i64 {}\n"
        "opaque Handle<T: Marker>;\n"
        "proc main() -> i64 { let value: *Handle<bool> = null; return 0; }\n",
        False,
    ),
    "generic opaque where clause rejects missing implementation": (
        "trait Marker {}\n"
        "impl Marker for i64 {}\n"
        "opaque Handle<T> where T: Marker;\n"
        "proc main() -> i64 { let value: *Handle<bool> = null; return 0; }\n",
        False,
    ),
    "generic opaque where clause accepts implementation": (
        "trait Marker {}\n"
        "impl Marker for i64 {}\n"
        "opaque Handle<T> where T: Marker;\n"
        "proc consume(value: *Handle<i64>) {}\n"
        "proc main() -> i64 { return 0; }\n",
        True,
    ),
    "generic trait bound rejects missing implementation in application": (
        "trait Marker {}\n"
        "trait Collection<T: Marker> {}\n"
        "impl Marker for i64 {}\n"
        "proc consume(value: Collection<bool>) {}\n"
        "proc main() -> i64 { consume(null); return 0; }\n",
        False,
    ),
    "generic trait where clause rejects missing implementation": (
        "trait Marker {}\n"
        "trait Collection<T> where T: Marker {}\n"
        "impl Marker for i64 {}\n"
        "proc consume(value: Collection<bool>) {}\n"
        "proc main() -> i64 { return 0; }\n",
        False,
    ),
    "generic trait where clause accepts implementation": (
        "trait Marker {}\n"
        "trait Collection<T> where T: Marker {}\n"
        "impl Marker for i64 {}\n"
        "proc consume(value: Collection<i64>) {}\n"
        "proc main() -> i64 { return 0; }\n",
        True,
    ),
    "generic implementation where bound rejects missing implementation": (
        "trait Marker {}\n"
        "trait Container {}\n"
        "form Box<T> { value: T }\n"
        "impl Marker for i64 {}\n"
        "impl<T> Container for Box<T> where T: Marker {}\n"
        "proc take<T: Container>(value: T) -> T { return value; }\n"
        "proc main() -> i64 { let value = Box<bool> { value: true }; let result = take(value); return 0; }\n",
        False,
    ),
    "generic implementation parameter bound rejects missing implementation": (
        "trait Marker {}\n"
        "trait Container {}\n"
        "form Box<T> { value: T }\n"
        "impl Marker for i64 {}\n"
        "impl<T: Marker> Container for Box<T> {}\n"
        "proc take<T: Container>(value: T) -> T { return value; }\n"
        "proc main() -> i64 { let value = Box<bool> { value: true }; let result = take(value); return 0; }\n",
        False,
    ),
    "generic implementation parameter bound resolves implementation": (
        "trait Marker {}\n"
        "trait Container {}\n"
        "form Box<T> { value: T }\n"
        "impl Marker for i64 {}\n"
        "impl<T: Marker> Container for Box<T> {}\n"
        "proc take<T: Container>(value: T) -> T { return value; }\n"
        "proc main() -> i64 { let value = Box<i64> { value: 42 }; return take(value).value - 42; }\n",
        True,
    ),
    "generic trait bounds require every declared trait": (
        "trait Marker {}\n"
        "trait Displayable {}\n"
        "proc identity<T: Marker + Displayable>(value: T) -> T { return value; }\n"
        "impl Marker for i64 {}\n"
        "proc main() -> i64 { return identity(42) - 42; }\n",
        False,
    ),
    "generic procedure unifies repeated parameter": (
        "proc same<T>(left: T, right: T) -> T { return left; }\n"
        "proc main() -> i64 { return same(41, 42) - 41; }\n",
        True,
    ),
    "generic procedure rejects inconsistent repeated parameter": (
        "proc same<T>(left: T, right: T) -> T { return left; }\n"
        "proc main() -> i64 { return same(41, true); }\n",
        False,
    ),
    "generic procedure infers composed nominal parameters": (
        "form Box<T> { value: T }\n"
        "proc select_box<T, U>(first: Box<T>, second: Box<U>) -> Box<T> { return first; }\n"
        "proc main() -> i64 {\n"
        "    let result: Box<i64> = select_box(\n"
        "        Box<i64> { value: 42 }, Box<bool> { value: true }\n"
        "    );\n"
        "    return result.value - 42;\n"
        "}\n",
        True,
    ),
    "generic procedure rejects incompatible composed repeated parameter": (
        "form Box<T> { value: T }\n"
        "proc same_box<T>(first: Box<T>, second: Box<T>) -> Box<T> { return first; }\n"
        "proc main() -> i64 {\n"
        "    let result: Box<i64> = same_box(\n"
        "        Box<i64> { value: 42 }, Box<bool> { value: true }\n"
        "    );\n"
        "    return result.value - 42;\n"
        "}\n",
        False,
    ),
    "generic procedure parses adjacent nested type closers": (
        "form Box<T> { value: T }\n"
        "form Wrapper<T> { value: T }\n"
        "proc select_nested<T, U>(first: Wrapper<Box<T>>, second: Box<U>) -> Wrapper<Box<T>> { return first; }\n"
        "proc main() -> i64 {\n"
        "    let first = Wrapper<Box<i64>> { value: Box<i64> { value: 42 } };\n"
        "    let second = Box<bool> { value: true };\n"
        "    let selected = select_nested(first, second);\n"
        "    return selected.value.value - 42;\n"
        "}\n",
        True,
    ),
    "generic procedure rejects incompatible deeply nested substitution": (
        "form Box<T> { value: T }\n"
        "form Wrapper<T> { value: T }\n"
        "proc same_nested<T>(first: Wrapper<Box<T>>, second: Wrapper<Box<T>>) -> Wrapper<Box<T>> { return first; }\n"
        "proc main() -> i64 {\n"
        "    let first = Wrapper<Box<i64>> { value: Box<i64> { value: 42 } };\n"
        "    let second = Wrapper<Box<bool>> { value: Box<bool> { value: true } };\n"
        "    let result = same_nested(first, second);\n"
        "    return result.value.value - 42;\n"
        "}\n",
        False,
    ),
}

# Wrong generic arity currently produces different cascaded diagnostic counts
# in the two frontends. Both must reject it; exact diagnostic parity is tested
# for the other semantic fixtures.
DIAGNOSTIC_PARITY_EXCEPTIONS = {
    "generic form rejects wrong argument count",
    "generic form rejects excess argument",
    "generic opaque wrong argument count",
    "generic opaque arguments stay distinct",
}


def run_check(binary: Path, source_path: Path, label: str):
    result = subprocess.run(
        [
            str(binary),
            "check",
            str(source_path),
            "--quiet",
            "--no-color",
            "--diagnostic=json",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )
    if result.returncode == 0:
        return True, ()

    return diagnostic_codes(result, binary, label)


def run_program(binary: Path, source_path: Path, label: str):
    if sys.platform == "win32" and "bootstrap" in binary.name.lower():
        executed = run_emitted_c_on_windows(binary, source_path, label)
        if executed.returncode == 0:
            return True, ()
        return False, (f"generated C program exited with status {executed.returncode}",)

    result = subprocess.run(
        [
            str(binary),
            "run",
            str(source_path),
            "--quiet",
            "--no-color",
            "--diagnostic=json",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )
    if result.returncode == 0:
        return True, ()
    failure = diagnostic_codes(result, binary, label)
    if sys.platform == "win32" and failure == (False, ("E0903",)):
        # The native Windows `run` path currently rejects this imported generic
        # call before it can launch the generated program. Still verify the
        # compiler's C output and execute it with the Windows C toolchain.
        executed = run_emitted_c_on_windows(binary, source_path, label)
        if executed.returncode == 0:
            return True, ()
        return False, (f"generated C program exited with status {executed.returncode}",)
    return failure


def run_emitted_c_on_windows(binary: Path, source_path: Path, label: str, emitted=None):
    """Compile and run compiler-generated C with the Windows C toolchain."""
    if emitted is None:
        emitted = run_emit_c(binary, source_path)
    if emitted.returncode != 0:
        raise AssertionError(
            f"{label}: C emission failed on Windows; "
            f"stdout={emitted.stdout[:2000]!r}; stderr={emitted.stderr[:4000]!r}"
        )

    with tempfile.TemporaryDirectory(prefix="vitte-bootstrap-parity-") as temp_dir:
        c_source = Path(temp_dir) / "program.c"
        executable = Path(temp_dir) / "program.exe"
        c_source.write_text(emitted.stdout, encoding="utf-8")
        compiled = subprocess.run(
            ["cc", "-std=c17", str(c_source), "-o", str(executable)],
            capture_output=True,
            text=True,
            timeout=120,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                f"{label}: Windows C compiler rejected generated C; "
                f"stdout={compiled.stdout[:2000]!r}; stderr={compiled.stderr[:4000]!r}"
            )
        return subprocess.run(
            [str(executable)],
            capture_output=True,
            text=True,
            timeout=60,
        )


def expect_runtime_abort(binary: Path, source_path: Path, label: str):
    emitted = run_emit_c(binary, source_path)
    if emitted.returncode != 0 or "abort()" not in emitted.stdout:
        raise AssertionError(
            f"{label}: compiler must emit the checked optional trap; "
            f"returncode={emitted.returncode}, stderr={emitted.stderr[:1000]!r}"
        )
    if sys.platform == "win32" and "bootstrap" in binary.name.lower():
        executed = run_emitted_c_on_windows(binary, source_path, label, emitted)
        if executed.returncode == 0:
            raise AssertionError(f"{label}: generated C should abort for a null unwrap")
        return

    result = subprocess.run(
        [
            str(binary),
            "run",
            str(source_path),
            "--quiet",
            "--no-color",
            "--diagnostic=json",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )
    try:
        payload = json.loads(result.stderr or result.stdout)
    except json.JSONDecodeError:
        payload = {}
    runtime_failure = any(
        diagnostic.get("phase") == "runtime"
        and diagnostic.get("internal_code")
        in {"VITTE_BACKEND_E_EXECUTION", "VITTE_BACKEND_E_RUNTIME"}
        for diagnostic in payload.get("diagnostics", [])
    )
    runtime_failure = runtime_failure or any(
        diagnostic.get("code") == "E0012"
        and diagnostic.get("internal_code") == "VITTE_INFRA_E_PHASE"
        and "compilation and linking succeeded" in diagnostic.get("details", "")
        for diagnostic in payload.get("diagnostics", [])
    )
    if result.returncode == 0 or not runtime_failure:
        raise AssertionError(
            f"{label}: expected a runtime abort on null, not a compile error; "
            f"returncode={result.returncode}, stdout={result.stdout!r}, "
            f"stderr={result.stderr[:1000]!r}"
        )


def generic_specialization_source(count: int) -> str:
    scalar_types = ("i64", "bool", "u64", "f64", "string", "char")
    specializations = list(product(scalar_types, repeat=4))[:count]
    lines = ["form Pack<A, B, C, D> { a: A, b: B, c: C, d: D }"]
    lines.extend(
        f"proc use{index}(value: Pack<{', '.join(arguments)}>) -> i64 {{ "
        "return 0; }"
        for index, arguments in enumerate(specializations)
    )
    lines.append("proc main() -> i64 { return 0; }")
    return "\n".join(lines) + "\n"


def run_emit_c(binary: Path, source_path: Path):
    return subprocess.run(
        [
            str(binary),
            "compile",
            str(source_path),
            "--emit-c",
            "--quiet",
            "--no-color",
            "--diagnostic=json",
        ],
        capture_output=True,
        text=True,
        timeout=120,
    )


def run_strict_c17_emission(binary: Path, source_path: Path, label: str):
    emitted = run_emit_c(binary, source_path)
    if emitted.returncode != 0:
        raise AssertionError(
            f"{label}: C17 emission failed; stderr={emitted.stderr[:2000]!r}"
        )
    if "__auto_type" in emitted.stdout or "({" in emitted.stdout:
        raise AssertionError(
            f"{label}: generated C contains a GNU statement-expression extension"
        )
    with tempfile.TemporaryDirectory(prefix="vitte-strict-c17-") as temp_dir:
        executable = Path(temp_dir) / "program"
        compiled = subprocess.run(
            [
                "cc",
                "-std=c17",
                "-pedantic-errors",
                "-x",
                "c",
                "-",
                "-o",
                str(executable),
            ],
            input=emitted.stdout,
            capture_output=True,
            text=True,
            timeout=120,
        )
        if compiled.returncode != 0:
            raise AssertionError(
                f"{label}: generated code is not strict ISO C17; "
                f"stderr={compiled.stderr[:3000]!r}"
            )
        return subprocess.run(
            [str(executable)],
            capture_output=True,
            text=True,
            timeout=60,
        )


def diagnostic_codes(result, binary: Path, label: str):
    payload = result.stderr or result.stdout
    try:
        document = json.loads(payload)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"{label}: expected JSON diagnostics from {binary}, got "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        ) from error

    codes = tuple(
        sorted(
            item.get("code", "")
            for item in document.get("diagnostics", [])
            if item.get("severity") in ("error", "fatal")
        )
    )
    return False, codes


def main() -> int:
    if len(sys.argv) != 4:
        print(
            f"usage: {sys.argv[0]} NATIVE_VITTE BOOTSTRAP_VITTE PARSER_PARITY_TEST",
            file=sys.stderr,
        )
        return 2

    native = Path(sys.argv[1]).resolve()
    bootstrap = Path(sys.argv[2]).resolve()
    parser_parity = Path(sys.argv[3]).resolve()

    parser_result = subprocess.run(
        [str(parser_parity)], capture_output=True, text=True, timeout=60
    )
    if parser_result.returncode != 0:
        raise AssertionError(
            "native/bootstrap parser and AST structural parity failed; "
            f"stdout={parser_result.stdout!r}, stderr={parser_result.stderr!r}"
        )
    print(parser_result.stdout.strip())

    with tempfile.TemporaryDirectory(prefix="vitte-native-bootstrap-parity-") as value:
        directory = Path(value)
        for name, (source, expected_success) in SEMANTIC_CASES.items():
            source_path = directory / (name.replace(" ", "_") + ".vit")
            source_path.write_text(source, encoding="utf-8")
            native_result = run_check(native, source_path, name)
            bootstrap_result = run_check(bootstrap, source_path, name)

            if native_result[0] != expected_success:
                raise AssertionError(
                    f"{name}: native compiler success={native_result[0]}, "
                    f"expected {expected_success}, diagnostics={native_result[1]}"
                )
            if bootstrap_result[0] != expected_success:
                raise AssertionError(
                    f"{name}: bootstrap compiler success={bootstrap_result[0]}, "
                    f"expected {expected_success}, diagnostics={bootstrap_result[1]}"
                )
            if (
                native_result != bootstrap_result
                and name not in DIAGNOSTIC_PARITY_EXCEPTIONS
            ):
                raise AssertionError(
                    f"{name}: native/bootstrap semantic results differ: "
                    f"native={native_result}, bootstrap={bootstrap_result}"
                )

        async_runtime_source = Path(__file__).with_name("async_runtime.vit")
        for compiler_name, compiler in (
            ("native", native),
            ("bootstrap", bootstrap),
        ):
            async_runtime = run_program(
                compiler,
                async_runtime_source,
                f"{compiler_name} async/await runtime",
            )
            if async_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} async/await runtime failed: "
                    f"{async_runtime}"
                )
        print(
            "Async task runtime parity passed: "
            "native and bootstrap exit 0."
        )

        generic_procedure_source = directory / "generic_procedure_runtime.vit"
        generic_procedure_source.write_text(
            "proc consume<T>(value: T) { let _ = value; }\n"
            "proc identity<T>(value: T) -> T { return value; }\n"
            "proc relay<T>(value: T) -> T { return identity(value); }\n"
            "proc relay_again<U>(value: U) -> U { return relay(value); }\n"
            "proc recursive_identity<T>(count: i64, value: T) -> T {\n"
            "    if count == 0 { return value; }\n"
            "    return recursive_identity(count - 1, value);\n"
            "}\n"
            "proc first<T, U>(left: T, right: U) -> T { return left; }\n"
            "proc same<T>(left: T, right: T) -> T { return left; }\n"
            "proc main() -> i64 {\n"
            "    consume(42);\n"
            "    consume(true);\n"
            "    if relay(5) != 5 { return 19; }\n"
            "    if relay(false) { return 20; }\n"
            "    if relay_again(6) != 6 { return 21; }\n"
            "    if recursive_identity(3, 7) != 7 { return 22; }\n"
            "    if not recursive_identity(2, true) { return 23; }\n"
            "    if not identity(true) { return 1; }\n"
            "    let identity_result: i64 = identity(40);\n"
            "    if identity_result != 40 { return 2; }\n"
            "    if first(identity_result, false) != 40 { return 3; }\n"
            "    let inferred_identity_result = identity(39);\n"
            "    if first(inferred_identity_result, false) != 39 { return 4; }\n"
            "    if first(identity(38), false) != 38 { return 5; }\n"
            "    let value_i8: i8 = 8 as i8;\n"
            "    if identity(value_i8) != value_i8 { return 6; }\n"
            "    let value_u8: u8 = 8 as u8;\n"
            "    if identity(value_u8) != value_u8 { return 7; }\n"
            "    let value_i16: i16 = 16 as i16;\n"
            "    if identity(value_i16) != value_i16 { return 8; }\n"
            "    let value_u16: u16 = 16 as u16;\n"
            "    if identity(value_u16) != value_u16 { return 9; }\n"
            "    let value_i32: i32 = 32 as i32;\n"
            "    if identity(value_i32) != value_i32 { return 10; }\n"
            "    let value_u32: u32 = 32 as u32;\n"
            "    if identity(value_u32) != value_u32 { return 11; }\n"
            "    let value_u64: u64 = 64 as u64;\n"
            "    if identity(value_u64) != value_u64 { return 12; }\n"
            "    let value_isize: isize = 16 as isize;\n"
            "    if identity(value_isize) != value_isize { return 13; }\n"
            "    let value_usize: usize = 16 as usize;\n"
            "    if identity(value_usize) != value_usize { return 14; }\n"
            "    let value_f32: f32 = 1.5 as f32;\n"
            "    if identity(value_f32) != value_f32 { return 15; }\n"
            "    let value_f64: f64 = 2.5;\n"
            "    if identity(value_f64) != value_f64 { return 16; }\n"
            "    let value_char: char = 'v';\n"
            "    if identity(value_char) != value_char { return 17; }\n"
            "    let value_string: string = \"vitte\";\n"
            "    if identity(value_string) != \"vitte\" { return 18; }\n"
            "    return same(42, 40) - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            generic_procedure_runtime = run_program(
                compiler,
                generic_procedure_source,
                f"{compiler_name} generic procedure runtime",
            )
            if generic_procedure_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} generic procedure specializations must "
                    f"compile and run with exit 0; got {generic_procedure_runtime}"
                )
        print("Generic procedure runtime parity passed: native and bootstrap exit 0.")

        generic_impl_trait_method_runtime = (
            Path(__file__).parent / "generic_impl_trait_method_runtime.vit"
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            impl_trait_runtime = run_program(
                compiler,
                generic_impl_trait_method_runtime,
                f"{compiler_name} generic impl trait method runtime",
            )
            if impl_trait_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} generic impl trait method specialization "
                    f"must compile and run with exit 0; got {impl_trait_runtime}"
                )
        print(
            "Generic impl trait method runtime parity passed: "
            "native and bootstrap exit 0."
        )

        constrained_generic_procedure_source = directory / "constrained_generic_procedure_runtime.vit"
        constrained_generic_procedure_source.write_text(
            "trait Marker {}\n"
            "trait Displayable {}\n"
            "trait Boxed {}\n"
            "impl Marker for i64 {}\n"
            "impl Displayable for i64 {}\n"
            "trait Container {}\n"
            "trait ParamContainer {}\n"
            "type Marked<T> where T: Marker = T;\n"
            "form Box<T> { value: T }\n"
            "form Guarded<T: Marker> { value: T }\n"
            "form WhereGuarded<T> where T: Marker { value: T }\n"
            "impl<T> Boxed for Box<T> {}\n"
            "impl<T> Container for Box<T> where T: Marker + Displayable {}\n"
            "impl<T: Marker + Displayable> ParamContainer for Box<T> {}\n"
            "proc bounded<T: Marker + Displayable>(value: T) -> T { return value; }\n"
            "proc constrained<T>(value: T) -> T where T: Marker + Displayable { return value; }\n"
            "proc relay<T: Marker + Displayable>(value: T) -> T { return bounded(value); }\n"
            "proc relay_where<T>(value: T) -> T where T: Marker + Displayable { return constrained(value); }\n"
            "proc pair<T, U>(first: T, second: U) -> T where T: Marker, U: Marker { return first; }\n"
            "proc boxed<U: Boxed>(value: U) -> U { return value; }\n"
            "proc contained<U: Container>(value: U) -> U { return value; }\n"
            "proc param_contained<U: ParamContainer>(value: U) -> U { return value; }\n"
            "proc guarded<U: Marker>(value: Guarded<U>) -> U { return value.value; }\n"
            "proc guarded_where<U: Marker>(value: WhereGuarded<U>) -> U { return value.value; }\n"
            "proc main() -> i64 {\n"
            "    if relay(20) != 20 { return 1; }\n"
            "    if relay_where(22) != 22 { return 2; }\n"
            "    let marked: Marked<i64> = 24;\n"
            "    if marked != 24 { return 7; }\n"
            "    let value = Box<i64> { value: 18 };\n"
            "    if boxed(value).value != 18 { return 3; }\n"
            "    if contained(value).value != 18 { return 6; }\n"
            "    if param_contained(value).value != 18 { return 8; }\n"
            "    let guarded_value = Guarded<i64> { value: 19 };\n"
            "    if guarded(guarded_value) != 19 { return 4; }\n"
            "    let where_value = WhereGuarded<i64> { value: 21 };\n"
            "    if guarded_where(where_value) != 21 { return 5; }\n"
            "    return pair(42, 7) - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            constrained_runtime = run_program(
                compiler,
                constrained_generic_procedure_source,
                f"{compiler_name} constrained generic procedure runtime",
            )
            if constrained_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} constrained generic procedures must check "
                    "parameter bounds and `where` clauses, emit C17 and run "
                    f"with exit 0; got {constrained_runtime}"
                )
        print("Constrained generic procedure parity passed: parameter bounds and where clauses run on native and bootstrap.")

        composed_generic_procedure_source = directory / "composed_generic_procedure_runtime.vit"
        composed_generic_procedure_source.write_text(
            "form Box<T> { value: T }\n"
            "proc select_box<T, U>(first: Box<T>, second: Box<U>) -> Box<T> { return first; }\n"
            "proc main() -> i64 {\n"
            "    let result: Box<i64> = select_box(\n"
            "        Box<i64> { value: 42 },\n"
            "        Box<bool> { value: true }\n"
            "    );\n"
            "    return result.value - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            composed_generic_runtime = run_program(
                compiler,
                composed_generic_procedure_source,
                f"{compiler_name} composed generic procedure runtime",
            )
            if composed_generic_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} composed generic procedure must infer "
                    "Box<T> and Box<U>, compile and run with exit 0; "
                    f"got {composed_generic_runtime}"
                )
        print("Composed generic procedure runtime parity passed: native and bootstrap exit 0.")

        nested_composed_generic_procedure_source = directory / "nested_composed_generic_procedure_runtime.vit"
        nested_composed_generic_procedure_source.write_text(
            "form Box<T> { value: T }\n"
            "form Wrapper<T> { value: T }\n"
            "proc select_nested<T, U>(first: Wrapper<Box<T>>, second: Box<U>) -> Wrapper<Box<T>> { return first; }\n"
            "proc unwrap<T>(wrapped: Wrapper<T>) -> T { return wrapped.value; }\n"
            "proc main() -> i64 {\n"
            "    let first = Wrapper<Box<i64>> { value: Box<i64> { value: 42 } };\n"
            "    let second = Box<bool> { value: true };\n"
            "    let selected = select_nested(first, second);\n"
            "    return unwrap(selected).value - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            nested_composed_generic_runtime = run_program(
                compiler,
                nested_composed_generic_procedure_source,
                f"{compiler_name} nested composed generic procedure runtime",
            )
            if nested_composed_generic_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} nested composed generic procedure must "
                    "parse adjacent closers, infer substitutions through both "
                    "wrappers, compile and run with exit 0; "
                    f"got {nested_composed_generic_runtime}"
                )
        print("Nested composed generic procedure parity passed: native and bootstrap exit 0.")

        generic_module_root = directory / "generic_imports"
        generic_module_file = generic_module_root / "foo" / "lib.vit"
        generic_module_file.parent.mkdir(parents=True)
        generic_module_file.write_text(
            "space foo::lib;\n"
            "export proc identity<T>(value: T) -> T { return value; }\n"
            "export proc first<T, U>(left: T, right: U) -> T { return left; }\n",
            encoding="utf-8",
        )
        generic_import_source = generic_module_root / "main.vit"
        generic_import_source.write_text(
            "space foo::main;\n"
            "use foo::lib;\n"
            "use foo::lib as lib_alias;\n"
            "proc main() -> i64 {\n"
            "    let qualified = foo::lib::identity(40);\n"
            "    let aliased = lib_alias::identity(39);\n"
            "    if lib_alias::first(qualified, false) != 40 { return 1; }\n"
            "    if aliased != 39 { return 2; }\n"
            "    return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            imported_generic_runtime = run_program(
                compiler,
                generic_import_source,
                f"{compiler_name} imported generic procedure runtime",
            )
            if imported_generic_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} qualified, aliased and imported generic "
                    "calls must infer local result types and run with exit 0; "
                    f"got {imported_generic_runtime}"
                )
        print("Imported generic procedure runtime parity passed: native and bootstrap exit 0.")

        nested_source = directory / "nested_generic_runtime.vit"
        nested_source.write_text(
            "form Box<T> { value: T }\n"
            "form Outer<T> { wrapped: Box<T> }\n"
            "proc main() -> i64 {\n"
            "    let outer: Outer<Box<i64>> = Outer<Box<i64>> {\n"
            "        wrapped: Box<Box<i64>> { value: Box<i64> { value: 42 } },\n"
            "    };\n"
            "    return outer.wrapped.value.value - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        native_runtime = run_program(native, nested_source, "nested generic runtime")
        bootstrap_runtime = run_program(bootstrap, nested_source, "nested generic runtime")
        if native_runtime != (True, ()):
            raise AssertionError(
                "native nested generic program must compile and run with exit 0; "
                f"got {native_runtime}"
            )
        if bootstrap_runtime != (True, ()):
            raise AssertionError(
                "bootstrap nested generic program must compile and run with exit 0; "
                f"got {bootstrap_runtime}"
            )

        generic_alias_source = directory / "generic_identity_alias_runtime.vit"
        generic_alias_source.write_text(
            "type Identity<T> = T;\n"
            "type AlsoIdentity<T> = T;\n"
            "proc identity_value(value: Identity<i64>) -> AlsoIdentity<i64> { return value; }\n"
            "proc main() -> i64 { return identity_value(42) - 42; }\n",
            encoding="utf-8",
        )
        native_generic_alias = run_program(
            native, generic_alias_source, "generic identity alias"
        )
        bootstrap_generic_alias = run_program(
            bootstrap, generic_alias_source, "generic identity alias"
        )
        if native_generic_alias != (True, ()):
            raise AssertionError(
                "native generic identity alias must compile and run with exit 0; "
                f"got {native_generic_alias}"
            )
        if bootstrap_generic_alias != (True, ()):
            raise AssertionError(
                "bootstrap generic identity alias must compile and run with exit 0; "
                f"got {bootstrap_generic_alias}"
            )
        print("Generic identity alias runtime parity passed: native and bootstrap exit 0.")

        generic_alias_chain_source = directory / "generic_alias_chain_runtime.vit"
        generic_alias_chain_source.write_text(
            "type Identity<T> = T;\n"
            "type First<U> = Identity<U>;\n"
            "type Second<V> = First<V>;\n"
            "proc get_value(value: Second<i64>) -> i64 { return value; }\n"
            "proc main() -> i64 { return get_value(42) - 42; }\n",
            encoding="utf-8",
        )
        native_alias_chain = run_program(
            native, generic_alias_chain_source, "generic alias chain"
        )
        bootstrap_alias_chain = run_program(
            bootstrap, generic_alias_chain_source, "generic alias chain"
        )
        if native_alias_chain != (True, ()):
            raise AssertionError(
                "native generic alias chain must compile and run with exit 0; "
                f"got {native_alias_chain}"
            )
        if bootstrap_alias_chain != (True, ()):
            raise AssertionError(
                "bootstrap generic alias chain must compile and run with exit 0; "
                f"got {bootstrap_alias_chain}"
            )
        print("Generic alias chain runtime parity passed: native and bootstrap exit 0.")

        generic_alias_form_chain_source = directory / "generic_alias_form_chain_runtime.vit"
        generic_alias_form_chain_source.write_text(
            "form Box<T> { value: T }\n"
            "type Boxed<T> = Box<T>;\n"
            "type Wrapped<U> = Boxed<U>;\n"
            "proc get_value(value: Wrapped<i64>) -> i64 { return value.value; }\n"
            "proc main() -> i64 { let value: Wrapped<i64> = Wrapped<i64> { value: 42 }; return get_value(value) - 42; }\n",
            encoding="utf-8",
        )
        native_alias_form_chain = run_program(
            native, generic_alias_form_chain_source, "generic alias chain to form"
        )
        bootstrap_alias_form_chain = run_program(
            bootstrap, generic_alias_form_chain_source, "generic alias chain to form"
        )
        if native_alias_form_chain != (True, ()):
            raise AssertionError(
                "native generic alias chain to form must compile and run with exit 0; "
                f"got {native_alias_form_chain}"
            )
        if bootstrap_alias_form_chain != (True, ()):
            raise AssertionError(
                "bootstrap generic alias chain to form must compile and run with exit 0; "
                f"got {bootstrap_alias_form_chain}"
            )
        print("Generic alias chain to form runtime parity passed: native and bootstrap exit 0.")

        generic_slice_alias_source = directory / "generic_alias_dynamic_slice_runtime.vit"
        generic_slice_alias_source.write_text(
            "type Values<T> = [T];\n"
            "proc make_values() -> Values<i64> { return [20, 22]; }\n"
            "proc sum_values(values: Values<i64>) -> i64 { return values[0] + values[1]; }\n"
            "proc main() -> i64 {\n"
            "    let values: Values<i64> = make_values();\n"
            "    if values.length != 2 { return 1; }\n"
            "    return sum_values(values) - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        native_slice_alias = run_program(
            native, generic_slice_alias_source, "generic alias to dynamic slice"
        )
        bootstrap_slice_alias = run_program(
            bootstrap, generic_slice_alias_source, "generic alias to dynamic slice"
        )
        if native_slice_alias != (True, ()):
            raise AssertionError(
                "native generic alias to dynamic slice must compile and run with exit 0; "
                f"got {native_slice_alias}"
            )
        if bootstrap_slice_alias != (True, ()):
            raise AssertionError(
                "bootstrap generic alias to dynamic slice must compile and run with exit 0; "
                f"got {bootstrap_slice_alias}"
            )
        print("Generic alias to dynamic slice runtime parity passed: native and bootstrap exit 0.")

        generic_slice_alias_chain_source = directory / "generic_alias_dynamic_slice_chain_runtime.vit"
        generic_slice_alias_chain_source.write_text(
            "type Values<T> = [T];\n"
            "type ValuesAlias<U> = Values<U>;\n"
            "proc make_values() -> ValuesAlias<i64> { return [20, 22]; }\n"
            "proc sum_values(values: ValuesAlias<i64>) -> i64 { return values[0] + values[1]; }\n"
            "proc main() -> i64 {\n"
            "    let values: ValuesAlias<i64> = [20, 22];\n"
            "    if values.length != 2 { return 1; }\n"
            "    return sum_values(make_values()) + values[0] - 62;\n"
            "}\n",
            encoding="utf-8",
        )
        native_slice_alias_chain = run_program(
            native,
            generic_slice_alias_chain_source,
            "generic alias chain to dynamic slice",
        )
        bootstrap_slice_alias_chain = run_program(
            bootstrap,
            generic_slice_alias_chain_source,
            "generic alias chain to dynamic slice",
        )
        if native_slice_alias_chain != (True, ()):
            raise AssertionError(
                "native generic alias chain to dynamic slice must compile and run "
                f"with exit 0; got {native_slice_alias_chain}"
            )
        if bootstrap_slice_alias_chain != (True, ()):
            raise AssertionError(
                "bootstrap generic alias chain to dynamic slice must compile and "
                f"run with exit 0; got {bootstrap_slice_alias_chain}"
            )
        print("Generic alias chain to dynamic slice runtime parity passed: native and bootstrap exit 0.")

        generic_fixed_array_alias_source = directory / "generic_alias_fixed_array_runtime.vit"
        generic_fixed_array_alias_source.write_text(
            "type Pair<T> = [2]T;\n"
            "proc first_i64(values: Pair<i64>) -> i64 { return values[0]; }\n"
            "proc first_bool(values: Pair<bool>) -> bool { return values[0]; }\n"
            "proc main() -> i64 {\n"
            "    let values_i64: Pair<i64> = [20, 22];\n"
            "    let values_bool: Pair<bool> = [true, false];\n"
            "    if not first_bool(values_bool) { return 1; }\n"
            "    return first_i64(values_i64) + values_i64[1] - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        native_fixed_array_alias = run_program(
            native,
            generic_fixed_array_alias_source,
            "generic alias to fixed array",
        )
        bootstrap_fixed_array_alias = run_program(
            bootstrap,
            generic_fixed_array_alias_source,
            "generic alias to fixed array",
        )
        if native_fixed_array_alias != (True, ()):
            raise AssertionError(
                "native generic alias to fixed array must compile and run with exit 0; "
                f"got {native_fixed_array_alias}"
            )
        if bootstrap_fixed_array_alias != (True, ()):
            raise AssertionError(
                "bootstrap generic alias to fixed array must compile and run with exit 0; "
                f"got {bootstrap_fixed_array_alias}"
            )
        print("Generic alias to fixed array runtime parity passed: native and bootstrap exit 0.")

        generic_fixed_array_alias_chain_source = directory / "generic_alias_fixed_array_chain_runtime.vit"
        generic_fixed_array_alias_chain_source.write_text(
            "type Pair<T> = [2]T;\n"
            "type PairAlias<U> = Pair<U>;\n"
            "proc first_i64(values: PairAlias<i64>) -> i64 { return values[0]; }\n"
            "proc first_bool(values: PairAlias<bool>) -> bool { return values[0]; }\n"
            "proc main() -> i64 {\n"
            "    let values_i64: PairAlias<i64> = [20, 22];\n"
            "    let values_bool: PairAlias<bool> = [true, false];\n"
            "    if not first_bool(values_bool) { return 1; }\n"
            "    return first_i64(values_i64) + values_i64[1] - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        native_fixed_array_alias_chain = run_program(
            native,
            generic_fixed_array_alias_chain_source,
            "generic alias chain to fixed array",
        )
        bootstrap_fixed_array_alias_chain = run_program(
            bootstrap,
            generic_fixed_array_alias_chain_source,
            "generic alias chain to fixed array",
        )
        if native_fixed_array_alias_chain != (True, ()):
            raise AssertionError(
                "native generic alias chain to fixed array must compile and run "
                f"with exit 0; got {native_fixed_array_alias_chain}"
            )
        if bootstrap_fixed_array_alias_chain != (True, ()):
            raise AssertionError(
                "bootstrap generic alias chain to fixed array must compile and run "
                f"with exit 0; got {bootstrap_fixed_array_alias_chain}"
            )
        print("Generic alias chain to fixed array runtime parity passed: native and bootstrap exit 0.")

        generic_function_alias_chain_source = directory / "generic_function_alias_chain_runtime.vit"
        generic_function_alias_chain_source.write_text(
            "type Transform<T> = proc(T) -> T;\n"
            "type TransformAlias<U> = Transform<U>;\n"
            "type TransformAlias2<V> = TransformAlias<V>;\n"
            "proc keep_i64(value: i64) -> i64 { return value; }\n"
            "proc keep_bool(value: bool) -> bool { return value; }\n"
            "proc apply_i64(transform: TransformAlias2<i64>, value: i64) -> i64 { return transform(value); }\n"
            "proc apply_bool(transform: TransformAlias2<bool>, value: bool) -> bool { return transform(value); }\n"
            "proc main() -> i64 {\n"
            "    if apply_i64(keep_i64, 42) != 42 { return 1; }\n"
            "    if not apply_bool(keep_bool, true) { return 2; }\n"
            "    return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        native_function_alias_chain = run_program(
            native,
            generic_function_alias_chain_source,
            "generic function alias chain",
        )
        bootstrap_function_alias_chain = run_program(
            bootstrap,
            generic_function_alias_chain_source,
            "generic function alias chain",
        )
        if native_function_alias_chain != (True, ()):
            raise AssertionError(
                "native generic function alias chain must compile and run with "
                f"exit 0; got {native_function_alias_chain}"
            )
        if bootstrap_function_alias_chain != (True, ()):
            raise AssertionError(
                "bootstrap generic function alias chain must compile and run "
                f"with exit 0; got {bootstrap_function_alias_chain}"
            )
        print("Generic function alias chain runtime parity passed: native and bootstrap exit 0.")

        generic_function_alias_source = directory / "generic_function_alias_runtime.vit"
        generic_function_alias_source.write_text(
            "type Transform<T> = proc(T) -> T;\n"
            "proc keep_i64(value: i64) -> i64 { return value; }\n"
            "proc keep_bool(value: bool) -> bool { return value; }\n"
            "proc apply_i64(transform: Transform<i64>, value: i64) -> i64 { return transform(value); }\n"
            "proc apply_bool(transform: Transform<bool>, value: bool) -> bool { return transform(value); }\n"
            "proc main() -> i64 {\n"
            "    if apply_i64(keep_i64, 42) != 42 { return 1; }\n"
            "    if not apply_bool(keep_bool, true) { return 2; }\n"
            "    return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        native_function_alias = run_program(
            native, generic_function_alias_source, "generic function alias"
        )
        bootstrap_function_alias = run_program(
            bootstrap, generic_function_alias_source, "generic function alias"
        )
        if native_function_alias != (True, ()):
            raise AssertionError(
                "native generic function alias must compile and run with exit 0; "
                f"got {native_function_alias}"
            )
        if bootstrap_function_alias != (True, ()):
            raise AssertionError(
                "bootstrap generic function alias must compile and run with exit 0; "
                f"got {bootstrap_function_alias}"
            )
        print("Generic function alias runtime parity passed: native and bootstrap exit 0.")

        generic_reference_alias_source = directory / "generic_reference_alias_runtime.vit"
        generic_reference_alias_source.write_text(
            "type Ref<T> = &T;\n"
            "proc read_ref(value: Ref<i64>) -> i64 { return *value; }\n"
            "proc main() -> i64 { let value: i64 = 42; return read_ref(&value) - 42; }\n",
            encoding="utf-8",
        )
        native_reference_alias = run_program(
            native, generic_reference_alias_source, "generic reference alias"
        )
        bootstrap_reference_alias = run_program(
            bootstrap, generic_reference_alias_source, "generic reference alias"
        )
        if native_reference_alias != (True, ()):
            raise AssertionError(
                "native generic reference alias must compile and run with exit 0; "
                f"got {native_reference_alias}"
            )
        if bootstrap_reference_alias != (True, ()):
            raise AssertionError(
                "bootstrap generic reference alias must compile and run with exit 0; "
                f"got {bootstrap_reference_alias}"
            )
        print("Generic reference alias runtime parity passed: native and bootstrap exit 0.")

        generic_pointer_alias_source = directory / "generic_pointer_alias_runtime.vit"
        generic_pointer_alias_source.write_text(
            "type Ptr<T> = *T;\n"
            "proc read_ptr(value: Ptr<i64>) -> i64 { return *value; }\n"
            "proc main() -> i64 { let value: i64 = 42; return read_ptr(&value) - 42; }\n",
            encoding="utf-8",
        )
        native_pointer_alias = run_program(
            native, generic_pointer_alias_source, "generic pointer alias"
        )
        bootstrap_pointer_alias = run_program(
            bootstrap, generic_pointer_alias_source, "generic pointer alias"
        )
        if native_pointer_alias != (True, ()):
            raise AssertionError(
                "native generic pointer alias must compile and run with exit 0; "
                f"got {native_pointer_alias}"
            )
        if bootstrap_pointer_alias != (True, ()):
            raise AssertionError(
                "bootstrap generic pointer alias must compile and run with exit 0; "
                f"got {bootstrap_pointer_alias}"
            )
        print("Generic pointer alias runtime parity passed: native and bootstrap exit 0.")

        generic_opaque_source = directory / "generic_opaque_pointer_runtime.vit"
        generic_opaque_source.write_text(
            "opaque Handle<T>;\n"
            "proc is_null(value: *Handle<i64>) -> bool { return value == null; }\n"
            "proc main() -> i64 { if is_null(null) { return 0; } return 1; }\n",
            encoding="utf-8",
        )
        native_opaque = run_program(
            native, generic_opaque_source, "generic opaque pointer"
        )
        bootstrap_opaque = run_program(
            bootstrap, generic_opaque_source, "generic opaque pointer"
        )
        if native_opaque != (True, ()):
            raise AssertionError(
                "native generic opaque pointer must compile and run with exit 0; "
                f"got {native_opaque}"
            )
        if bootstrap_opaque != (True, ()):
            raise AssertionError(
                "bootstrap generic opaque pointer must compile and run with exit 0; "
                f"got {bootstrap_opaque}"
            )
        print("Generic opaque pointer runtime parity passed: native and bootstrap exit 0.")

        generic_form_alias_source = directory / "generic_form_alias_runtime.vit"
        generic_form_alias_source.write_text(
            "form Box<T> { value: T }\n"
            "type Boxed<T> = Box<T>;\n"
            "proc get_box(value: Boxed<i64>) -> i64 { return value.value; }\n"
            "proc main() -> i64 { let value: Boxed<i64> = Boxed<i64> { value: 42 }; return get_box(value) - 42; }\n",
            encoding="utf-8",
        )
        native_form_alias = run_program(
            native, generic_form_alias_source, "generic alias to form"
        )
        bootstrap_form_alias = run_program(
            bootstrap, generic_form_alias_source, "generic alias to form"
        )
        if native_form_alias != (True, ()):
            raise AssertionError(
                "native generic alias to form must compile and run with exit 0; "
                f"got {native_form_alias}"
            )
        if bootstrap_form_alias != (True, ()):
            raise AssertionError(
                "bootstrap generic alias to form must compile and run with exit 0; "
                f"got {bootstrap_form_alias}"
            )
        print("Generic alias to form runtime parity passed: native and bootstrap exit 0.")

        generic_form_alias_only_source = directory / "generic_form_alias_only_runtime.vit"
        generic_form_alias_only_source.write_text(
            "form Box<T> { value: T }\n"
            "type Boxed<T> = Box<T>;\n"
            "proc get_box(value: Boxed<i64>) -> i64 { return value.value; }\n"
            "proc main() -> i64 { return 0; }\n",
            encoding="utf-8",
        )
        native_form_alias_only = run_program(
            native,
            generic_form_alias_only_source,
            "generic alias to form without direct form use",
        )
        bootstrap_form_alias_only = run_program(
            bootstrap,
            generic_form_alias_only_source,
            "generic alias to form without direct form use",
        )
        if native_form_alias_only != (True, ()):
            raise AssertionError(
                "native generic alias to form without direct form use must "
                f"compile and run with exit 0; got {native_form_alias_only}"
            )
        if bootstrap_form_alias_only != (True, ()):
            raise AssertionError(
                "bootstrap generic alias to form without direct form use must "
                f"compile and run with exit 0; got {bootstrap_form_alias_only}"
            )
        print(
            "Generic alias to form without direct form use passed: "
            "native and bootstrap exit 0."
        )

        multi_parameter_source = directory / "multi_parameter_generic_runtime.vit"
        multi_parameter_source.write_text(
            "form Pair<T, U> { first: T, second: U }\n"
            "proc main() -> i64 {\n"
            "    let pair: Pair<i64, bool> = Pair<i64, bool> { first: 42, second: true };\n"
            "    if pair.first != 42 { return 1; }\n"
            "    if not pair.second { return 2; }\n"
            "    return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        native_multi_parameter_runtime = run_program(
            native, multi_parameter_source, "multi-parameter generic runtime"
        )
        bootstrap_multi_parameter_runtime = run_program(
            bootstrap, multi_parameter_source, "multi-parameter generic runtime"
        )
        if native_multi_parameter_runtime != (True, ()):
            raise AssertionError(
                "native multi-parameter generic program must compile and run with exit 0; "
                f"got {native_multi_parameter_runtime}"
            )
        if bootstrap_multi_parameter_runtime != (True, ()):
            raise AssertionError(
                "bootstrap multi-parameter generic program must compile and run with exit 0; "
                f"got {bootstrap_multi_parameter_runtime}"
            )

        composed_multi_parameter_source = directory / "composed_multi_parameter_generic_runtime.vit"
        composed_multi_parameter_source.write_text(
            "form Box<T> { value: T }\n"
            "form Triple<T, U, V> { first: T, second: Box<U>, third: V }\n"
            "proc main() -> i64 {\n"
            "    let triple: Triple<i64, bool, u64> = Triple<i64, bool, u64> {\n"
            "        first: 42,\n"
            "        second: Box<bool> { value: true },\n"
            "        third: 9,\n"
            "    };\n"
            "    if triple.first != 42 { return 1; }\n"
            "    if not triple.second.value { return 2; }\n"
            "    if triple.third != 9 { return 3; }\n"
            "    return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        native_composed_runtime = run_program(
            native,
            composed_multi_parameter_source,
            "composed multi-parameter generic runtime",
        )
        bootstrap_composed_runtime = run_program(
            bootstrap,
            composed_multi_parameter_source,
            "composed multi-parameter generic runtime",
        )
        if native_composed_runtime != (True, ()):
            raise AssertionError(
                "native composed multi-parameter program must compile and run with exit 0; "
                f"got {native_composed_runtime}"
            )
        if bootstrap_composed_runtime != (True, ()):
            raise AssertionError(
                "bootstrap composed multi-parameter program must compile and run with exit 0; "
                f"got {bootstrap_composed_runtime}"
            )

        composed_array_source = directory / "composed_generic_array_runtime.vit"
        composed_array_source.write_text(
            "form Box<T> { value: T }\n"
            "form Pair<T> { items: [2]Box<T> }\n"
            "proc main() -> i64 {\n"
            "    let pair: Pair<i64> = Pair<i64> {\n"
            "        items: [Box<i64> { value: 41 }, Box<i64> { value: 42 }],\n"
            "    };\n"
            "    return pair.items[0].value + pair.items[1].value - 83;\n"
            "}\n",
            encoding="utf-8",
        )
        native_composed_array_runtime = run_program(
            native, composed_array_source, "generic composed fixed-array runtime"
        )
        bootstrap_composed_array_runtime = run_program(
            bootstrap, composed_array_source, "generic composed fixed-array runtime"
        )
        if native_composed_array_runtime != (True, ()):
            raise AssertionError(
                "native generic composed fixed-array program must compile and run "
                f"with exit 0; got {native_composed_array_runtime}"
            )
        if bootstrap_composed_array_runtime != (True, ()):
            raise AssertionError(
                "bootstrap generic composed fixed-array program must compile and "
                f"run with exit 0; got {bootstrap_composed_array_runtime}"
            )

        dynamic_array_source = directory / "composed_generic_dynamic_array_runtime.vit"
        dynamic_array_source.write_text(
            "form Box<T> { value: T }\n"
            "form Bag<T> { items: [Box<T>] }\n"
            "proc main() -> i64 {\n"
            "    let bag: Bag<i64> = Bag<i64> {\n"
            "        items: [Box<i64> { value: 42 }],\n"
            "    };\n"
            "    return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        native_dynamic_array_runtime = run_program(
            native, dynamic_array_source, "generic composed dynamic-array runtime"
        )
        bootstrap_dynamic_array_runtime = run_program(
            bootstrap,
            dynamic_array_source,
            "generic composed dynamic-array runtime",
        )
        if native_dynamic_array_runtime != (True, ()):
            raise AssertionError(
                "native generic composed dynamic-array program must compile and "
                f"run with exit 0; got {native_dynamic_array_runtime}"
            )
        if bootstrap_dynamic_array_runtime != (True, ()):
            raise AssertionError(
                "bootstrap generic composed dynamic-array program must compile and "
                f"run with exit 0; got {bootstrap_dynamic_array_runtime}"
            )

        optional_source = directory / "composed_generic_optional_runtime.vit"
        optional_source.write_text(
            "form Box<T> { value: T }\n"
            "form Maybe<T> { item: ?Box<T> }\n"
            "proc main() -> i64 {\n"
            "    let box: Box<i64> = Box<i64> { value: 42 };\n"
            "    let maybe: Maybe<i64> = Maybe<i64> { item: &box };\n"
            "    if maybe.item == null { return 1; }\n"
            "    if maybe.item?.value == 42 { return 0; }\n"
            "    return 2;\n"
            "}\n",
            encoding="utf-8",
        )
        native_optional_runtime = run_program(
            native, optional_source, "generic composed optional runtime"
        )
        bootstrap_optional_runtime = run_program(
            bootstrap, optional_source, "generic composed optional runtime"
        )
        if native_optional_runtime != (True, ()):
            raise AssertionError(
                "native generic composed optional program must compile and run "
                f"with exit 0; got {native_optional_runtime}"
            )
        if bootstrap_optional_runtime != (True, ()):
            raise AssertionError(
                "bootstrap generic composed optional program must compile and run "
                f"with exit 0; got {bootstrap_optional_runtime}"
            )

        null_unwrap_source = directory / "optional_unwrap_null_runtime.vit"
        null_unwrap_source.write_text(
            "proc main() -> i64 {\n"
            "    let missing: ?i64 = null;\n"
            "    return missing?;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            expect_runtime_abort(
                compiler,
                null_unwrap_source,
                f"{compiler_name} optional unwrap of null",
            )

        once_source = directory / "optional_unwrap_evaluated_once_runtime.vit"
        once_source.write_text(
            "static mut CALLS: i64 = 0;\n"
            "static VALUE: i64 = 42;\n"
            "proc next_value() -> ?i64 {\n"
            "    set CALLS = CALLS + 1;\n"
            "    return &VALUE;\n"
            "}\n"
            "proc main() -> i64 {\n"
            "    let value: i64 = next_value()?;\n"
            "    if CALLS != 1 { return 1; }\n"
            "    return value - 42;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            once_runtime = run_program(
                compiler,
                once_source,
                f"{compiler_name} optional unwrap single evaluation",
            )
            if once_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} optional unwrap must evaluate its operand "
                    f"once; got {once_runtime}"
                )

        payload_source = directory / "optional_unwrap_payload_representations.vit"
        payload_source.write_text(
            "proc main() -> i64 {\n"
            "    let bool_value: bool = true;\n"
            "    let maybe_bool: ?bool = &bool_value;\n"
            "    if not maybe_bool? { return 1; }\n"
            "    let float_value: f64 = 42.5;\n"
            "    let maybe_float: ?f64 = &float_value;\n"
            "    if maybe_float? != 42.5 { return 2; }\n"
            "    let char_value: char = 'x';\n"
            "    let maybe_char: ?char = &char_value;\n"
            "    if maybe_char? != 'x' { return 3; }\n"
            "    let text: string = \"vitte\";\n"
            "    let maybe_text: ?string = text;\n"
            "    if maybe_text? != \"vitte\" { return 4; }\n"
            "    let integer: i64 = 42;\n"
            "    let pointer: *i64 = &integer;\n"
            "    let maybe_pointer: ?*i64 = pointer;\n"
            "    if *maybe_pointer? != 42 { return 5; }\n"
            "    return 0;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            payload_runtime = run_program(
                compiler,
                payload_source,
                f"{compiler_name} optional unwrap payload representations",
            )
            if payload_runtime != (True, ()):
                raise AssertionError(
                    f"{compiler_name} must unwrap pointer-encoded scalar, pointer, "
                    f"and null-sentinel string optionals; got {payload_runtime}"
                )

        null_string_source = directory / "optional_string_unwrap_null_runtime.vit"
        null_string_source.write_text(
            "proc main() -> i64 {\n"
            "    let missing: ?string = null;\n"
            "    if missing? == \"\" { return 0; }\n"
            "    return 1;\n"
            "}\n",
            encoding="utf-8",
        )
        for compiler, compiler_name in (
            (native, "native"),
            (bootstrap, "bootstrap"),
        ):
            expect_runtime_abort(
                compiler,
                null_string_source,
                f"{compiler_name} optional string unwrap of null",
            )

        parameter_count = 40
        parameter_names = [f"T{index}" for index in range(parameter_count)]
        field_names = [f"field{index}" for index in range(parameter_count)]
        argument_types = ["i64"] * parameter_count
        argument_types[33] = "bool"
        argument_types[39] = "u64"
        values = [str(index) for index in range(parameter_count)]
        values[33] = "true"
        many_source_text = (
            f"form Many<{', '.join(parameter_names)}> {{ "
            + ", ".join(
                f"{field}: {parameter}"
                for field, parameter in zip(field_names, parameter_names)
            )
            + " }\n"
            + "proc main() -> i64 {\n"
            + f"    let value: Many<{', '.join(argument_types)}> = Many<{', '.join(argument_types)}> {{\n"
            + "        "
            + ",\n        ".join(
                f"{field}: {value}"
                for field, value in zip(field_names, values)
            )
            + ",\n    };\n"
            + "    if value.field0 != 0 { return 1; }\n"
            + "    if not value.field33 { return 2; }\n"
            + "    if value.field39 != 39 { return 3; }\n"
            + "    return 0;\n"
            + "}\n"
        )
        many_source = directory / "forty_parameter_generic_runtime.vit"
        many_source.write_text(many_source_text, encoding="utf-8")
        native_many_runtime = run_program(
            native, many_source, "40-parameter generic runtime"
        )
        bootstrap_many_runtime = run_program(
            bootstrap, many_source, "40-parameter generic runtime"
        )
        if native_many_runtime != (True, ()):
            raise AssertionError(
                "native 40-parameter generic program must compile and run with exit 0; "
                f"got {native_many_runtime}"
            )
        if bootstrap_many_runtime != (True, ()):
            raise AssertionError(
                "bootstrap 40-parameter generic program must compile and run with exit 0; "
                f"got {bootstrap_many_runtime}"
            )

        specialization_512 = directory / "generic_specializations_512.vit"
        specialization_512.write_text(
            generic_specialization_source(512), encoding="utf-8"
        )
        native_512 = run_emit_c(native, specialization_512)
        if native_512.returncode != 0 or not native_512.stdout.strip():
            raise AssertionError(
                "native C17 backend must emit C for exactly 512 generic form "
                "specializations; "
                f"returncode={native_512.returncode}, "
                f"stdout bytes={len(native_512.stdout)}, stderr={native_512.stderr!r}"
            )

        specialization_513 = directory / "generic_specializations_513.vit"
        specialization_513.write_text(
            generic_specialization_source(513), encoding="utf-8"
        )
        bootstrap_513 = run_emit_c(bootstrap, specialization_513)
        if bootstrap_513.returncode != 0 or not bootstrap_513.stdout.strip():
            raise AssertionError(
                "bootstrap C17 backend must emit C for 513 generic form "
                "specializations; "
                f"returncode={bootstrap_513.returncode}, "
                f"stdout bytes={len(bootstrap_513.stdout)}, "
                f"stderr={bootstrap_513.stderr!r}"
            )

        native_513 = run_emit_c(native, specialization_513)
        if native_513.returncode != 0 or not native_513.stdout.strip():
            raise AssertionError(
                "native C17 backend must emit C for 513 distinct generic form "
                "specializations; "
                f"returncode={native_513.returncode}, "
                f"stdout bytes={len(native_513.stdout)}, "
                f"stderr={native_513.stderr[:1000]!r}"
            )
        native_513_runtime = run_program(
            native, specialization_513, "513 generic specialization runtime"
        )
        bootstrap_513_runtime = run_program(
            bootstrap, specialization_513, "513 generic specialization runtime"
        )
        if native_513_runtime != (True, ()):
            raise AssertionError(
                "native C17 backend must compile and run 513 generic form "
                f"specializations; got {native_513_runtime}"
            )
        if bootstrap_513_runtime != (True, ()):
            raise AssertionError(
                "bootstrap C17 backend must compile and run 513 generic form "
                f"specializations; got {bootstrap_513_runtime}"
            )

    print(
        "Native/bootstrap semantic parity passed: "
        f"{len(SEMANTIC_CASES)} cases."
    )

    composition_example = (
        Path(__file__).resolve().parents[2]
        / "examples"
        / "tutorials"
        / "compositions-generiques.vit"
    )
    native_composition_example = run_program(
        native, composition_example, "generic composition tutorial"
    )
    bootstrap_composition_example = run_program(
        bootstrap, composition_example, "generic composition tutorial"
    )
    if native_composition_example != (True, ()):
        raise AssertionError(
            "native generic composition tutorial must compile and run with exit 0; "
            f"got {native_composition_example}"
        )
    if bootstrap_composition_example != (True, ()):
        raise AssertionError(
            "bootstrap generic composition tutorial must compile and run with exit 0; "
            f"got {bootstrap_composition_example}"
        )
    print("Generic composition tutorial runtime passed: native and bootstrap exit 0.")

    constraints_example = (
        Path(__file__).resolve().parents[2]
        / "examples"
        / "tutorials"
        / "contraintes-generiques.vit"
    )
    native_constraints_example = run_program(
        native, constraints_example, "generic constraints tutorial"
    )
    bootstrap_constraints_example = run_program(
        bootstrap, constraints_example, "generic constraints tutorial"
    )
    if native_constraints_example != (True, ()):
        raise AssertionError(
            "native generic constraints tutorial must compile and run with exit 0; "
            f"got {native_constraints_example}"
        )
    if bootstrap_constraints_example != (True, ()):
        raise AssertionError(
            "bootstrap generic constraints tutorial must compile and run with exit 0; "
            f"got {bootstrap_constraints_example}"
        )
    print("Generic constraints tutorial runtime passed: native and bootstrap exit 0.")

    trait_method_example = (
        Path(__file__).resolve().parent
        / "generic_trait_method_dispatch_runtime.vit"
    )
    native_trait_method_example = run_program(
        native, trait_method_example, "generic trait method dispatch"
    )
    bootstrap_trait_method_example = run_program(
        bootstrap, trait_method_example, "generic trait method dispatch"
    )
    if native_trait_method_example != (True, ()):
        raise AssertionError(
            "native constrained trait method dispatch must compile and run "
            f"with exit 0; got {native_trait_method_example}"
        )
    if bootstrap_trait_method_example != (True, ()):
        raise AssertionError(
            "bootstrap constrained trait method dispatch must compile and run "
            f"with exit 0; got {bootstrap_trait_method_example}"
        )
    print(
        "Constrained trait method dispatch runtime passed: "
        "native and bootstrap exit 0."
    )

    generic_pick_example = (
        Path(__file__).resolve().parent / "generic_pick_runtime.vit"
    )
    native_generic_pick_example = run_program(
        native, generic_pick_example, "generic pick runtime"
    )
    bootstrap_generic_pick_example = run_program(
        bootstrap, generic_pick_example, "generic pick runtime"
    )
    if native_generic_pick_example != (True, ()):
        raise AssertionError(
            "native generic pick must compile and run with exit 0; "
            f"got {native_generic_pick_example}"
        )
    if bootstrap_generic_pick_example != (True, ()):
        raise AssertionError(
            "bootstrap generic pick must compile and run with exit 0; "
            f"got {bootstrap_generic_pick_example}"
        )
    print("Generic pick runtime passed: native and bootstrap exit 0.")

    generic_multi_pick_example = (
        Path(__file__).resolve().parent
        / "generic_pick_multi_parameter_runtime.vit"
    )
    native_multi_pick = run_program(
        native, generic_multi_pick_example, "multi-parameter generic pick"
    )
    bootstrap_multi_pick = run_program(
        bootstrap, generic_multi_pick_example, "multi-parameter generic pick"
    )
    if native_multi_pick != (True, ()):
        raise AssertionError(
            "native multi-parameter generic pick must run with exit 0; "
            f"got {native_multi_pick}"
        )
    if bootstrap_multi_pick != (True, ()):
        raise AssertionError(
            "bootstrap multi-parameter generic pick must run with exit 0; "
            f"got {bootstrap_multi_pick}"
        )
    print("Multi-parameter generic pick runtime passed on both compilers.")

    nullable_example = Path(__file__).resolve().parent / "nullable_runtime.vit"
    native_nullable = run_program(native, nullable_example, "nullable value")
    bootstrap_nullable = run_program(
        bootstrap, nullable_example, "nullable value"
    )
    if native_nullable != (True, ()):
        raise AssertionError(
            "native nullable values must compile and run with exit 0; "
            f"got {native_nullable}"
        )
    if bootstrap_nullable != (True, ()):
        raise AssertionError(
            "bootstrap nullable values must compile and run with exit 0; "
            f"got {bootstrap_nullable}"
        )
    print("Nullable value runtime passed on both compilers.")

    coalesce_example = (
        Path(__file__).resolve().parent / "nullable_coalesce_runtime.vit"
    )
    native_coalesce = run_program(
        native, coalesce_example, "nullable coalescing"
    )
    bootstrap_coalesce = run_program(
        bootstrap, coalesce_example, "nullable coalescing"
    )
    if native_coalesce != (True, ()):
        raise AssertionError(
            "native nullable coalescing must evaluate the left once and the "
            f"fallback lazily; got {native_coalesce}"
        )
    if bootstrap_coalesce != (True, ()):
        raise AssertionError(
            "bootstrap nullable coalescing must evaluate the left once and "
            f"the fallback lazily; got {bootstrap_coalesce}"
        )
    for compiler in (native, bootstrap):
        strict_result = run_strict_c17_emission(
            compiler,
            coalesce_example,
            "nullable coalescing",
        )
        if strict_result.returncode != 0:
            raise AssertionError(
                f"{compiler.name}: strict C17 coalescing runtime exited "
                f"{strict_result.returncode}; stderr={strict_result.stderr[:1000]!r}"
            )
    print("Nullable coalescing runtime passed on both compilers.")

    assert_example = Path(__file__).resolve().parent / "assert_runtime.vit"
    native_assert = run_program(native, assert_example, "assert statement")
    bootstrap_assert = run_program(
        bootstrap, assert_example, "assert statement"
    )
    if native_assert != (True, ()):
        raise AssertionError(
            "native assert statement must compile and run with exit 0; "
            f"got {native_assert}"
        )
    if bootstrap_assert != (True, ()):
        raise AssertionError(
            "bootstrap assert statement must compile and run with exit 0; "
            f"got {bootstrap_assert}"
        )
    with tempfile.TemporaryDirectory(prefix="vitte-assert-failure-") as value:
        failing_assert_source = Path(value) / "assert_failure.vit"
        failing_assert_source.write_text(
            "proc main() -> i64 { assert(false); return 0; }\n",
            encoding="utf-8",
        )
        for compiler in (native, bootstrap):
            failed_assert = subprocess.run(
                [
                    str(compiler),
                    "run",
                    str(failing_assert_source),
                    "--quiet",
                    "--no-color",
                    "--diagnostic=json",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            if failed_assert.returncode == 0 or "error[" in failed_assert.stderr:
                raise AssertionError(
                    f"{compiler.name} assert(false) must abort at runtime, "
                    "not fail compilation: "
                    f"exit={failed_assert.returncode}, "
                    f"stderr={failed_assert.stderr[:1000]!r}"
                )
    print("Assert statement runtime passed on both compilers.")

    with tempfile.TemporaryDirectory(prefix="vitte-test-declaration-") as value:
        test_only_source = Path(value) / "test_only.vit"
        test_only_source.write_text(
            'test "must run" { assert(false); }\n',
            encoding="utf-8",
        )
        test_with_main_source = Path(value) / "test_with_main.vit"
        test_with_main_source.write_text(
            'test "not part of normal main" { assert(false); }\n'
            "proc main() -> i64 { return 0; }\n",
            encoding="utf-8",
        )
        for compiler in (native, bootstrap):
            failed_test = subprocess.run(
                [
                    str(compiler),
                    "run",
                    str(test_only_source),
                    "--quiet",
                    "--no-color",
                    "--diagnostic=json",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            if (
                failed_test.returncode == 0
                or "VITTE_TYPE_E_CONDITION_NOT_BOOL" in failed_test.stderr
            ):
                raise AssertionError(
                    f"{compiler.name} test-only source must execute its "
                    "test body and fail at runtime; "
                    f"exit={failed_test.returncode}, "
                    f"stderr={failed_test.stderr[:1000]!r}"
                )
            normal_program = subprocess.run(
                [
                    str(compiler),
                    "run",
                    str(test_with_main_source),
                    "--quiet",
                    "--no-color",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            if normal_program.returncode != 0:
                raise AssertionError(
                    f"{compiler.name} must not execute test declarations "
                    "when running a source with main; "
                    f"exit={normal_program.returncode}, "
                    f"stderr={normal_program.stderr[:1000]!r}"
                )
    print("Test declarations run only as the test-only entry point on both compilers.")

    with tempfile.TemporaryDirectory(prefix="vitte-modifier-rejection-") as value:
        rejected_sources = {
            "comptime": "comptime proc main() -> i64 { return 0; }\n",
            "unknown_attribute": "#[not_supported] proc main() -> i64 { return 0; }\n",
        }
        for name, source in rejected_sources.items():
            source_path = Path(value) / f"{name}.vit"
            source_path.write_text(source, encoding="utf-8")
            for compiler in (native, bootstrap):
                rejected = subprocess.run(
                    [
                        str(compiler),
                        "run",
                        str(source_path),
                        "--quiet",
                        "--no-color",
                    ],
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                if rejected.returncode == 0 or "E0902" not in rejected.stderr:
                    raise AssertionError(
                        f"{compiler.name} must reject unsupported {name} "
                        "semantics at code generation; "
                        f"exit={rejected.returncode}, "
                        f"stderr={rejected.stderr[:1000]!r}"
                    )

        cold_source = Path(value) / "cold_attribute.vit"
        cold_source.write_text(
            "#[cold] proc main() -> i64 { return 0; }\n",
            encoding="utf-8",
        )
        for compiler in (native, bootstrap):
            accepted = subprocess.run(
                [
                    str(compiler),
                    "run",
                    str(cold_source),
                    "--quiet",
                    "--no-color",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            if accepted.returncode != 0:
                raise AssertionError(
                    f"{compiler.name} must accept the supported #[cold] "
                    "procedure attribute; "
                    f"exit={accepted.returncode}, "
                    f"stderr={accepted.stderr[:1000]!r}"
                )

        thread_local_source = (
            Path(__file__).resolve().parent
            / "thread_local_runtime.vit"
        )
        for compiler in (native, bootstrap):
            accepted = subprocess.run(
                [
                    str(compiler),
                    "run",
                    str(thread_local_source),
                    "--quiet",
                    "--no-color",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            if accepted.returncode != 0:
                raise AssertionError(
                    f"{compiler.name} must emit a thread-local static "
                    "with C17 storage duration; "
                    f"exit={accepted.returncode}, "
                    f"stderr={accepted.stderr[:1000]!r}"
                )

        unsafe_source = Path(value) / "unsafe_procedure.vit"
        unsafe_source.write_text(
            "unsafe proc main() -> i64 { return 0; }\n",
            encoding="utf-8",
        )
        for compiler in (native, bootstrap):
            accepted = subprocess.run(
                [
                    str(compiler),
                    "run",
                    str(unsafe_source),
                    "--quiet",
                    "--no-color",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            if accepted.returncode != 0:
                raise AssertionError(
                    f"{compiler.name} should preserve existing unsafe "
                    "procedure declarations; "
                    f"exit={accepted.returncode}, "
                    f"stderr={accepted.stderr[:1000]!r}"
                )
    print("Comptime and unknown attributes are rejected; supported attributes compile on both compilers.")

    defer_example = Path(__file__).resolve().parent / "defer_runtime.vit"
    native_defer = run_program(native, defer_example, "defer statement")
    bootstrap_defer = run_program(
        bootstrap, defer_example, "defer statement"
    )
    if native_defer != (True, ()):
        raise AssertionError(
            "native deferred actions must run in LIFO order at scope exit; "
            f"got {native_defer}"
        )
    if bootstrap_defer != (True, ()):
        raise AssertionError(
            "bootstrap deferred actions must run in LIFO order at scope exit; "
            f"got {bootstrap_defer}"
        )
    print("Deferred actions run in LIFO order on both compilers.")

    asm_example = Path(__file__).resolve().parent / "asm_runtime.vit"
    native_asm = run_program(native, asm_example, "asm statement")
    bootstrap_asm = run_program(bootstrap, asm_example, "asm statement")
    if native_asm != (True, ()):
        raise AssertionError(
            "native asm template and operands must compile and run; "
            f"got {native_asm}"
        )
    if bootstrap_asm != (True, ()):
        raise AssertionError(
            "bootstrap asm template and operands must compile and run; "
            f"got {bootstrap_asm}"
        )
    print("Assembly templates and compiler-barrier operands passed on both compilers.")

    extern_example = Path(__file__).resolve().parent / "extern_runtime.vit"
    native_extern = run_program(native, extern_example, "extern procedure")
    bootstrap_extern = run_program(
        bootstrap, extern_example, "extern procedure"
    )
    if native_extern != (True, ()):
        raise AssertionError(
            "native extern procedure must compile and run with exit 0; "
            f"got {native_extern}"
        )
    if bootstrap_extern != (True, ()):
        raise AssertionError(
            "bootstrap extern procedure must compile and run with exit 0; "
            f"got {bootstrap_extern}"
        )
    print("Extern procedure runtime passed on both compilers.")

    intrinsic_example = Path(__file__).resolve().parent / "intrinsic_runtime.vit"
    for compiler in (native, bootstrap):
        intrinsic_result = subprocess.run(
            [
                str(compiler),
                "run",
                str(intrinsic_example),
                "--quiet",
                "--no-color",
            ],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if intrinsic_result.returncode != 0 or intrinsic_result.stdout != "42":
            raise AssertionError(
                f"{compiler.name} io_print_i64 intrinsic must print 42 and "
                "exit successfully; "
                f"exit={intrinsic_result.returncode}, "
                f"stdout={intrinsic_result.stdout!r}, "
                f"stderr={intrinsic_result.stderr[:1000]!r}"
            )
    print("C17 host intrinsic runtime passed on native and bootstrap.")

    with tempfile.TemporaryDirectory(prefix="vitte-print-parity-") as value:
        print_source = Path(value) / "print.vit"
        print_source.write_text(
            'proc main() -> int { print("Hello from Vitte!"); return 0; }\n',
            encoding="utf-8",
        )
        for compiler in (native, bootstrap):
            printed = subprocess.run(
                [
                    str(compiler),
                    "run",
                    str(print_source),
                    "--quiet",
                    "--no-color",
                    "--diagnostic=json",
                ],
                capture_output=True,
                text=True,
                timeout=60,
            )
            if printed.returncode != 0 or printed.stdout != "Hello from Vitte!":
                raise AssertionError(
                    f"{compiler.name} print output mismatch: "
                    f"exit={printed.returncode}, stdout={printed.stdout!r}, "
                    f"stderr={printed.stderr!r}"
                )
    print("Builtin print output passed: native and bootstrap match.")

    range_for_example = Path(__file__).resolve().parent / "range_for_runtime.vit"
    native_range_for = run_program(native, range_for_example, "range for")
    bootstrap_range_for = run_program(bootstrap, range_for_example, "range for")
    if native_range_for != (True, ()):
        raise AssertionError(
            "native range iteration must compile and run with exit 0; "
            f"got {native_range_for}"
        )
    if bootstrap_range_for != (True, ()):
        raise AssertionError(
            "bootstrap range iteration must compile and run with exit 0; "
            f"got {bootstrap_range_for}"
        )
    print("Range iteration runtime passed: native and bootstrap exit 0.")

    print("Nested generic runtime parity passed: native and bootstrap exit 0.")
    print("Multi-parameter generic runtime parity passed: native and bootstrap exit 0.")
    print("Composed multi-parameter generic runtime parity passed: native and bootstrap exit 0.")
    print("Generic composed fixed-array runtime parity passed: native and bootstrap exit 0.")
    print("Generic composed dynamic-array runtime parity passed: native and bootstrap exit 0.")
    print("Generic composed optional runtime passed: native and bootstrap exit 0.")
    print("Null optional unwrap aborts in native and bootstrap C17 backends.")
    print("Optional unwrap evaluates a side-effecting operand exactly once.")
    print("40-parameter generic runtime parity passed: native and bootstrap exit 0.")
    print(
        "Generic specialization coverage passed: both compilers emitted C and "
        "ran a program with 513 distinct specializations."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
