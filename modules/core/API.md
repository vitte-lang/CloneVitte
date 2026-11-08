# `core` public API reference

This file is generated from exported declarations under `src/`.
Run `python3 tools/generate_api_reference.py` from `modules/core` after
changing an exported signature. Contracts and error semantics are described
in [`README.md`](README.md). This is a source API index, not a stable ABI
promise.

Source files indexed: 181.
Exported declarations indexed: 1652.

## `core::array::compare`

Source: [`src/array/compare.vit`](src/array/compare.vit) · exports: 5

```vit
export proc array_equal<T>(left: &[T], right: &[T]) -> bool;
export proc array_compare<T>(left: &[T], right: &[T], compare: proc(&T, &T) -> i8) -> i32;
export proc array_not_equal<T>(left: &[T], right: &[T]) -> bool;
export proc array_starts_with<T>(array: &[T], prefix: &[T]) -> bool;
export proc array_ends_with<T>(array: &[T], suffix: &[T]) -> bool;
```

## `core::array::fixed`

Source: [`src/array/fixed.vit`](src/array/fixed.vit) · exports: 12

```vit
export proc fixed_fill<T>(array: &mut [T], value: T);
export proc fixed_copy<T>(source: &[T], destination: &mut [T]) -> usize;
export proc fixed_equal<T>(left: &[T], right: &[T]) -> bool;
export proc fixed_contains<T>(array: &[T], value: T) -> bool;
export proc fixed_index_of<T>(array: &[T], value: T) -> isize;
export proc fixed_reverse<T>(array: &mut [T]);
export proc fixed_rotate_left<T>(array: &mut [T]);
export proc fixed_rotate_right<T>(array: &mut [T]);
export proc fixed_count<T>(array: &[T], value: T) -> usize;
export proc fixed_is_sorted_i64(array: &[i64]) -> bool;
export proc fixed_is_sorted_u64(array: &[u64]) -> bool;
export proc fixed_is_sorted_f64(array: &[f64]) -> bool;
```

## `core::array::init`

Source: [`src/array/init.vit`](src/array/init.vit) · exports: 11

```vit
export proc array_init_fill<T>(
    array: &mut [T],
    value: T
);
export proc array_init_indexed(
    array: &mut [usize]
);
export proc array_init_sequence(
    array: &mut [i64],
    start: i64,
    step: i64
) -> bool;
export proc array_init_reverse_sequence(
    array: &mut [i64],
    start: i64,
    step: i64
) -> bool;
export proc array_init_copy<T>(
    source: &[T],
    destination: &mut [T]
) -> usize;
export proc array_init_range<T>(
    array: &mut [T],
    start: usize,
    end: usize,
    value: T
);
export proc array_init_alternating<T>(
    array: &mut [T],
    first: T,
    second: T
);
export proc array_init_geometric(
    array: &mut [i64],
    start: i64,
    ratio: i64
) -> bool;
export proc array_init_bool(
    array: &mut [bool],
    value: bool
);
export proc array_init_false(
    array: &mut [bool]
);
export proc array_init_true(
    array: &mut [bool]
);
```

## `core::array`

Source: [`src/array/mod.vit`](src/array/mod.vit) · exports: 1

```vit
export proc array_length<T>(value: &[T]) -> usize;
```

## `core::array::repeat`

Source: [`src/array/repeat.vit`](src/array/repeat.vit) · exports: 15

```vit
export proc array_repeat_fill<T>(
    array: &mut [T],
    value: T
);
export proc array_repeat_n<T>(
    array: &mut [T],
    value: T,
    count: usize
) -> usize;
export proc array_repeat_range<T>(
    array: &mut [T],
    start: usize,
    end: usize,
    value: T
) -> usize;
export proc array_repeat_pattern<T>(
    destination: &mut [T],
    pattern: &[T]
) -> usize;
export proc array_repeat_pattern_n<T>(
    destination: &mut [T],
    pattern: &[T],
    repetitions: usize
) -> usize;
export proc array_repeat_alternating<T>(
    array: &mut [T],
    first: T,
    second: T
);
export proc array_repeat_three<T>(
    array: &mut [T],
    first: T,
    second: T,
    third: T
);
export proc array_repeat_step<T>(
    array: &mut [T],
    value: T,
    step: usize
) -> usize;
export proc array_repeat_blocks<T>(
    array: &mut [T],
    value: T,
    block_size: usize
) -> usize;
export proc array_repeat_sequence(
    array: &mut [i64],
    start: i64,
    step: i64,
    cycle_length: usize
) -> bool;
export proc array_repeat_each<T>(
    source: &[T],
    destination: &mut [T],
    repetitions: usize
) -> usize;
export proc array_repeat_reverse_pattern<T>(
    destination: &mut [T],
    pattern: &[T]
) -> usize;
export proc array_repeat_ping_pong<T>(
    destination: &mut [T],
    pattern: &[T]
) -> usize;
export proc array_repeat_fill_count<T>(
    array: &mut [T],
    value: T
) -> usize;
export proc array_repeat_pattern_range<T>(
    destination: &mut [T],
    pattern: &[T],
    start: usize,
    end: usize
) -> usize;
```

## `core::array::zip`

Source: [`src/array/zip.vit`](src/array/zip.vit) · exports: 6

```vit
export proc array_zip_min_length<T, U>(
    left: &[T],
    right: &[U]
) -> usize;
export proc array_zip3_min_length<T, U, V>(
    first: &[T],
    second: &[U],
    third: &[V]
) -> usize;
export proc array_zip4_min_length<T, U, V, W>(
    first: &[T],
    second: &[U],
    third: &[V],
    fourth: &[W]
) -> usize;
export proc array_zip<T, U>(
    left: &[T],
    right: &[U],
    left_output: &mut [T],
    right_output: &mut [U]
) -> usize;
export proc array_zip3<T, U, V>(
    first: &[T],
    second: &[U],
    third: &[V],
    first_output: &mut [T],
    second_output: &mut [U],
    third_output: &mut [V]
) -> usize;
export proc array_zip4<T, U, V, W>(
    first: &[T],
    second: &[U],
    third: &[V],
    fourth: &[W],
    first_output: &mut [T],
    second_output: &mut [U],
    third_output: &mut [V],
    fourth_output: &mut [W]
) -> usize;
```

## `core::cell::cell`

Source: [`src/cell/cell.vit`](src/cell/cell.vit) · exports: 16

```vit
export form Cell<T> {
    value: T,
}
export proc cell_new<T>(value: T) -> Cell[T];
export proc cell_get<T>(cell: &Cell[T]) -> T;
export proc cell_set<T>(
    cell: &mut Cell[T],
    value: T
);
export proc cell_replace<T>(
    cell: &mut Cell[T],
    value: T
) -> T;
export proc cell_swap<T>(
    left: &mut Cell[T],
    right: &mut Cell[T]
);
export proc cell_update<T>(
    cell: &mut Cell[T],
    transform: proc(T) -> T
);
export proc cell_equal<T>(
    left: &Cell[T],
    right: &Cell[T]
) -> bool;
export proc cell_contains<T>(
    cell: &Cell[T],
    value: T
) -> bool;
export proc cell_as_ref<T>(
    cell: &Cell[T]
) -> &T;
export proc cell_as_mut<T>(
    cell: &mut Cell[T]
) -> &mut T;
export proc cell_into_inner<T>(
    cell: Cell[T]
) -> T;
export proc cell_copy_from<T>(
    destination: &mut Cell[T],
    source: &Cell[T]
);
export proc cell_reset<T>(
    cell: &mut Cell[T],
    value: T
);
export proc cell_set_if<T>(
    cell: &mut Cell[T],
    condition: bool,
    value: T
) -> bool;
export proc cell_update_if<T>(
    cell: &mut Cell[T],
    predicate: proc(&T) -> bool,
    transform: proc(T) -> T
) -> bool;
```

## `core::cmp::clamp`

Source: [`src/cmp/clamp.vit`](src/cmp/clamp.vit) · exports: 70

```vit
export proc clamp_i8(value: i8, minimum: i8, maximum: i8) -> i8;
export proc clamp_i16(value: i16, minimum: i16, maximum: i16) -> i16;
export proc clamp_i32(value: i32, minimum: i32, maximum: i32) -> i32;
export proc clamp_i64(value: i64, minimum: i64, maximum: i64) -> i64;
export proc clamp_u8(value: u8, minimum: u8, maximum: u8) -> u8;
export proc clamp_u16(value: u16, minimum: u16, maximum: u16) -> u16;
export proc clamp_u32(value: u32, minimum: u32, maximum: u32) -> u32;
export proc clamp_u64(value: u64, minimum: u64, maximum: u64) -> u64;
export proc clamp_f32(value: f32, minimum: f32, maximum: f32) -> f32;
export proc clamp_f64(value: f64, minimum: f64, maximum: f64) -> f64;
export proc clamp_min_i8(value: i8, minimum: i8) -> i8;
export proc clamp_min_i16(value: i16, minimum: i16) -> i16;
export proc clamp_min_i32(value: i32, minimum: i32) -> i32;
export proc clamp_min_i64(value: i64, minimum: i64) -> i64;
export proc clamp_min_u8(value: u8, minimum: u8) -> u8;
export proc clamp_min_u16(value: u16, minimum: u16) -> u16;
export proc clamp_min_u32(value: u32, minimum: u32) -> u32;
export proc clamp_min_u64(value: u64, minimum: u64) -> u64;
export proc clamp_min_f32(value: f32, minimum: f32) -> f32;
export proc clamp_min_f64(value: f64, minimum: f64) -> f64;
export proc clamp_max_i8(value: i8, maximum: i8) -> i8;
export proc clamp_max_i16(value: i16, maximum: i16) -> i16;
export proc clamp_max_i32(value: i32, maximum: i32) -> i32;
export proc clamp_max_i64(value: i64, maximum: i64) -> i64;
export proc clamp_max_u8(value: u8, maximum: u8) -> u8;
export proc clamp_max_u16(value: u16, maximum: u16) -> u16;
export proc clamp_max_u32(value: u32, maximum: u32) -> u32;
export proc clamp_max_u64(value: u64, maximum: u64) -> u64;
export proc clamp_max_f32(value: f32, maximum: f32) -> f32;
export proc clamp_max_f64(value: f64, maximum: f64) -> f64;
export proc clamp_ordered_i32(value: i32, a: i32, b: i32) -> i32;
export proc clamp_ordered_i64(value: i64, a: i64, b: i64) -> i64;
export proc clamp_ordered_u32(value: u32, a: u32, b: u32) -> u32;
export proc clamp_ordered_u64(value: u64, a: u64, b: u64) -> u64;
export proc clamp_ordered_f32(value: f32, a: f32, b: f32) -> f32;
export proc clamp_ordered_f64(value: f64, a: f64, b: f64) -> f64;
export proc in_range_i32(value: i32, minimum: i32, maximum: i32) -> bool;
export proc in_range_i64(value: i64, minimum: i64, maximum: i64) -> bool;
export proc in_range_u32(value: u32, minimum: u32, maximum: u32) -> bool;
export proc in_range_u64(value: u64, minimum: u64, maximum: u64) -> bool;
export proc in_range_f32(value: f32, minimum: f32, maximum: f32) -> bool;
export proc in_range_f64(value: f64, minimum: f64, maximum: f64) -> bool;
export proc in_range_exclusive_i32(value: i32, minimum: i32, maximum: i32) -> bool;
export proc in_range_exclusive_i64(value: i64, minimum: i64, maximum: i64) -> bool;
export proc in_range_exclusive_u32(value: u32, minimum: u32, maximum: u32) -> bool;
export proc in_range_exclusive_u64(value: u64, minimum: u64, maximum: u64) -> bool;
export proc in_range_exclusive_f32(value: f32, minimum: f32, maximum: f32) -> bool;
export proc in_range_exclusive_f64(value: f64, minimum: f64, maximum: f64) -> bool;
export proc out_of_range_i32(value: i32, minimum: i32, maximum: i32) -> bool;
export proc out_of_range_i64(value: i64, minimum: i64, maximum: i64) -> bool;
export proc out_of_range_u32(value: u32, minimum: u32, maximum: u32) -> bool;
export proc out_of_range_u64(value: u64, minimum: u64, maximum: u64) -> bool;
export proc out_of_range_f32(value: f32, minimum: f32, maximum: f32) -> bool;
export proc out_of_range_f64(value: f64, minimum: f64, maximum: f64) -> bool;
export proc saturate_f32(value: f32) -> f32;
export proc saturate_f64(value: f64) -> f64;
export proc clamp_checked_i8(value: i8, minimum: i8, maximum: i8) -> ?i8;
export proc clamp_checked_i16(value: i16, minimum: i16, maximum: i16) -> ?i16;
export proc clamp_checked_i32(value: i32, minimum: i32, maximum: i32) -> ?i32;
export proc clamp_checked_i64(value: i64, minimum: i64, maximum: i64) -> ?i64;
export proc clamp_checked_u8(value: u8, minimum: u8, maximum: u8) -> ?u8;
export proc clamp_checked_u16(value: u16, minimum: u16, maximum: u16) -> ?u16;
export proc clamp_checked_u32(value: u32, minimum: u32, maximum: u32) -> ?u32;
export proc clamp_checked_u64(value: u64, minimum: u64, maximum: u64) -> ?u64;
export proc clamp_checked_f32(value: f32, minimum: f32, maximum: f32) -> ?f32;
export proc clamp_checked_f64(value: f64, minimum: f64, maximum: f64) -> ?f64;
export proc clamp_symmetric_i32(value: i32, limit: i32) -> i32;
export proc clamp_symmetric_i64(value: i64, limit: i64) -> i64;
export proc clamp_symmetric_f32(value: f32, limit: f32) -> f32;
export proc clamp_symmetric_f64(value: f64, limit: f64) -> f64;
```

## `core::cmp::equal`

Source: [`src/cmp/equal.vit`](src/cmp/equal.vit) · exports: 58

```vit
export proc equal_i8(a: i8, b: i8) -> bool;
export proc equal_i16(a: i16, b: i16) -> bool;
export proc equal_i32(a: i32, b: i32) -> bool;
export proc equal_i64(a: i64, b: i64) -> bool;
export proc equal_u8(a: u8, b: u8) -> bool;
export proc equal_u16(a: u16, b: u16) -> bool;
export proc equal_u32(a: u32, b: u32) -> bool;
export proc equal_u64(a: u64, b: u64) -> bool;
export proc equal_f32(a: f32, b: f32) -> bool;
export proc equal_f64(a: f64, b: f64) -> bool;
export proc equal_bool(a: bool, b: bool) -> bool;
export proc not_equal_i8(a: i8, b: i8) -> bool;
export proc not_equal_i16(a: i16, b: i16) -> bool;
export proc not_equal_i32(a: i32, b: i32) -> bool;
export proc not_equal_i64(a: i64, b: i64) -> bool;
export proc not_equal_u8(a: u8, b: u8) -> bool;
export proc not_equal_u16(a: u16, b: u16) -> bool;
export proc not_equal_u32(a: u32, b: u32) -> bool;
export proc not_equal_u64(a: u64, b: u64) -> bool;
export proc not_equal_f32(a: f32, b: f32) -> bool;
export proc not_equal_f64(a: f64, b: f64) -> bool;
export proc not_equal_bool(a: bool, b: bool) -> bool;
export proc equal3_i32(a: i32, b: i32, c: i32) -> bool;
export proc equal3_i64(a: i64, b: i64, c: i64) -> bool;
export proc equal3_u32(a: u32, b: u32, c: u32) -> bool;
export proc equal3_u64(a: u64, b: u64, c: u64) -> bool;
export proc equal3_f32(a: f32, b: f32, c: f32) -> bool;
export proc equal3_f64(a: f64, b: f64, c: f64) -> bool;
export proc equal3_bool(a: bool, b: bool, c: bool) -> bool;
export proc equal4_i32(a: i32, b: i32, c: i32, d: i32) -> bool;
export proc equal4_i64(a: i64, b: i64, c: i64, d: i64) -> bool;
export proc equal4_u32(a: u32, b: u32, c: u32, d: u32) -> bool;
export proc equal4_u64(a: u64, b: u64, c: u64, d: u64) -> bool;
export proc equal4_f32(a: f32, b: f32, c: f32, d: f32) -> bool;
export proc equal4_f64(a: f64, b: f64, c: f64, d: f64) -> bool;
export proc equal4_bool(a: bool, b: bool, c: bool, d: bool) -> bool;
export proc all_different3_i32(a: i32, b: i32, c: i32) -> bool;
export proc all_different3_i64(a: i64, b: i64, c: i64) -> bool;
export proc all_different3_u32(a: u32, b: u32, c: u32) -> bool;
export proc all_different3_u64(a: u64, b: u64, c: u64) -> bool;
export proc all_different3_f32(a: f32, b: f32, c: f32) -> bool;
export proc all_different3_f64(a: f64, b: f64, c: f64) -> bool;
export proc equal_zero_i32(value: i32) -> bool;
export proc equal_zero_i64(value: i64) -> bool;
export proc equal_zero_u32(value: u32) -> bool;
export proc equal_zero_u64(value: u64) -> bool;
export proc equal_zero_f32(value: f32) -> bool;
export proc equal_zero_f64(value: f64) -> bool;
export proc equal_one_i32(value: i32) -> bool;
export proc equal_one_i64(value: i64) -> bool;
export proc equal_one_u32(value: u32) -> bool;
export proc equal_one_u64(value: u64) -> bool;
export proc equal_one_f32(value: f32) -> bool;
export proc equal_one_f64(value: f64) -> bool;
export proc approx_equal_f32(a: f32, b: f32, epsilon: f32) -> bool;
export proc approx_equal_f64(a: f64, b: f64, epsilon: f64) -> bool;
export proc relative_equal_f32(a: f32, b: f32, epsilon: f32) -> bool;
export proc relative_equal_f64(a: f64, b: f64, epsilon: f64) -> bool;
```

## `core::cmp::max`

Source: [`src/cmp/max.vit`](src/cmp/max.vit) · exports: 44

```vit
export proc max_i8(a: i8, b: i8) -> i8;
export proc max_i16(a: i16, b: i16) -> i16;
export proc max_i32(a: i32, b: i32) -> i32;
export proc max_i64(a: i64, b: i64) -> i64;
export proc max_u8(a: u8, b: u8) -> u8;
export proc max_u16(a: u16, b: u16) -> u16;
export proc max_u32(a: u32, b: u32) -> u32;
export proc max_u64(a: u64, b: u64) -> u64;
export proc max_f32(a: f32, b: f32) -> f32;
export proc max_f64(a: f64, b: f64) -> f64;
export proc max3_i32(a: i32, b: i32, c: i32) -> i32;
export proc max3_i64(a: i64, b: i64, c: i64) -> i64;
export proc max3_u32(a: u32, b: u32, c: u32) -> u32;
export proc max3_u64(a: u64, b: u64, c: u64) -> u64;
export proc max3_f32(a: f32, b: f32, c: f32) -> f32;
export proc max3_f64(a: f64, b: f64, c: f64) -> f64;
export proc max4_i32(a: i32, b: i32, c: i32, d: i32) -> i32;
export proc max4_i64(a: i64, b: i64, c: i64, d: i64) -> i64;
export proc max4_u32(a: u32, b: u32, c: u32, d: u32) -> u32;
export proc max4_u64(a: u64, b: u64, c: u64, d: u64) -> u64;
export proc max4_f32(a: f32, b: f32, c: f32, d: f32) -> f32;
export proc max4_f64(a: f64, b: f64, c: f64, d: f64) -> f64;
export proc max_zero_i32(value: i32) -> i32;
export proc max_zero_i64(value: i64) -> i64;
export proc max_zero_f32(value: f32) -> f32;
export proc max_zero_f64(value: f64) -> f64;
export proc is_max_i32(a: i32, b: i32) -> bool;
export proc is_max_i64(a: i64, b: i64) -> bool;
export proc is_max_u32(a: u32, b: u32) -> bool;
export proc is_max_u64(a: u64, b: u64) -> bool;
export proc is_max_f32(a: f32, b: f32) -> bool;
export proc is_max_f64(a: f64, b: f64) -> bool;
export proc max5_i32(a: i32, b: i32, c: i32, d: i32, e: i32) -> i32;
export proc max5_i64(a: i64, b: i64, c: i64, d: i64, e: i64) -> i64;
export proc max5_f32(a: f32, b: f32, c: f32, d: f32, e: f32) -> f32;
export proc max5_f64(a: f64, b: f64, c: f64, d: f64, e: f64) -> f64;
export proc max6_i32(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32) -> i32;
export proc max6_i64(a: i64, b: i64, c: i64, d: i64, e: i64, f: i64) -> i64;
export proc max6_f32(a: f32, b: f32, c: f32, d: f32, e: f32, f: f32) -> f32;
export proc max6_f64(a: f64, b: f64, c: f64, d: f64, e: f64, f: f64) -> f64;
export proc positive_max_i32(a: i32, b: i32) -> i32;
export proc positive_max_i64(a: i64, b: i64) -> i64;
export proc positive_max_f32(a: f32, b: f32) -> f32;
export proc positive_max_f64(a: f64, b: f64) -> f64;
```

## `core::cmp::min`

Source: [`src/cmp/min.vit`](src/cmp/min.vit) · exports: 44

```vit
export proc min_i8(a: i8, b: i8) -> i8;
export proc min_i16(a: i16, b: i16) -> i16;
export proc min_i32(a: i32, b: i32) -> i32;
export proc min_i64(a: i64, b: i64) -> i64;
export proc min_u8(a: u8, b: u8) -> u8;
export proc min_u16(a: u16, b: u16) -> u16;
export proc min_u32(a: u32, b: u32) -> u32;
export proc min_u64(a: u64, b: u64) -> u64;
export proc min_f32(a: f32, b: f32) -> f32;
export proc min_f64(a: f64, b: f64) -> f64;
export proc min3_i32(a: i32, b: i32, c: i32) -> i32;
export proc min3_i64(a: i64, b: i64, c: i64) -> i64;
export proc min3_u32(a: u32, b: u32, c: u32) -> u32;
export proc min3_u64(a: u64, b: u64, c: u64) -> u64;
export proc min3_f32(a: f32, b: f32, c: f32) -> f32;
export proc min3_f64(a: f64, b: f64, c: f64) -> f64;
export proc min4_i32(a: i32, b: i32, c: i32, d: i32) -> i32;
export proc min4_i64(a: i64, b: i64, c: i64, d: i64) -> i64;
export proc min4_u32(a: u32, b: u32, c: u32, d: u32) -> u32;
export proc min4_u64(a: u64, b: u64, c: u64, d: u64) -> u64;
export proc min4_f32(a: f32, b: f32, c: f32, d: f32) -> f32;
export proc min4_f64(a: f64, b: f64, c: f64, d: f64) -> f64;
export proc min_zero_i32(value: i32) -> i32;
export proc min_zero_i64(value: i64) -> i64;
export proc min_zero_f32(value: f32) -> f32;
export proc min_zero_f64(value: f64) -> f64;
export proc is_min_i32(a: i32, b: i32) -> bool;
export proc is_min_i64(a: i64, b: i64) -> bool;
export proc is_min_u32(a: u32, b: u32) -> bool;
export proc is_min_u64(a: u64, b: u64) -> bool;
export proc is_min_f32(a: f32, b: f32) -> bool;
export proc is_min_f64(a: f64, b: f64) -> bool;
export proc min5_i32(
    a: i32,
    b: i32,
    c: i32,
    d: i32,
    e: i32
) -> i32;
export proc min5_i64(
    a: i64,
    b: i64,
    c: i64,
    d: i64,
    e: i64
) -> i64;
export proc min5_f32(
    a: f32,
    b: f32,
    c: f32,
    d: f32,
    e: f32
) -> f32;
export proc min5_f64(
    a: f64,
    b: f64,
    c: f64,
    d: f64,
    e: f64
) -> f64;
export proc min6_i32(
    a: i32,
    b: i32,
    c: i32,
    d: i32,
    e: i32,
    f: i32
) -> i32;
export proc min6_i64(
    a: i64,
    b: i64,
    c: i64,
    d: i64,
    e: i64,
    f: i64
) -> i64;
export proc min6_f32(
    a: f32,
    b: f32,
    c: f32,
    d: f32,
    e: f32,
    f: f32
) -> f32;
export proc min6_f64(
    a: f64,
    b: f64,
    c: f64,
    d: f64,
    e: f64,
    f: f64
) -> f64;
export proc negative_min_i32(a: i32, b: i32) -> i32;
export proc negative_min_i64(a: i64, b: i64) -> i64;
export proc negative_min_f32(a: f32, b: f32) -> f32;
export proc negative_min_f64(a: f64, b: f64) -> f64;
```

## `core::cmp::ordering`

Source: [`src/cmp/ordering.vit`](src/cmp/ordering.vit) · exports: 50

```vit
export type Ordering = CoreOrdering;
export type PartialOrdering = CorePartialOrdering;
export proc compare_i8(a: i8, b: i8) -> Ordering;
export proc compare_i16(a: i16, b: i16) -> Ordering;
export proc compare_i32(a: i32, b: i32) -> Ordering;
export proc compare_i64(a: i64, b: i64) -> Ordering;
export proc compare_u8(a: u8, b: u8) -> Ordering;
export proc compare_u16(a: u16, b: u16) -> Ordering;
export proc compare_u32(a: u32, b: u32) -> Ordering;
export proc compare_u64(a: u64, b: u64) -> Ordering;
export proc partial_compare_f32(a: f32, b: f32) -> PartialOrdering;
export proc partial_compare_f64(a: f64, b: f64) -> PartialOrdering;
export proc compare_bool(a: bool, b: bool) -> Ordering;
export proc ordering_is_less(ordering: Ordering) -> bool;
export proc ordering_is_equal(ordering: Ordering) -> bool;
export proc ordering_is_greater(ordering: Ordering) -> bool;
export proc ordering_is_less_or_equal(ordering: Ordering) -> bool;
export proc ordering_is_greater_or_equal(ordering: Ordering) -> bool;
export proc partial_is_less(ordering: PartialOrdering) -> bool;
export proc partial_is_equal(ordering: PartialOrdering) -> bool;
export proc partial_is_greater(ordering: PartialOrdering) -> bool;
export proc partial_is_unordered(ordering: PartialOrdering) -> bool;
export proc partial_is_ordered(ordering: PartialOrdering) -> bool;
export proc partial_is_less_or_equal(ordering: PartialOrdering) -> bool;
export proc partial_is_greater_or_equal(ordering: PartialOrdering) -> bool;
export proc ordering_reverse(ordering: Ordering) -> Ordering;
export proc partial_ordering_reverse(
    ordering: PartialOrdering
) -> PartialOrdering;
export proc ordering_to_i32(ordering: Ordering) -> i32;
export proc ordering_to_i64(ordering: Ordering) -> i64;
export proc ordering_from_i32(value: i32) -> Ordering;
export proc ordering_from_i64(value: i64) -> Ordering;
export proc ordering_to_partial(
    ordering: Ordering
) -> PartialOrdering;
export proc less_i32(a: i32, b: i32) -> bool;
export proc less_i64(a: i64, b: i64) -> bool;
export proc less_u32(a: u32, b: u32) -> bool;
export proc less_u64(a: u64, b: u64) -> bool;
export proc greater_i32(a: i32, b: i32) -> bool;
export proc greater_i64(a: i64, b: i64) -> bool;
export proc greater_u32(a: u32, b: u32) -> bool;
export proc greater_u64(a: u64, b: u64) -> bool;
export proc compare_pair_i32(
    a0: i32,
    a1: i32,
    b0: i32,
    b1: i32
) -> Ordering;
export proc compare_pair_i64(
    a0: i64,
    a1: i64,
    b0: i64,
    b1: i64
) -> Ordering;
export proc compare_pair_u32(
    a0: u32,
    a1: u32,
    b0: u32,
    b1: u32
) -> Ordering;
export proc compare_pair_u64(
    a0: u64,
    a1: u64,
    b0: u64,
    b1: u64
) -> Ordering;
export proc compare_triple_i32(
    a0: i32,
    a1: i32,
    a2: i32,
    b0: i32,
    b1: i32,
    b2: i32
) -> Ordering;
export proc compare_triple_i64(
    a0: i64,
    a1: i64,
    a2: i64,
    b0: i64,
    b1: i64,
    b2: i64
) -> Ordering;
export proc ordering_then(
    first: Ordering,
    second: Ordering
) -> Ordering;
export proc partial_ordering_then(
    first: PartialOrdering,
    second: PartialOrdering
) -> PartialOrdering;
export proc ordering_is_opposite(
    a: Ordering,
    b: Ordering
) -> bool;
export proc ordering_is_same(
    a: Ordering,
    b: Ordering
) -> bool;
```

## `core::cmp::partial_order`

Source: [`src/cmp/partial_order.vit`](src/cmp/partial_order.vit) · exports: 43

```vit
export proc partial_cmp_f32(a: f32, b: f32) -> PartialOrdering;
export proc partial_cmp_f64(a: f64, b: f64) -> PartialOrdering;
export proc partial_cmp_i8(a: i8, b: i8) -> PartialOrdering;
export proc partial_cmp_i16(a: i16, b: i16) -> PartialOrdering;
export proc partial_cmp_i32(a: i32, b: i32) -> PartialOrdering;
export proc partial_cmp_i64(a: i64, b: i64) -> PartialOrdering;
export proc partial_cmp_u8(a: u8, b: u8) -> PartialOrdering;
export proc partial_cmp_u16(a: u16, b: u16) -> PartialOrdering;
export proc partial_cmp_u32(a: u32, b: u32) -> PartialOrdering;
export proc partial_cmp_u64(a: u64, b: u64) -> PartialOrdering;
export proc partial_cmp_bool(a: bool, b: bool) -> PartialOrdering;
export proc is_ordered_f32(a: f32, b: f32) -> bool;
export proc is_ordered_f64(a: f64, b: f64) -> bool;
export proc is_unordered_f32(a: f32, b: f32) -> bool;
export proc is_unordered_f64(a: f64, b: f64) -> bool;
export proc is_nan_f32(value: f32) -> bool;
export proc is_nan_f64(value: f64) -> bool;
export proc partial_order_is_less(
    order: PartialOrdering
) -> bool;
export proc partial_order_is_equal(
    order: PartialOrdering
) -> bool;
export proc partial_order_is_greater(
    order: PartialOrdering
) -> bool;
export proc partial_order_is_unordered(
    order: PartialOrdering
) -> bool;
export proc partial_order_is_ordered(
    order: PartialOrdering
) -> bool;
export proc partial_order_is_less_or_equal(
    order: PartialOrdering
) -> bool;
export proc partial_order_is_greater_or_equal(
    order: PartialOrdering
) -> bool;
export proc partial_order_reverse(
    order: PartialOrdering
) -> PartialOrdering;
export proc partial_order_from_ordering(
    order: Ordering
) -> PartialOrdering;
export proc partial_order_to_i32(
    order: PartialOrdering
) -> i32;
export proc partial_order_from_i32(value: i32) -> PartialOrdering;
export proc partial_order_then(
    first: PartialOrdering,
    second: PartialOrdering
) -> PartialOrdering;
export proc partial_cmp_pair_f32(
    a0: f32,
    a1: f32,
    b0: f32,
    b1: f32
) -> PartialOrdering;
export proc partial_cmp_pair_f64(
    a0: f64,
    a1: f64,
    b0: f64,
    b1: f64
) -> PartialOrdering;
export proc partial_cmp_triple_f32(
    a0: f32,
    a1: f32,
    a2: f32,
    b0: f32,
    b1: f32,
    b2: f32
) -> PartialOrdering;
export proc partial_cmp_triple_f64(
    a0: f64,
    a1: f64,
    a2: f64,
    b0: f64,
    b1: f64,
    b2: f64
) -> PartialOrdering;
export proc partial_order_same(
    a: PartialOrdering,
    b: PartialOrdering
) -> bool;
export proc partial_order_opposite(
    a: PartialOrdering,
    b: PartialOrdering
) -> bool;
export proc partial_less_f32(a: f32, b: f32) -> bool;
export proc partial_less_f64(a: f64, b: f64) -> bool;
export proc partial_less_equal_f32(a: f32, b: f32) -> bool;
export proc partial_less_equal_f64(a: f64, b: f64) -> bool;
export proc partial_greater_f32(a: f32, b: f32) -> bool;
export proc partial_greater_f64(a: f64, b: f64) -> bool;
export proc partial_greater_equal_f32(a: f32, b: f32) -> bool;
export proc partial_greater_equal_f64(a: f64, b: f64) -> bool;
```

## `core::cmp::total_order`

Source: [`src/cmp/total_order.vit`](src/cmp/total_order.vit) · exports: 4

```vit
export proc total_cmp_f32(left: f32, right: f32) -> Ordering;
export proc total_cmp_f64(left: f64, right: f64) -> Ordering;
export proc total_eq_f32(left: f32, right: f32) -> bool;
export proc total_eq_f64(left: f64, right: f64) -> bool;
```

## `core::convert::cast`

Source: [`src/convert/cast.vit`](src/convert/cast.vit) · exports: 103

```vit
export pick CastError {
    None,
    Negative,
    Overflow,
    Underflow,
    InvalidValue
}
export form CastU8Result {
    success: bool,
    value: u8,
    error: CastError
}
export form CastU16Result {
    success: bool,
    value: u16,
    error: CastError
}
export form CastU32Result {
    success: bool,
    value: u32,
    error: CastError
}
export form CastU64Result {
    success: bool,
    value: u64,
    error: CastError
}
export form CastI8Result {
    success: bool,
    value: i8,
    error: CastError
}
export form CastI16Result {
    success: bool,
    value: i16,
    error: CastError
}
export form CastI32Result {
    success: bool,
    value: i32,
    error: CastError
}
export form CastI64Result {
    success: bool,
    value: i64,
    error: CastError
}
export proc cast_u8_to_u16(value: u8) -> u16;
export proc cast_u8_to_u32(value: u8) -> u32;
export proc cast_u8_to_u64(value: u8) -> u64;
export proc cast_u16_to_u32(value: u16) -> u32;
export proc cast_u16_to_u64(value: u16) -> u64;
export proc cast_u32_to_u64(value: u32) -> u64;
export proc cast_i8_to_i16(value: i8) -> i16;
export proc cast_i8_to_i32(value: i8) -> i32;
export proc cast_i8_to_i64(value: i8) -> i64;
export proc cast_i16_to_i32(value: i16) -> i32;
export proc cast_i16_to_i64(value: i16) -> i64;
export proc cast_i32_to_i64(value: i32) -> i64;
export proc cast_u8_to_i16(value: u8) -> i16;
export proc cast_u8_to_i32(value: u8) -> i32;
export proc cast_u8_to_i64(value: u8) -> i64;
export proc cast_u16_to_i32(value: u16) -> i32;
export proc cast_u16_to_i64(value: u16) -> i64;
export proc cast_u32_to_i64(value: u32) -> i64;
export proc checked_u16_to_u8(value: u16) -> CastU8Result;
export proc checked_u32_to_u8(value: u32) -> CastU8Result;
export proc checked_u64_to_u8(value: u64) -> CastU8Result;
export proc checked_u32_to_u16(value: u32) -> CastU16Result;
export proc checked_u64_to_u16(value: u64) -> CastU16Result;
export proc checked_u64_to_u32(value: u64) -> CastU32Result;
export proc checked_u32_to_i8(value: u32) -> CastI8Result;
export proc checked_i32_to_i8(value: i32) -> CastI8Result;
export proc checked_i64_to_i8(value: i64) -> CastI8Result;
export proc checked_i64_to_i16(value: i64) -> CastI16Result;
export proc checked_i64_to_u8(value: i64) -> CastU8Result;
export proc checked_i64_to_u16(value: i64) -> CastU16Result;
export proc checked_u64_to_i64(value: u64) -> CastI64Result;
export proc checked_u16_to_i8(value: u16) -> CastI8Result;
export proc checked_u8_to_i8(value: u8) -> CastI8Result;
export proc checked_u16_to_i16(value: u16) -> CastI16Result;
export proc checked_u32_to_i16(value: u32) -> CastI16Result;
export proc checked_u32_to_i32(value: u32) -> CastI32Result;
export proc checked_u64_to_i8(value: u64) -> CastI8Result;
export proc checked_u64_to_i16(value: u64) -> CastI16Result;
export proc checked_u64_to_i32(value: u64) -> CastI32Result;
export proc checked_i64_to_u64(value: i64) -> CastU64Result;
export proc checked_i8_to_u8(value: i8) -> CastU8Result;
export proc checked_i8_to_u16(value: i8) -> CastU16Result;
export proc checked_i8_to_u32(value: i8) -> CastU32Result;
export proc checked_i8_to_u64(value: i8) -> CastU64Result;
export proc checked_i16_to_u8(value: i16) -> CastU8Result;
export proc checked_i16_to_u16(value: i16) -> CastU16Result;
export proc checked_i16_to_u32(value: i16) -> CastU32Result;
export proc checked_i16_to_u64(value: i16) -> CastU64Result;
export proc checked_i32_to_u32(value: i32) -> CastU32Result;
export proc checked_i32_to_u64(value: i32) -> CastU64Result;
export proc checked_f64_to_i64(value: f64) -> CastI64Result;
export proc checked_f64_to_u64(value: f64) -> CastU64Result;
export proc checked_f32_to_i64(value: f32) -> CastI64Result;
export proc checked_f32_to_u64(value: f32) -> CastU64Result;
export proc checked_i16_to_i8(value: i16) -> CastI8Result;
export proc checked_i32_to_i16(value: i32) -> CastI16Result;
export proc checked_i64_to_i32(value: i64) -> CastI32Result;
export proc checked_i32_to_u8(value: i32) -> CastU8Result;
export proc checked_i32_to_u16(value: i32) -> CastU16Result;
export proc checked_i64_to_u32(value: i64) -> CastU32Result;
export proc saturating_u16_to_u8(value: u16) -> u8;
export proc saturating_u32_to_u8(value: u32) -> u8;
export proc saturating_u64_to_u8(value: u64) -> u8;
export proc saturating_u32_to_u16(value: u32) -> u16;
export proc saturating_u64_to_u16(value: u64) -> u16;
export proc saturating_u64_to_u32(value: u64) -> u32;
export proc saturating_u32_to_i8(value: u32) -> i8;
export proc saturating_i32_to_i8(value: i32) -> i8;
export proc saturating_i64_to_i8(value: i64) -> i8;
export proc saturating_i64_to_i16(value: i64) -> i16;
export proc saturating_i64_to_u8(value: i64) -> u8;
export proc saturating_i64_to_u16(value: i64) -> u16;
export proc saturating_i64_to_u64(value: i64) -> u64;
export proc saturating_i16_to_i8(value: i16) -> i8;
export proc saturating_i32_to_i16(value: i32) -> i16;
export proc saturating_i64_to_i32(value: i64) -> i32;
export proc saturating_i32_to_u8(value: i32) -> u8;
export proc saturating_i32_to_u16(value: i32) -> u16;
export proc saturating_i64_to_u32(value: i64) -> u32;
export proc cast_bool_to_u8(value: bool) -> u8;
export proc cast_bool_to_i32(value: bool) -> i32;
export proc cast_bool_to_u64(value: bool) -> u64;
export proc cast_i32_to_bool(value: i32) -> bool;
export proc cast_i64_to_bool(value: i64) -> bool;
export proc cast_u32_to_bool(value: u32) -> bool;
export proc cast_u64_to_bool(value: u64) -> bool;
export proc cast_f32_to_f64(value: f32) -> f64;
export proc cast_f64_to_f32(value: f64) -> f32;
export proc cast_i32_to_f64(value: i32) -> f64;
export proc cast_u32_to_f64(value: u32) -> f64;
export proc cast_i64_to_f64(value: i64) -> f64;
export proc cast_u64_to_f64(value: u64) -> f64;
export proc cast_i32_to_f32(value: i32) -> f32;
export proc cast_u32_to_f32(value: u32) -> f32;
```

## `core::convert::format`

Source: [`src/convert/format.vit`](src/convert/format.vit) · exports: 55

```vit
export pick FormatError {
    None,
    NullBuffer,
    BufferTooSmall,
    InvalidBase,
    InvalidArgument,
    Unsupported
}
export form FormatResult {
    success: bool,
    length: usize,
    error: FormatError
}
export proc format_success(length: usize) -> FormatResult;
export proc format_failure(error: FormatError) -> FormatResult;
export proc format_digit_lower(digit: u8) -> u8;
export proc format_digit_upper(digit: u8) -> u8;
export proc format_buffer_valid(
    buffer: *mut u8,
    capacity: usize
) -> bool;
export proc format_write_byte(
    buffer: *mut u8,
    capacity: usize,
    index: usize,
    byte: u8
) -> bool;
export proc format_u64_decimal_length(value: u64) -> usize;
export proc format_u64(
    value: u64,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_i64_decimal_length(value: i64) -> usize;
export proc format_i64(
    value: i64,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_u8(
    value: u8,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_u16(
    value: u16,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_u32(
    value: u32,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_i8(
    value: i8,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_i16(
    value: i16,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_i32(
    value: i32,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_bool(
    value: bool,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_u64_base_length(
    value: u64,
    base: u8
) -> usize;
export proc format_u64_base(
    value: u64,
    base: u8,
    uppercase: bool,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_i64_base(
    value: i64,
    base: u8,
    uppercase: bool,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_i64_binary(value: i64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_i64_octal(value: i64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_i64_hex(value: i64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_i32_base(value: i32, base: u8, uppercase: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_i16_base(value: i16, base: u8, uppercase: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_i8_base(value: i8, base: u8, uppercase: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_u32_base(value: u32, base: u8, uppercase: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_u16_base(value: u16, base: u8, uppercase: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_u8_base(value: u8, base: u8, uppercase: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_hex_u64(
    value: u64,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_hex_upper_u64(
    value: u64,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_hex_u32(
    value: u32,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_hex_u16(
    value: u16,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_hex_u8(
    value: u8,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_binary_u64(
    value: u64,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_binary_u32(
    value: u32,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_binary_u16(
    value: u16,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_binary_u8(
    value: u8,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_octal_u64(
    value: u64,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_octal_u32(
    value: u32,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_octal_u16(value: u16, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_octal_u8(value: u8, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_hex_upper_u32(value: u32, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_hex_upper_u16(value: u16, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_hex_upper_u8(value: u8, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_hex_fixed_u64(
    value: u64,
    width: usize,
    uppercase: bool,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_ascii(
    character: u8,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_char(
    character: char,
    buffer: *mut u8,
    capacity: usize
) -> FormatResult;
export proc format_bytes(bytes: &[u8], buffer: *mut u8, capacity: usize) -> FormatResult;
export proc format_bool_length(value: bool) -> usize;
export proc format_hex_u64_length(value: u64) -> usize;
export proc format_binary_u64_length(value: u64) -> usize;
export proc format_octal_u64_length(value: u64) -> usize;
```

## `core::convert::from`

Source: [`src/convert/from.vit`](src/convert/from.vit) · exports: 50

```vit
export proc from_u8_to_u16(value: u8) -> u16;
export proc from_u8_to_u32(value: u8) -> u32;
export proc from_u8_to_u64(value: u8) -> u64;
export proc from_u16_to_u32(value: u16) -> u32;
export proc from_u16_to_u64(value: u16) -> u64;
export proc from_u32_to_u64(value: u32) -> u64;
export proc from_i8_to_i16(value: i8) -> i16;
export proc from_i8_to_i32(value: i8) -> i32;
export proc from_i8_to_i64(value: i8) -> i64;
export proc from_i16_to_i32(value: i16) -> i32;
export proc from_i16_to_i64(value: i16) -> i64;
export proc from_i32_to_i64(value: i32) -> i64;
export proc from_u8_to_i16(value: u8) -> i16;
export proc from_u8_to_i32(value: u8) -> i32;
export proc from_u8_to_i64(value: u8) -> i64;
export proc from_u16_to_i32(value: u16) -> i32;
export proc from_u16_to_i64(value: u16) -> i64;
export proc from_u32_to_i64(value: u32) -> i64;
export proc from_bool_to_u8(value: bool) -> u8;
export proc from_bool_to_u16(value: bool) -> u16;
export proc from_bool_to_u32(value: bool) -> u32;
export proc from_bool_to_u64(value: bool) -> u64;
export proc from_bool_to_i8(value: bool) -> i8;
export proc from_bool_to_i16(value: bool) -> i16;
export proc from_bool_to_i32(value: bool) -> i32;
export proc from_bool_to_i64(value: bool) -> i64;
export proc from_bool_to_f32(value: bool) -> f32;
export proc from_bool_to_f64(value: bool) -> f64;
export proc from_i8_to_f32(value: i8) -> f32;
export proc from_u8_to_f32(value: u8) -> f32;
export proc from_i16_to_f32(value: i16) -> f32;
export proc from_u16_to_f32(value: u16) -> f32;
export proc from_i8_to_f64(value: i8) -> f64;
export proc from_u8_to_f64(value: u8) -> f64;
export proc from_i16_to_f64(value: i16) -> f64;
export proc from_u16_to_f64(value: u16) -> f64;
export proc from_i32_to_f64(value: i32) -> f64;
export proc from_u32_to_f64(value: u32) -> f64;
export proc from_f32_to_f64(value: f32) -> f64;
export proc from_i8_to_i8(value: i8) -> i8;
export proc from_i16_to_i16(value: i16) -> i16;
export proc from_i32_to_i32(value: i32) -> i32;
export proc from_i64_to_i64(value: i64) -> i64;
export proc from_u8_to_u8(value: u8) -> u8;
export proc from_u16_to_u16(value: u16) -> u16;
export proc from_u32_to_u32(value: u32) -> u32;
export proc from_u64_to_u64(value: u64) -> u64;
export proc from_f32_to_f32(value: f32) -> f32;
export proc from_f64_to_f64(value: f64) -> f64;
export proc from_bool_to_bool(value: bool) -> bool;
```

## `core::convert::into`

Source: [`src/convert/into.vit`](src/convert/into.vit) · exports: 50

```vit
export proc into_u16_from_u8(value: u8) -> u16;
export proc into_u32_from_u8(value: u8) -> u32;
export proc into_u64_from_u8(value: u8) -> u64;
export proc into_u32_from_u16(value: u16) -> u32;
export proc into_u64_from_u16(value: u16) -> u64;
export proc into_u64_from_u32(value: u32) -> u64;
export proc into_i16_from_i8(value: i8) -> i16;
export proc into_i32_from_i8(value: i8) -> i32;
export proc into_i64_from_i8(value: i8) -> i64;
export proc into_i32_from_i16(value: i16) -> i32;
export proc into_i64_from_i16(value: i16) -> i64;
export proc into_i64_from_i32(value: i32) -> i64;
export proc into_i16_from_u8(value: u8) -> i16;
export proc into_i32_from_u8(value: u8) -> i32;
export proc into_i64_from_u8(value: u8) -> i64;
export proc into_i32_from_u16(value: u16) -> i32;
export proc into_i64_from_u16(value: u16) -> i64;
export proc into_i64_from_u32(value: u32) -> i64;
export proc into_u8_from_bool(value: bool) -> u8;
export proc into_u16_from_bool(value: bool) -> u16;
export proc into_u32_from_bool(value: bool) -> u32;
export proc into_u64_from_bool(value: bool) -> u64;
export proc into_i8_from_bool(value: bool) -> i8;
export proc into_i16_from_bool(value: bool) -> i16;
export proc into_i32_from_bool(value: bool) -> i32;
export proc into_i64_from_bool(value: bool) -> i64;
export proc into_f32_from_bool(value: bool) -> f32;
export proc into_f64_from_bool(value: bool) -> f64;
export proc into_f32_from_i8(value: i8) -> f32;
export proc into_f32_from_u8(value: u8) -> f32;
export proc into_f32_from_i16(value: i16) -> f32;
export proc into_f32_from_u16(value: u16) -> f32;
export proc into_f64_from_i8(value: i8) -> f64;
export proc into_f64_from_u8(value: u8) -> f64;
export proc into_f64_from_i16(value: i16) -> f64;
export proc into_f64_from_u16(value: u16) -> f64;
export proc into_f64_from_i32(value: i32) -> f64;
export proc into_f64_from_u32(value: u32) -> f64;
export proc into_f64_from_f32(value: f32) -> f64;
export proc into_i8_from_i8(value: i8) -> i8;
export proc into_i16_from_i16(value: i16) -> i16;
export proc into_i32_from_i32(value: i32) -> i32;
export proc into_i64_from_i64(value: i64) -> i64;
export proc into_u8_from_u8(value: u8) -> u8;
export proc into_u16_from_u16(value: u16) -> u16;
export proc into_u32_from_u32(value: u32) -> u32;
export proc into_u64_from_u64(value: u64) -> u64;
export proc into_f32_from_f32(value: f32) -> f32;
export proc into_f64_from_f64(value: f64) -> f64;
export proc into_bool_from_bool(value: bool) -> bool;
```

## `core::convert::parse`

Source: [`src/convert/parse.vit`](src/convert/parse.vit) · exports: 19

```vit
export pick ParseError {
    None,
    Empty,
    InvalidBase,
    InvalidDigit,
    Overflow,
}
export form ParseU64Result {
    success: bool,
    value: u64,
    error: ParseError,
    error_index: usize,
}
export form ParseI64Result {
    success: bool,
    value: i64,
    error: ParseError,
    error_index: usize,
}
export form ParseBoolResult {
    success: bool,
    value: bool,
    error: ParseError,
    error_index: usize,
}
export form ParseU8Result { success: bool, value: u8, error: ParseError, error_index: usize }
export form ParseU16Result { success: bool, value: u16, error: ParseError, error_index: usize }
export form ParseU32Result { success: bool, value: u32, error: ParseError, error_index: usize }
export form ParseI8Result { success: bool, value: i8, error: ParseError, error_index: usize }
export form ParseI16Result { success: bool, value: i16, error: ParseError, error_index: usize }
export form ParseI32Result { success: bool, value: i32, error: ParseError, error_index: usize }
export proc parse_u64(text: str, base: u32) -> ParseU64Result;
export proc parse_i64(text: str, base: u32) -> ParseI64Result;
export proc parse_bool(text: str) -> ParseBoolResult;
export proc parse_u8(text: str, base: u32) -> ParseU8Result;
export proc parse_u16(text: str, base: u32) -> ParseU16Result;
export proc parse_u32(text: str, base: u32) -> ParseU32Result;
export proc parse_i8(text: str, base: u32) -> ParseI8Result;
export proc parse_i16(text: str, base: u32) -> ParseI16Result;
export proc parse_i32(text: str, base: u32) -> ParseI32Result;
```

## `core::convert::try_from`

Source: [`src/convert/try_from.vit`](src/convert/try_from.vit) · exports: 42

```vit
export proc try_from_u16_to_u8(value: u16) -> CastU8Result;
export proc try_from_u32_to_u8(value: u32) -> CastU8Result;
export proc try_from_u64_to_u8(value: u64) -> CastU8Result;
export proc try_from_u32_to_u16(value: u32) -> CastU16Result;
export proc try_from_u64_to_u16(value: u64) -> CastU16Result;
export proc try_from_u64_to_u32(value: u64) -> CastU32Result;
export proc try_from_i16_to_i8(value: i16) -> CastI8Result;
export proc try_from_i32_to_i16(value: i32) -> CastI16Result;
export proc try_from_i64_to_i32(value: i64) -> CastI32Result;
export proc try_from_i32_to_u8(value: i32) -> CastU8Result;
export proc try_from_i32_to_u16(value: i32) -> CastU16Result;
export proc try_from_i64_to_u32(value: i64) -> CastU32Result;
export proc try_from_u32_to_i8(value: u32) -> CastI8Result;
export proc try_from_i32_to_i8(value: i32) -> CastI8Result;
export proc try_from_i64_to_i8(value: i64) -> CastI8Result;
export proc try_from_i64_to_i16(value: i64) -> CastI16Result;
export proc try_from_i64_to_u8(value: i64) -> CastU8Result;
export proc try_from_i64_to_u16(value: i64) -> CastU16Result;
export proc try_from_u64_to_i64(value: u64) -> CastI64Result;
export proc try_from_i64_to_u64(value: i64) -> CastU64Result;
export proc try_from_u16_to_i8(value: u16) -> CastI8Result;
export proc try_from_u8_to_i8(value: u8) -> CastI8Result;
export proc try_from_u16_to_i16(value: u16) -> CastI16Result;
export proc try_from_u32_to_i16(value: u32) -> CastI16Result;
export proc try_from_u32_to_i32(value: u32) -> CastI32Result;
export proc try_from_u64_to_i8(value: u64) -> CastI8Result;
export proc try_from_u64_to_i16(value: u64) -> CastI16Result;
export proc try_from_u64_to_i32(value: u64) -> CastI32Result;
export proc try_from_i8_to_u8(value: i8) -> CastU8Result;
export proc try_from_i8_to_u16(value: i8) -> CastU16Result;
export proc try_from_i8_to_u32(value: i8) -> CastU32Result;
export proc try_from_i8_to_u64(value: i8) -> CastU64Result;
export proc try_from_i16_to_u8(value: i16) -> CastU8Result;
export proc try_from_i16_to_u16(value: i16) -> CastU16Result;
export proc try_from_i16_to_u32(value: i16) -> CastU32Result;
export proc try_from_i16_to_u64(value: i16) -> CastU64Result;
export proc try_from_i32_to_u32(value: i32) -> CastU32Result;
export proc try_from_i32_to_u64(value: i32) -> CastU64Result;
export proc try_from_f32_to_i64(value: f32) -> CastI64Result;
export proc try_from_f32_to_u64(value: f32) -> CastU64Result;
export proc try_from_f64_to_i64(value: f64) -> CastI64Result;
export proc try_from_f64_to_u64(value: f64) -> CastU64Result;
```

## `core::convert::try_into`

Source: [`src/convert/try_into.vit`](src/convert/try_into.vit) · exports: 42

```vit
export proc try_into_u8_from_u16(value: u16) -> CastU8Result;
export proc try_into_u8_from_u32(value: u32) -> CastU8Result;
export proc try_into_u8_from_u64(value: u64) -> CastU8Result;
export proc try_into_u16_from_u32(value: u32) -> CastU16Result;
export proc try_into_u16_from_u64(value: u64) -> CastU16Result;
export proc try_into_u32_from_u64(value: u64) -> CastU32Result;
export proc try_into_i8_from_i16(value: i16) -> CastI8Result;
export proc try_into_i16_from_i32(value: i32) -> CastI16Result;
export proc try_into_i32_from_i64(value: i64) -> CastI32Result;
export proc try_into_u8_from_i32(value: i32) -> CastU8Result;
export proc try_into_u16_from_i32(value: i32) -> CastU16Result;
export proc try_into_u32_from_i64(value: i64) -> CastU32Result;
export proc try_into_i8_from_u32(value: u32) -> CastI8Result;
export proc try_into_i8_from_i32(value: i32) -> CastI8Result;
export proc try_into_i8_from_i64(value: i64) -> CastI8Result;
export proc try_into_i16_from_i64(value: i64) -> CastI16Result;
export proc try_into_u8_from_i64(value: i64) -> CastU8Result;
export proc try_into_u16_from_i64(value: i64) -> CastU16Result;
export proc try_into_i64_from_u64(value: u64) -> CastI64Result;
export proc try_into_u64_from_i64(value: i64) -> CastU64Result;
export proc try_into_i8_from_u16(value: u16) -> CastI8Result;
export proc try_into_i8_from_u8(value: u8) -> CastI8Result;
export proc try_into_i16_from_u16(value: u16) -> CastI16Result;
export proc try_into_i16_from_u32(value: u32) -> CastI16Result;
export proc try_into_i32_from_u32(value: u32) -> CastI32Result;
export proc try_into_i8_from_u64(value: u64) -> CastI8Result;
export proc try_into_i16_from_u64(value: u64) -> CastI16Result;
export proc try_into_i32_from_u64(value: u64) -> CastI32Result;
export proc try_into_u8_from_i8(value: i8) -> CastU8Result;
export proc try_into_u16_from_i8(value: i8) -> CastU16Result;
export proc try_into_u32_from_i8(value: i8) -> CastU32Result;
export proc try_into_u64_from_i8(value: i8) -> CastU64Result;
export proc try_into_u8_from_i16(value: i16) -> CastU8Result;
export proc try_into_u16_from_i16(value: i16) -> CastU16Result;
export proc try_into_u32_from_i16(value: i16) -> CastU32Result;
export proc try_into_u64_from_i16(value: i16) -> CastU64Result;
export proc try_into_u32_from_i32(value: i32) -> CastU32Result;
export proc try_into_u64_from_i32(value: i32) -> CastU64Result;
export proc try_into_i64_from_f32(value: f32) -> CastI64Result;
export proc try_into_u64_from_f32(value: f32) -> CastU64Result;
export proc try_into_i64_from_f64(value: f64) -> CastI64Result;
export proc try_into_u64_from_f64(value: f64) -> CastU64Result;
```

## `core`

Source: [`src/core.vit`](src/core.vit) · exports: 3

```vit
export const CORE_VERSION_MAJOR: u32 = 0;
export const CORE_VERSION_MINOR: u32 = 1;
export const CORE_VERSION_PATCH: u32 = 0;
```

## `core::error::assert`

Source: [`src/error/assert.vit`](src/error/assert.vit) · exports: 3

```vit
export form AssertionResult {
    success: bool,
    code: u32,
    location: usize,
}
export proc assertion_check(condition: bool, code: u32, location: usize) -> AssertionResult;
export proc assertion_equal<T>(left: T, right: T, code: u32, location: usize) -> AssertionResult;
```

## `core::error::backtrace`

Source: [`src/error/backtrace.vit`](src/error/backtrace.vit) · exports: 4

```vit
export form BacktraceFrame {
    function: u64,
    location: usize,
}
export form Backtrace {
    frames: &[BacktraceFrame],
}
export proc backtrace_depth(value: &Backtrace) -> usize;
export proc backtrace_frame(value: &Backtrace, index: usize) -> ?&BacktraceFrame;
```

## `core::error::error`

Source: [`src/error/error.vit`](src/error/error.vit) · exports: 8

```vit
export form Error {
    code: u32,
    message: str,
    location: usize,
}
export proc error_new(code: u32, message: str, location: usize) -> Error;
export proc error_is_valid(error: &Error) -> bool;
export proc error_has_message(error: &Error) -> bool;
export proc error_code(error: &Error) -> u32;
export proc error_location(error: &Error) -> usize;
export proc error_same_code(left: &Error, right: &Error) -> bool;
export proc error_fingerprint(error: &Error) -> u64;
```

## `core::error::located`

Source: [`src/error/located.vit`](src/error/located.vit) · exports: 5

```vit
export form LocatedError {
    cause: Error,
    span: SourceSpan,
}
export proc located_error_new(cause: Error, span: SourceSpan) -> ?LocatedError;
export proc located_error_is_valid(value: &LocatedError) -> bool;
export proc located_error_cause(value: &LocatedError) -> &Error;
export proc located_error_span(value: &LocatedError) -> &SourceSpan;
```

## `core::error::location`

Source: [`src/error/location.vit`](src/error/location.vit) · exports: 9

```vit
export form SourceLocation {
    file: u64,
    offset: usize,
    line: usize,
    column: usize,
}
export form SourceSpan {
    start: SourceLocation,
    end: SourceLocation,
}
export proc source_location(file: u64, offset: usize, line: usize, column: usize) -> SourceLocation;
export proc source_location_is_valid(value: &SourceLocation) -> bool;
export proc source_span_is_valid(value: &SourceSpan) -> bool;
export proc source_span_length(value: &SourceSpan) -> usize;
export proc source_span_is_empty(value: &SourceSpan) -> bool;
export proc source_span_contains_offset(value: &SourceSpan, file: u64, offset: usize) -> bool;
export proc source_span_covers_offset(value: &SourceSpan, file: u64, offset: usize) -> bool;
```

## `core::error::panic`

Source: [`src/error/panic.vit`](src/error/panic.vit) · exports: 5

```vit
export form PanicInfo {
    message: str,
    file: u64,
    line: usize,
    column: usize,
}
export proc panic_info(message: str, file: u64, line: usize, column: usize) -> PanicInfo;
export proc panic_info_is_valid(value: &PanicInfo) -> bool;
export proc panic_has_location(value: &PanicInfo) -> bool;
export proc panic(message: str) -> never;
```

## `core::error::result`

Source: [`src/error/result.vit`](src/error/result.vit) · exports: 6

```vit
export proc error_result_is_ok<T, E>(value: &Result[T, E]) -> bool;
export proc error_result_is_err<T, E>(value: &Result[T, E]) -> bool;
export proc error_result_value<T, E>(value: &Result[T, E]) -> ?T;
export proc error_result_error<T, E>(value: &Result[T, E]) -> ?E;
export proc error_result_cause<T, E>(value: &Result[T, E]) -> ?E;
export proc error_result_is_valid<T, E>(value: &Result[T, E]) -> bool;
```

## `core::fmt::arguments`

Source: [`src/fmt/arguments.vit`](src/fmt/arguments.vit) · exports: 7

```vit
export pick FormatArgKind { Signed, Unsigned, Boolean, Character, String }
export form FormatArg {
    kind: FormatArgKind,
    signed_value: i64,
    unsigned_value: u64,
    bool_value: bool,
    char_value: char,
    string_value: str,
}
export proc format_arg_i64(value: i64) -> FormatArg;
export proc format_arg_u64(value: u64) -> FormatArg;
export proc format_arg_bool(value: bool) -> FormatArg;
export proc format_arg_char(value: char) -> FormatArg;
export proc format_arg_str(value: str) -> FormatArg;
```

## `core::fmt::debug`

Source: [`src/fmt/debug.vit`](src/fmt/debug.vit) · exports: 9

```vit
export proc debug_bool(value: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc debug_i64(value: i64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc debug_u64(value: u64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc debug_i8(value: i8, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc debug_i16(value: i16, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc debug_i32(value: i32, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc debug_u8(value: u8, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc debug_u16(value: u16, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc debug_u32(value: u32, buffer: *mut u8, capacity: usize) -> FormatResult;
```

## `core::fmt::display`

Source: [`src/fmt/display.vit`](src/fmt/display.vit) · exports: 10

```vit
export proc display_bool(value: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_i64(value: i64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_u64(value: u64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_i8(value: i8, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_i16(value: i16, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_i32(value: i32, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_u8(value: u8, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_u16(value: u16, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_u32(value: u32, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc display_char(value: char, buffer: *mut u8, capacity: usize) -> FormatResult;
```

## `core::fmt::float`

Source: [`src/fmt/float.vit`](src/fmt/float.vit) · exports: 2

```vit
export proc fmt_f64_fixed(value: f64, precision: usize, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_f32_fixed(value: f32, precision: usize, buffer: *mut u8, capacity: usize) -> FormatResult;
```

## `core::fmt::formatter`

Source: [`src/fmt/formatter.vit`](src/fmt/formatter.vit) · exports: 12

```vit
export pick FormatterError { None, NullBuffer, Full, InvalidCharacter, InvalidArgument }
export form Formatter {
    buffer: *mut u8,
    capacity: usize,
    length: usize,
    error: FormatterError,
}
export proc formatter(buffer: *mut u8, capacity: usize) -> Formatter;
export proc formatter_write_byte(value: &mut Formatter, byte: u8) -> bool;
export proc formatter_remaining_capacity(value: &Formatter) -> usize;
export proc formatter_write_bytes(value: &mut Formatter, bytes: &[u8]) -> bool;
export proc formatter_write_str(value: &mut Formatter, text: str) -> bool;
export proc formatter_write_char(value: &mut Formatter, character: char) -> bool;
export proc formatter_write_bool(value: &mut Formatter, flag: bool) -> bool;
export proc formatter_write_u64(value: &mut Formatter, number: u64) -> bool;
export proc formatter_write_i64(value: &mut Formatter, number: i64) -> bool;
export proc formatter_write_arg(value: &mut Formatter, argument: &FormatArg) -> bool;
```

## `core::fmt::integer`

Source: [`src/fmt/integer.vit`](src/fmt/integer.vit) · exports: 10

```vit
export proc fmt_i64(value: i64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_u64(value: u64, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_u64_radix(value: u64, radix: u8, uppercase: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_i64_radix(value: i64, radix: u8, uppercase: bool, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_i8(value: i8, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_i16(value: i16, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_i32(value: i32, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_u8(value: u8, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_u16(value: u16, buffer: *mut u8, capacity: usize) -> FormatResult;
export proc fmt_u32(value: u32, buffer: *mut u8, capacity: usize) -> FormatResult;
```

## `core::fmt::padding`

Source: [`src/fmt/padding.vit`](src/fmt/padding.vit) · exports: 2

```vit
export proc fmt_pad_left(buffer: *mut u8, capacity: usize, length: usize, width: usize, fill: u8) -> ?usize;
export proc fmt_pad_right(buffer: *mut u8, capacity: usize, length: usize, width: usize, fill: u8) -> ?usize;
```

## `core::hash::combine`

Source: [`src/hash/combine.vit`](src/hash/combine.vit) · exports: 3

```vit
export proc hash_combine(seed: u64, value: u64) -> u64;
export proc hash_combine_pair(first: u64, second: u64) -> u64;
export proc hash_combine3(first: u64, second: u64, third: u64) -> u64;
```

## `core::hash::fnv`

Source: [`src/hash/fnv.vit`](src/hash/fnv.vit) · exports: 4

```vit
export const FNV64_OFFSET_BASIS: u64 = 14695981039346656037;
export const FNV64_PRIME: u64 = 1099511628211;
export proc fnv1a64(seed: u64, bytes: &[u8]) -> u64;
export proc fnv1a64_bytes(bytes: &[u8]) -> u64;
```

## `core::hash::hasher`

Source: [`src/hash/hasher.vit`](src/hash/hasher.vit) · exports: 10

```vit
export form Hasher {
    state: u64,
    length: u64,
}
export proc hasher_new() -> Hasher;
export proc hasher_write_byte(hasher: &mut Hasher, value: u8);
export proc hasher_write_bytes(hasher: &mut Hasher, bytes: &[u8]);
export proc hasher_write_u64(hasher: &mut Hasher, value: u64);
export proc hasher_write_u32(hasher: &mut Hasher, value: u32);
export proc hasher_write_u16(hasher: &mut Hasher, value: u16);
export proc hasher_write_i64(hasher: &mut Hasher, value: i64);
export proc hasher_length(hasher: &Hasher) -> u64;
export proc hasher_finish(hasher: &Hasher) -> u64;
```

## `core::hash::siphash`

Source: [`src/hash/siphash.vit`](src/hash/siphash.vit) · exports: 1

```vit
export proc siphash24(bytes: &[u8], key0: u64, key1: u64) -> u64;
```

## `core::internal::abi`

Source: [`src/internal/abi.vit`](src/internal/abi.vit) · exports: 5

```vit
export pick CallingConvention { Native, C, System }
export pick Endianness { Unknown, Little, Big }
export form AbiDescriptor {
    pointer_width: usize,
    size_width: usize,
    calling_convention: CallingConvention,
    endianness: Endianness,
}
export proc native_abi() -> AbiDescriptor;
export proc abi_is_valid(value: &AbiDescriptor) -> bool;
```

## `core::internal::builtin`

Source: [`src/internal/builtin.vit`](src/internal/builtin.vit) · exports: 3

```vit
export pick BuiltinCategory {
    Arithmetic,
    Bitwise,
    Memory,
    Atomic,
    TypeInfo,
    ControlFlow,
}
export pick BuiltinId {
    AtomicLoad,
    AtomicStore,
    AtomicExchange,
    AtomicCompareExchange,
    AtomicFetchAdd,
    AtomicFetchSub,
    AtomicFence,
    Unreachable,
}
export proc builtin_category(id: BuiltinId) -> BuiltinCategory;
```

## `core::internal::compiler`

Source: [`src/internal/compiler.vit`](src/internal/compiler.vit) · exports: 3

```vit
export form CompilerCapabilities {
    generic_forms: bool,
    nullable_types: bool,
    intrinsics: bool,
    atomic_operations: bool,
    type_layout_queries: bool,
}
export proc required_capabilities() -> CompilerCapabilities;
export proc capabilities_are_supported(value: &CompilerCapabilities) -> bool;
```

## `core::internal::platform`

Source: [`src/internal/platform.vit`](src/internal/platform.vit) · exports: 4

```vit
export pick OperatingSystem { Unknown, Linux, MacOS, Windows, FreeBSD }
export pick Architecture { Unknown, X86, X86_64, Arm, AArch64, Wasm32 }
export form Platform {
    operating_system: OperatingSystem,
    architecture: Architecture,
    pointer_width: usize,
    little_endian: bool,
}
export proc platform_is_valid(value: &Platform) -> bool;
```

## `core::internal::utilities`

Source: [`src/internal/utilities.vit`](src/internal/utilities.vit) · exports: 4

```vit
export proc internal_min_usize(left: usize, right: usize) -> usize;
export proc internal_max_usize(left: usize, right: usize) -> usize;
export proc internal_saturating_add_usize(left: usize, right: usize) -> usize;
export proc internal_string_equal(left: str, right: str) -> bool;
```

## `core::internal::validation`

Source: [`src/internal/validation.vit`](src/internal/validation.vit) · exports: 4

```vit
export proc validate_pointer_length(pointer: *const u8, length: usize) -> bool;
export proc validate_range(start: usize, end: usize, limit: usize) -> bool;
export proc validate_alignment(alignment: usize) -> bool;
export proc validate_slice_length<T>(value: &[T], expected: usize) -> bool;
```

## `core::intrinsics::arithmetic`

Source: [`src/intrinsics/arithmetic.vit`](src/intrinsics/arithmetic.vit) · exports: 9

```vit
export proc add_overflow_u64(left: u64, right: u64) -> bool;
export proc sub_overflow_u64(left: u64, right: u64) -> bool;
export proc mul_overflow_u64(left: u64, right: u64) -> bool;
export proc add_overflow_i64(left: i64, right: i64) -> bool;
export proc sub_overflow_i64(left: i64, right: i64) -> bool;
export proc mul_overflow_i64(left: i64, right: i64) -> bool;
export proc add_overflow_usize(left: usize, right: usize) -> bool;
export proc sub_overflow_usize(left: usize, right: usize) -> bool;
export proc mul_overflow_usize(left: usize, right: usize) -> bool;
```

## `core::intrinsics::atomic`

Source: [`src/intrinsics/atomic.vit`](src/intrinsics/atomic.vit) · exports: 7

```vit
export intrinsic atomic_load<T>(source: *const T, order: AtomicOrdering) -> T;
export intrinsic atomic_store<T>(destination: *mut T, value: T, order: AtomicOrdering);
export intrinsic atomic_exchange<T>(destination: *mut T, value: T, order: AtomicOrdering) -> T;
export intrinsic atomic_compare_exchange<T>(destination: *mut T, expected: T, replacement: T, success: AtomicOrdering, failure: AtomicOrdering) -> bool;
export intrinsic atomic_fetch_add<T>(destination: *mut T, value: T, order: AtomicOrdering) -> T;
export intrinsic atomic_fetch_sub<T>(destination: *mut T, value: T, order: AtomicOrdering) -> T;
export intrinsic atomic_fence(order: AtomicOrdering);
```

## `core::intrinsics::bitwise`

Source: [`src/intrinsics/bitwise.vit`](src/intrinsics/bitwise.vit) · exports: 23

```vit
export proc rotate_left_u8(value: u8, count: u32) -> u8;
export proc rotate_right_u8(value: u8, count: u32) -> u8;
export proc rotate_left_u16(value: u16, count: u32) -> u16;
export proc rotate_right_u16(value: u16, count: u32) -> u16;
export proc rotate_left_u32(value: u32, count: u32) -> u32;
export proc rotate_right_u32(value: u32, count: u32) -> u32;
export proc rotate_left_u64(value: u64, count: u32) -> u64;
export proc rotate_right_u64(value: u64, count: u32) -> u64;
export proc count_ones_u32(value: u32) -> u32;
export proc count_ones_u64(value: u64) -> u32;
export proc count_ones_u8(value: u8) -> u32;
export proc count_ones_u16(value: u16) -> u32;
export proc leading_zeros_u8(value: u8) -> u32;
export proc leading_zeros_u16(value: u16) -> u32;
export proc trailing_zeros_u8(value: u8) -> u32;
export proc trailing_zeros_u16(value: u16) -> u32;
export proc leading_zeros_u32(value: u32) -> u32;
export proc leading_zeros_u64(value: u64) -> u32;
export proc trailing_zeros_u32(value: u32) -> u32;
export proc trailing_zeros_u64(value: u64) -> u32;
export proc reverse_bytes_u16(value: u16) -> u16;
export proc reverse_bytes_u32(value: u32) -> u32;
export proc reverse_bytes_u64(value: u64) -> u64;
```

## `core::intrinsics::floating`

Source: [`src/intrinsics/floating.vit`](src/intrinsics/floating.vit) · exports: 6

```vit
export proc intrinsic_is_nan_f32(value: f32) -> bool;
export proc intrinsic_is_nan_f64(value: f64) -> bool;
export proc intrinsic_is_infinite_f32(value: f32) -> bool;
export proc intrinsic_is_infinite_f64(value: f64) -> bool;
export proc intrinsic_is_finite_f32(value: f32) -> bool;
export proc intrinsic_is_finite_f64(value: f64) -> bool;
```

## `core::intrinsics::memory`

Source: [`src/intrinsics/memory.vit`](src/intrinsics/memory.vit) · exports: 7

```vit
export intrinsic native_allocate(size: usize) -> *mut u8;
export intrinsic native_deallocate(pointer: *mut u8);
export intrinsic volatile_load<T>(source: *const T) -> T;
export intrinsic volatile_store<T>(destination: *mut T, value: T);
export proc intrinsic_fill(destination: *mut u8, byte: u8, length: usize) -> bool;
export proc intrinsic_copy(destination: *mut u8, source: *const u8, length: usize) -> bool;
export proc intrinsic_move(destination: *mut u8, source: *const u8, length: usize) -> bool;
```

## `core::intrinsics::type_info`

Source: [`src/intrinsics/type_info.vit`](src/intrinsics/type_info.vit) · exports: 5

```vit
export form TypeLayout {
    size: usize,
    align: usize,
}
export proc type_layout<T>() -> TypeLayout;
export proc type_size<T>() -> usize;
export proc type_alignment<T>() -> usize;
export proc type_layout_is_valid(value: &TypeLayout) -> bool;
```

## `core::intrinsics::unreachable`

Source: [`src/intrinsics/unreachable.vit`](src/intrinsics/unreachable.vit) · exports: 2

```vit
export intrinsic trap() -> never;
export proc unreachable() -> never;
```

## `core::iter::chain`

Source: [`src/iter/chain.vit`](src/iter/chain.vit) · exports: 1

```vit
export proc chain_copy<T>(left: &[T], right: &[T], destination: &mut [T]) -> usize;
```

## `core::iter::enumerate`

Source: [`src/iter/enumerate.vit`](src/iter/enumerate.vit) · exports: 1

```vit
export proc enumerate_copy<T>(
    source: &[T],
    values: &mut [T],
    indices: &mut [usize]
) -> usize;
```

## `core::iter::filter`

Source: [`src/iter/filter.vit`](src/iter/filter.vit) · exports: 1

```vit
export proc filter_copy<T>(
    source: &[T],
    destination: &mut [T],
    predicate: proc(&T) -> bool
) -> usize;
```

## `core::iter::fold`

Source: [`src/iter/fold.vit`](src/iter/fold.vit) · exports: 3

```vit
export form FoldStep<U> {
    accumulator: U,
    should_continue: bool,
}
export proc fold<T, U>(
    source: &[T],
    initial: U,
    combine: proc(U, &T) -> U
) -> U;
export proc fold_while<T, U>(
    source: &[T],
    initial: U,
    combine: proc(U, &T) -> FoldStep[U]
) -> U;
```

## `core::iter::iterator`

Source: [`src/iter/iterator.vit`](src/iter/iterator.vit) · exports: 8

```vit
export form SliceIterator<T> {
    source: &[T],
    front: usize,
    back: usize,
}
export proc slice_iterator<T>(source: &[T]) -> SliceIterator[T];
export proc iterator_is_empty<T>(iterator: &SliceIterator[T]) -> bool;
export proc iterator_remaining<T>(iterator: &SliceIterator[T]) -> usize;
export proc iterator_next<T>(iterator: &mut SliceIterator[T]) -> ?T;
export proc iterator_next_ref<T>(iterator: &mut SliceIterator[T]) -> ?&T;
export proc iterator_next_back<T>(iterator: &mut SliceIterator[T]) -> ?T;
export proc iterator_reset<T>(iterator: &mut SliceIterator[T]);
```

## `core::iter::map`

Source: [`src/iter/map.vit`](src/iter/map.vit) · exports: 1

```vit
export proc map<T, U>(
    source: &[T],
    destination: &mut [U],
    transform: proc(&T) -> U
) -> usize;
```

## `core::iter::peekable`

Source: [`src/iter/peekable.vit`](src/iter/peekable.vit) · exports: 4

```vit
export form Peekable<T> {
    source: &[T],
    position: usize,
}
export proc peekable<T>(source: &[T]) -> Peekable[T];
export proc peek<T>(iterator: &Peekable[T]) -> ?&T;
export proc peekable_next<T>(iterator: &mut Peekable[T]) -> ?T;
```

## `core::iter::range`

Source: [`src/iter/range.vit`](src/iter/range.vit) · exports: 3

```vit
export form RangeIterator {
    next: usize,
    end: usize,
}
export proc range_iterator(range: Range) -> RangeIterator;
export proc range_iterator_next(iterator: &mut RangeIterator) -> ?usize;
```

## `core::iter::reduce`

Source: [`src/iter/reduce.vit`](src/iter/reduce.vit) · exports: 1

```vit
export proc reduce<T>(source: &[T], combine: proc(T, T) -> T) -> ?T;
```

## `core::iter::repeat`

Source: [`src/iter/repeat.vit`](src/iter/repeat.vit) · exports: 1

```vit
export proc repeat_fill<T>(destination: &mut [T], value: T) -> usize;
```

## `core::iter::skip`

Source: [`src/iter/skip.vit`](src/iter/skip.vit) · exports: 2

```vit
export proc skip_start(length: usize, count: usize) -> usize;
export proc skip_remaining(length: usize, count: usize) -> usize;
```

## `core::iter::take`

Source: [`src/iter/take.vit`](src/iter/take.vit) · exports: 1

```vit
export proc take_count(length: usize, count: usize) -> usize;
```

## `core::iter::zip`

Source: [`src/iter/zip.vit`](src/iter/zip.vit) · exports: 1

```vit
export proc zip_copy<T, U>(
    left: &[T],
    right: &[U],
    left_output: &mut [T],
    right_output: &mut [U]
) -> usize;
```

## `core::marker::copy`

Source: [`src/marker/copy.vit`](src/marker/copy.vit) · exports: 1

```vit
export trait Copy {}
```

## `core::marker::phantom`

Source: [`src/marker/phantom.vit`](src/marker/phantom.vit) · exports: 1

```vit
export type PhantomData<T> = [T;
```

## `core::marker::send`

Source: [`src/marker/send.vit`](src/marker/send.vit) · exports: 1

```vit

```

## `core::marker::sized`

Source: [`src/marker/sized.vit`](src/marker/sized.vit) · exports: 1

```vit
export trait Sized {}
```

## `core::marker::sync`

Source: [`src/marker/sync.vit`](src/marker/sync.vit) · exports: 1

```vit

```

## `core::marker::unpin`

Source: [`src/marker/unpin.vit`](src/marker/unpin.vit) · exports: 1

```vit
export trait Unpin {}
```

## `core::mem::align`

Source: [`src/mem/align.vit`](src/mem/align.vit) · exports: 3

```vit
export proc mem_alignment_is_valid(alignment: usize) -> bool;
export proc mem_align_down(value: usize, alignment: usize) -> ?usize;
export proc mem_align_up(value: usize, alignment: usize) -> ?usize;
```

## `core::mem::forget`

Source: [`src/mem/forget.vit`](src/mem/forget.vit) · exports: 1

```vit
export proc mem_forget<T>(value: T);
```

## `core::mem::layout`

Source: [`src/mem/layout.vit`](src/mem/layout.vit) · exports: 8

```vit
export form Layout {
    valid: bool,
    size: usize,
    align: usize,
}
export form LayoutExtension {
    valid: bool,
    layout: Layout,
    offset: usize,
}
export proc layout_is_valid(value: &Layout) -> bool;
export proc layout(size: usize, alignment: usize) -> Layout;
export proc layout_of<T>() -> Layout;
export proc layout_array(element: Layout, count: usize) -> Layout;
export proc layout_extend_with_offset(left: Layout, right: Layout) -> LayoutExtension;
export proc layout_extend(left: Layout, right: Layout) -> Layout;
```

## `core::mem::memory`

Source: [`src/mem/memory.vitte`](src/mem/memory.vitte) · exports: 5

```vit
export proc mem_fill(destination: *mut u8, value: u8, length: usize) -> bool;
export proc mem_copy(destination: *mut u8, source: *const u8, length: usize) -> bool;
export proc mem_compare(left: *const u8, right: *const u8, length: usize) -> ?i8;
export proc mem_equal(left: *const u8, right: *const u8, length: usize) -> bool;
export proc mem_zero(destination: *mut u8, length: usize) -> bool;
```

## `core::mem::pointer`

Source: [`src/mem/pointer.vit`](src/mem/pointer.vit) · exports: 4

```vit
export type Address = usize;
export proc mem_address<T>(pointer: *const T) -> Address;
export proc mem_address_add(address: Address, offset: usize) -> ?Address;
export proc mem_pointer_from_address<T>(address: Address) -> *mut T;
```

## `core::mem::replace`

Source: [`src/mem/replace.vit`](src/mem/replace.vit) · exports: 1

```vit
export proc mem_replace<T>(destination: &mut T, replacement: T) -> T;
```

## `core::mem::size`

Source: [`src/mem/size.vit`](src/mem/size.vit) · exports: 3

```vit
export proc mem_size_of<T>() -> usize;
export proc mem_align_of<T>() -> usize;
export proc mem_array_size<T>(count: usize) -> ?usize;
```

## `core::mem::swap`

Source: [`src/mem/swap.vit`](src/mem/swap.vit) · exports: 1

```vit
export proc mem_swap<T>(left: &mut T, right: &mut T);
```

## `core::mem::take`

Source: [`src/mem/take.vit`](src/mem/take.vit) · exports: 1

```vit
export proc mem_take<T>(source: &mut T, replacement: T) -> T;
```

## `core::mem::transmute`

Source: [`src/mem/transmute.vit`](src/mem/transmute.vit) · exports: 1

```vit

```

## `core::mem::uninit`

Source: [`src/mem/uninit.vit`](src/mem/uninit.vit) · exports: 7

```vit
export form MaybeUninit<T> {
    initialized: bool,
    value: ?T,
}
export proc maybe_uninit<T>() -> MaybeUninit[T];
export proc maybe_uninit_from<T>(value: T) -> MaybeUninit[T];
export proc maybe_uninit_is_initialized<T>(value: &MaybeUninit[T]) -> bool;
export proc maybe_uninit_write<T>(destination: &mut MaybeUninit[T], value: T);
export proc maybe_uninit_clear<T>(destination: &mut MaybeUninit[T]);
export proc maybe_uninit_read<T>(source: &MaybeUninit[T]) -> ?T;
```

## `core::mem::zeroed`

Source: [`src/mem/zeroed.vit`](src/mem/zeroed.vit) · exports: 1

```vit
export proc mem_zeroed_bytes(destination: *mut u8, length: usize) -> bool;
```

## `core::num::checked`

Source: [`src/num/checked.vit`](src/num/checked.vit) · exports: 18

```vit
export pick CheckedError {
    None,
    Overflow,
    DivisionByZero,
}
export form CheckedI64Result {
    success: bool,
    value: i64,
    error: CheckedError,
}
export form CheckedU64Result {
    success: bool,
    value: u64,
    error: CheckedError,
}
export type CheckedI64 = CheckedI64Result;
export type CheckedU64 = CheckedU64Result;
export proc checked_u64_add(left: u64, right: u64) -> CheckedU64Result;
export proc checked_u64_sub(left: u64, right: u64) -> CheckedU64Result;
export proc checked_u64_mul(left: u64, right: u64) -> CheckedU64Result;
export proc checked_u64_div(left: u64, right: u64) -> CheckedU64Result;
export proc checked_u64_rem(left: u64, right: u64) -> CheckedU64Result;
export proc checked_i64_add(left: i64, right: i64) -> CheckedI64Result;
export proc checked_i64_sub(left: i64, right: i64) -> CheckedI64Result;
export proc checked_i64_mul(left: i64, right: i64) -> CheckedI64Result;
export proc checked_i64_div(left: i64, right: i64) -> CheckedI64Result;
export proc checked_i64_rem(left: i64, right: i64) -> CheckedI64Result;
export proc checked_i64_neg(value: i64) -> CheckedI64Result;
export proc checked_u64_pow(base: u64, exponent: u32) -> CheckedU64Result;
export proc checked_i64_pow(base: i64, exponent: u32) -> CheckedI64Result;
```

## `core::num::float`

Source: [`src/num/float.vit`](src/num/float.vit) · exports: 6

```vit
export proc float_is_nan_f32(value: f32) -> bool;
export proc float_is_nan_f64(value: f64) -> bool;
export proc float_is_infinite_f32(value: f32) -> bool;
export proc float_is_infinite_f64(value: f64) -> bool;
export proc float_is_finite_f32(value: f32) -> bool;
export proc float_is_finite_f64(value: f64) -> bool;
```

## `core::num::integer`

Source: [`src/num/integer.vit`](src/num/integer.vit) · exports: 6

```vit
export proc integer_gcd_u64(left: u64, right: u64) -> u64;
export proc integer_lcm_u64(left: u64, right: u64, output: &mut u64) -> bool;
export proc integer_sqrt_u64(value: u64) -> u64;
export proc integer_abs_diff_u64(left: u64, right: u64) -> u64;
export proc integer_is_power_of_two_u64(value: u64) -> bool;
export proc integer_next_power_of_two_u64(value: u64, output: &mut u64) -> bool;
```

## `core::num::limits`

Source: [`src/num/limits.vit`](src/num/limits.vit) · exports: 14

```vit
export const I8_MIN: i8 = -128;
export const I8_MAX: i8 = 127;
export const I16_MIN: i16 = -32768;
export const I16_MAX: i16 = 32767;
export const I32_MIN: i32 = -2147483647 - 1;
export const I32_MAX: i32 = 2147483647;
export const I64_MIN: i64 = -9223372036854775807 - 1;
export const I64_MAX: i64 = 9223372036854775807;
export const U8_MAX: u8 = 255;
export const U16_MAX: u16 = 65535;
export const U32_MAX: u32 = 4294967295;
export const U64_MAX: u64 = 18446744073709551615;
export const ISIZE_MAX: isize = 9223372036854775807;
export const USIZE_MAX: usize = (0 as usize) - 1;
```

## `core::num::nonzero`

Source: [`src/num/nonzero.vit`](src/num/nonzero.vit) · exports: 9

```vit
export form NonZeroU64 { value: u64 }
export form NonZeroU32 { value: u32 }
export form NonZeroI64 { value: i64 }
export proc nonzero_u64(value: u64) -> ?NonZeroU64;
export proc nonzero_u32(value: u32) -> ?NonZeroU32;
export proc nonzero_i64(value: i64) -> ?NonZeroI64;
export proc nonzero_u64_get(value: &NonZeroU64) -> u64;
export proc nonzero_u32_get(value: &NonZeroU32) -> u32;
export proc nonzero_i64_get(value: &NonZeroI64) -> i64;
```

## `core::num::overflowing`

Source: [`src/num/overflowing.vit`](src/num/overflowing.vit) · exports: 8

```vit
export form OverflowingU64 {
    value: u64,
    overflowed: bool,
}
export form OverflowingI64 {
    value: i64,
    overflowed: bool,
}
export proc overflowing_u64_add(left: u64, right: u64) -> OverflowingU64;
export proc overflowing_u64_sub(left: u64, right: u64) -> OverflowingU64;
export proc overflowing_u64_mul(left: u64, right: u64) -> OverflowingU64;
export proc overflowing_i64_add(left: i64, right: i64) -> OverflowingI64;
export proc overflowing_i64_sub(left: i64, right: i64) -> OverflowingI64;
export proc overflowing_i64_mul(left: i64, right: i64) -> OverflowingI64;
```

## `core::num::radix`

Source: [`src/num/radix.vit`](src/num/radix.vit) · exports: 2

```vit
export proc radix_digit_to_char(value: u8, uppercase: bool) -> ?char;
export proc radix_char_to_digit(value: char) -> ?u8;
```

## `core::num::saturating`

Source: [`src/num/saturating.vit`](src/num/saturating.vit) · exports: 8

```vit
export proc saturating_u64_add(left: u64, right: u64) -> u64;
export proc saturating_u64_sub(left: u64, right: u64) -> u64;
export proc saturating_i64_add(left: i64, right: i64) -> i64;
export proc saturating_i64_sub(left: i64, right: i64) -> i64;
export proc saturating_u64_mul(left: u64, right: u64) -> u64;
export proc saturating_i64_mul(left: i64, right: i64) -> i64;
export proc saturating_i64_neg(value: i64) -> i64;
export proc saturating_i64_abs(value: i64) -> i64;
```

## `core::num::wrapping`

Source: [`src/num/wrapping.vit`](src/num/wrapping.vit) · exports: 24

```vit
export proc wrapping_u64_add(left: u64, right: u64) -> u64;
export proc wrapping_u64_sub(left: u64, right: u64) -> u64;
export proc wrapping_u64_mul(left: u64, right: u64) -> u64;
export proc wrapping_u8_add(left: u8, right: u8) -> u8;
export proc wrapping_u8_sub(left: u8, right: u8) -> u8;
export proc wrapping_u8_mul(left: u8, right: u8) -> u8;
export proc wrapping_u16_add(left: u16, right: u16) -> u16;
export proc wrapping_u16_sub(left: u16, right: u16) -> u16;
export proc wrapping_u16_mul(left: u16, right: u16) -> u16;
export proc wrapping_u32_add(left: u32, right: u32) -> u32;
export proc wrapping_u32_sub(left: u32, right: u32) -> u32;
export proc wrapping_u32_mul(left: u32, right: u32) -> u32;
export proc wrapping_i64_add(left: i64, right: i64) -> i64;
export proc wrapping_i64_sub(left: i64, right: i64) -> i64;
export proc wrapping_i64_mul(left: i64, right: i64) -> i64;
export proc wrapping_i8_add(left: i8, right: i8) -> i8;
export proc wrapping_i8_sub(left: i8, right: i8) -> i8;
export proc wrapping_i8_mul(left: i8, right: i8) -> i8;
export proc wrapping_i16_add(left: i16, right: i16) -> i16;
export proc wrapping_i16_sub(left: i16, right: i16) -> i16;
export proc wrapping_i16_mul(left: i16, right: i16) -> i16;
export proc wrapping_i32_add(left: i32, right: i32) -> i32;
export proc wrapping_i32_sub(left: i32, right: i32) -> i32;
export proc wrapping_i32_mul(left: i32, right: i32) -> i32;
```

## `core::ops::add`

Source: [`src/ops/add.vit`](src/ops/add.vit) · exports: 10

```vit
export proc add_i8(left: i8, right: i8) -> i8;
export proc add_i16(left: i16, right: i16) -> i16;
export proc add_i32(left: i32, right: i32) -> i32;
export proc add_i64(left: i64, right: i64) -> i64;
export proc add_u8(left: u8, right: u8) -> u8;
export proc add_u16(left: u16, right: u16) -> u16;
export proc add_u32(left: u32, right: u32) -> u32;
export proc add_u64(left: u64, right: u64) -> u64;
export proc add_f32(left: f32, right: f32) -> f32;
export proc add_f64(left: f64, right: f64) -> f64;
```

## `core::ops::assign`

Source: [`src/ops/assign.vit`](src/ops/assign.vit) · exports: 88

```vit
export proc add_assign_i8(left: &mut i8, right: i8);
export proc sub_assign_i8(left: &mut i8, right: i8);
export proc mul_assign_i8(left: &mut i8, right: i8);
export proc div_assign_i8(left: &mut i8, right: i8);
export proc rem_assign_i8(left: &mut i8, right: i8);
export proc add_assign_i16(left: &mut i16, right: i16);
export proc sub_assign_i16(left: &mut i16, right: i16);
export proc mul_assign_i16(left: &mut i16, right: i16);
export proc div_assign_i16(left: &mut i16, right: i16);
export proc rem_assign_i16(left: &mut i16, right: i16);
export proc add_assign_i32(left: &mut i32, right: i32);
export proc sub_assign_i32(left: &mut i32, right: i32);
export proc mul_assign_i32(left: &mut i32, right: i32);
export proc div_assign_i32(left: &mut i32, right: i32);
export proc rem_assign_i32(left: &mut i32, right: i32);
export proc add_assign_i64(left: &mut i64, right: i64);
export proc sub_assign_i64(left: &mut i64, right: i64);
export proc mul_assign_i64(left: &mut i64, right: i64);
export proc div_assign_i64(left: &mut i64, right: i64);
export proc rem_assign_i64(left: &mut i64, right: i64);
export proc add_assign_u8(left: &mut u8, right: u8);
export proc sub_assign_u8(left: &mut u8, right: u8);
export proc mul_assign_u8(left: &mut u8, right: u8);
export proc div_assign_u8(left: &mut u8, right: u8);
export proc rem_assign_u8(left: &mut u8, right: u8);
export proc add_assign_u16(left: &mut u16, right: u16);
export proc sub_assign_u16(left: &mut u16, right: u16);
export proc mul_assign_u16(left: &mut u16, right: u16);
export proc div_assign_u16(left: &mut u16, right: u16);
export proc rem_assign_u16(left: &mut u16, right: u16);
export proc add_assign_u32(left: &mut u32, right: u32);
export proc sub_assign_u32(left: &mut u32, right: u32);
export proc mul_assign_u32(left: &mut u32, right: u32);
export proc div_assign_u32(left: &mut u32, right: u32);
export proc rem_assign_u32(left: &mut u32, right: u32);
export proc add_assign_u64(left: &mut u64, right: u64);
export proc sub_assign_u64(left: &mut u64, right: u64);
export proc mul_assign_u64(left: &mut u64, right: u64);
export proc div_assign_u64(left: &mut u64, right: u64);
export proc rem_assign_u64(left: &mut u64, right: u64);
export proc add_assign_f32(left: &mut f32, right: f32);
export proc sub_assign_f32(left: &mut f32, right: f32);
export proc mul_assign_f32(left: &mut f32, right: f32);
export proc div_assign_f32(left: &mut f32, right: f32);
export proc add_assign_f64(left: &mut f64, right: f64);
export proc sub_assign_f64(left: &mut f64, right: f64);
export proc mul_assign_f64(left: &mut f64, right: f64);
export proc div_assign_f64(left: &mut f64, right: f64);
export proc and_assign_i8(left: &mut i8, right: i8);
export proc or_assign_i8(left: &mut i8, right: i8);
export proc xor_assign_i8(left: &mut i8, right: i8);
export proc shl_assign_i8(left: &mut i8, right: usize);
export proc shr_assign_i8(left: &mut i8, right: usize);
export proc and_assign_i16(left: &mut i16, right: i16);
export proc or_assign_i16(left: &mut i16, right: i16);
export proc xor_assign_i16(left: &mut i16, right: i16);
export proc shl_assign_i16(left: &mut i16, right: usize);
export proc shr_assign_i16(left: &mut i16, right: usize);
export proc and_assign_i32(left: &mut i32, right: i32);
export proc or_assign_i32(left: &mut i32, right: i32);
export proc xor_assign_i32(left: &mut i32, right: i32);
export proc shl_assign_i32(left: &mut i32, right: usize);
export proc shr_assign_i32(left: &mut i32, right: usize);
export proc and_assign_i64(left: &mut i64, right: i64);
export proc or_assign_i64(left: &mut i64, right: i64);
export proc xor_assign_i64(left: &mut i64, right: i64);
export proc shl_assign_i64(left: &mut i64, right: usize);
export proc shr_assign_i64(left: &mut i64, right: usize);
export proc and_assign_u8(left: &mut u8, right: u8);
export proc or_assign_u8(left: &mut u8, right: u8);
export proc xor_assign_u8(left: &mut u8, right: u8);
export proc shl_assign_u8(left: &mut u8, right: usize);
export proc shr_assign_u8(left: &mut u8, right: usize);
export proc and_assign_u16(left: &mut u16, right: u16);
export proc or_assign_u16(left: &mut u16, right: u16);
export proc xor_assign_u16(left: &mut u16, right: u16);
export proc shl_assign_u16(left: &mut u16, right: usize);
export proc shr_assign_u16(left: &mut u16, right: usize);
export proc and_assign_u32(left: &mut u32, right: u32);
export proc or_assign_u32(left: &mut u32, right: u32);
export proc xor_assign_u32(left: &mut u32, right: u32);
export proc shl_assign_u32(left: &mut u32, right: usize);
export proc shr_assign_u32(left: &mut u32, right: usize);
export proc and_assign_u64(left: &mut u64, right: u64);
export proc or_assign_u64(left: &mut u64, right: u64);
export proc xor_assign_u64(left: &mut u64, right: u64);
export proc shl_assign_u64(left: &mut u64, right: usize);
export proc shr_assign_u64(left: &mut u64, right: usize);
```

## `core::ops::bitand`

Source: [`src/ops/bitand.vit`](src/ops/bitand.vit) · exports: 8

```vit
export proc bitand_i8(left: i8, right: i8) -> i8;
export proc bitand_i16(left: i16, right: i16) -> i16;
export proc bitand_i32(left: i32, right: i32) -> i32;
export proc bitand_i64(left: i64, right: i64) -> i64;
export proc bitand_u8(left: u8, right: u8) -> u8;
export proc bitand_u16(left: u16, right: u16) -> u16;
export proc bitand_u32(left: u32, right: u32) -> u32;
export proc bitand_u64(left: u64, right: u64) -> u64;
```

## `core::ops::bitor`

Source: [`src/ops/bitor.vit`](src/ops/bitor.vit) · exports: 8

```vit
export proc bitor_i8(left: i8, right: i8) -> i8;
export proc bitor_i16(left: i16, right: i16) -> i16;
export proc bitor_i32(left: i32, right: i32) -> i32;
export proc bitor_i64(left: i64, right: i64) -> i64;
export proc bitor_u8(left: u8, right: u8) -> u8;
export proc bitor_u16(left: u16, right: u16) -> u16;
export proc bitor_u32(left: u32, right: u32) -> u32;
export proc bitor_u64(left: u64, right: u64) -> u64;
```

## `core::ops::bitxor`

Source: [`src/ops/bitxor.vit`](src/ops/bitxor.vit) · exports: 8

```vit
export proc bitxor_i8(left: i8, right: i8) -> i8;
export proc bitxor_i16(left: i16, right: i16) -> i16;
export proc bitxor_i32(left: i32, right: i32) -> i32;
export proc bitxor_i64(left: i64, right: i64) -> i64;
export proc bitxor_u8(left: u8, right: u8) -> u8;
export proc bitxor_u16(left: u16, right: u16) -> u16;
export proc bitxor_u32(left: u32, right: u32) -> u32;
export proc bitxor_u64(left: u64, right: u64) -> u64;
```

## `core::ops::deref`

Source: [`src/ops/deref.vit`](src/ops/deref.vit) · exports: 2

```vit
export proc deref<T>(value: &T) -> &T;
export proc deref_mut<T>(value: &mut T) -> &mut T;
```

## `core::ops::div`

Source: [`src/ops/div.vit`](src/ops/div.vit) · exports: 18

```vit
export proc div_i8(left: i8, right: i8) -> i8;
export proc div_i16(left: i16, right: i16) -> i16;
export proc div_i32(left: i32, right: i32) -> i32;
export proc div_i64(left: i64, right: i64) -> i64;
export proc div_u8(left: u8, right: u8) -> u8;
export proc div_u16(left: u16, right: u16) -> u16;
export proc div_u32(left: u32, right: u32) -> u32;
export proc div_u64(left: u64, right: u64) -> u64;
export proc div_f32(left: f32, right: f32) -> f32;
export proc div_f64(left: f64, right: f64) -> f64;
export proc checked_div_i8(left: i8, right: i8) -> ?i8;
export proc checked_div_i16(left: i16, right: i16) -> ?i16;
export proc checked_div_i32(left: i32, right: i32) -> ?i32;
export proc checked_div_i64(left: i64, right: i64) -> ?i64;
export proc checked_div_u8(left: u8, right: u8) -> ?u8;
export proc checked_div_u16(left: u16, right: u16) -> ?u16;
export proc checked_div_u32(left: u32, right: u32) -> ?u32;
export proc checked_div_u64(left: u64, right: u64) -> ?u64;
```

## `core::ops::index`

Source: [`src/ops/index.vit`](src/ops/index.vit) · exports: 2

```vit
export proc index<T>(slice: &[T], position: usize) -> ?&T;
export proc index_mut<T>(slice: &mut [T], position: usize) -> ?&mut T;
```

## `core::ops::mod`

Source: [`src/ops/mod.vit`](src/ops/mod.vit) · exports: 8

```vit
export proc mod_i8(left: i8, right: i8) -> ?i8;
export proc mod_i16(left: i16, right: i16) -> ?i16;
export proc mod_i32(left: i32, right: i32) -> ?i32;
export proc mod_i64(left: i64, right: i64) -> ?i64;
export proc mod_u8(left: u8, right: u8) -> ?u8;
export proc mod_u16(left: u16, right: u16) -> ?u16;
export proc mod_u32(left: u32, right: u32) -> ?u32;
export proc mod_u64(left: u64, right: u64) -> ?u64;
```

## `core::ops::mul`

Source: [`src/ops/mul.vit`](src/ops/mul.vit) · exports: 10

```vit
export proc mul_i8(left: i8, right: i8) -> i8;
export proc mul_i16(left: i16, right: i16) -> i16;
export proc mul_i32(left: i32, right: i32) -> i32;
export proc mul_i64(left: i64, right: i64) -> i64;
export proc mul_u8(left: u8, right: u8) -> u8;
export proc mul_u16(left: u16, right: u16) -> u16;
export proc mul_u32(left: u32, right: u32) -> u32;
export proc mul_u64(left: u64, right: u64) -> u64;
export proc mul_f32(left: f32, right: f32) -> f32;
export proc mul_f64(left: f64, right: f64) -> f64;
```

## `core::ops::neg`

Source: [`src/ops/neg.vit`](src/ops/neg.vit) · exports: 6

```vit
export proc neg_i8(value: i8) -> i8;
export proc neg_i16(value: i16) -> i16;
export proc neg_i32(value: i32) -> i32;
export proc neg_i64(value: i64) -> i64;
export proc neg_f32(value: f32) -> f32;
export proc neg_f64(value: f64) -> f64;
```

## `core::ops::not`

Source: [`src/ops/not.vit`](src/ops/not.vit) · exports: 9

```vit
export proc bitnot_i8(value: i8) -> i8;
export proc bitnot_i16(value: i16) -> i16;
export proc bitnot_i32(value: i32) -> i32;
export proc bitnot_i64(value: i64) -> i64;
export proc bitnot_u8(value: u8) -> u8;
export proc bitnot_u16(value: u16) -> u16;
export proc bitnot_u32(value: u32) -> u32;
export proc bitnot_u64(value: u64) -> u64;
export proc logical_not(value: bool) -> bool;
```

## `core::ops::rem`

Source: [`src/ops/rem.vit`](src/ops/rem.vit) · exports: 16

```vit
export proc rem_i8(left: i8, right: i8) -> i8;
export proc rem_i16(left: i16, right: i16) -> i16;
export proc rem_i32(left: i32, right: i32) -> i32;
export proc rem_i64(left: i64, right: i64) -> i64;
export proc rem_u8(left: u8, right: u8) -> u8;
export proc rem_u16(left: u16, right: u16) -> u16;
export proc rem_u32(left: u32, right: u32) -> u32;
export proc rem_u64(left: u64, right: u64) -> u64;
export proc checked_rem_i8(left: i8, right: i8) -> ?i8;
export proc checked_rem_i16(left: i16, right: i16) -> ?i16;
export proc checked_rem_i32(left: i32, right: i32) -> ?i32;
export proc checked_rem_i64(left: i64, right: i64) -> ?i64;
export proc checked_rem_u8(left: u8, right: u8) -> ?u8;
export proc checked_rem_u16(left: u16, right: u16) -> ?u16;
export proc checked_rem_u32(left: u32, right: u32) -> ?u32;
export proc checked_rem_u64(left: u64, right: u64) -> ?u64;
```

## `core::ops::shl`

Source: [`src/ops/shl.vit`](src/ops/shl.vit) · exports: 16

```vit
export proc shl_i8(left: i8, amount: usize) -> i8;
export proc shl_i16(left: i16, amount: usize) -> i16;
export proc shl_i32(left: i32, amount: usize) -> i32;
export proc shl_i64(left: i64, amount: usize) -> i64;
export proc shl_u8(left: u8, amount: usize) -> u8;
export proc shl_u16(left: u16, amount: usize) -> u16;
export proc shl_u32(left: u32, amount: usize) -> u32;
export proc shl_u64(left: u64, amount: usize) -> u64;
export proc checked_shl_i8(left: i8, amount: usize, output: &mut i8) -> bool;
export proc checked_shl_i16(left: i16, amount: usize, output: &mut i16) -> bool;
export proc checked_shl_i32(left: i32, amount: usize, output: &mut i32) -> bool;
export proc checked_shl_i64(left: i64, amount: usize, output: &mut i64) -> bool;
export proc checked_shl_u8(left: u8, amount: usize, output: &mut u8) -> bool;
export proc checked_shl_u16(left: u16, amount: usize, output: &mut u16) -> bool;
export proc checked_shl_u32(left: u32, amount: usize, output: &mut u32) -> bool;
export proc checked_shl_u64(left: u64, amount: usize, output: &mut u64) -> bool;
```

## `core::ops::shr`

Source: [`src/ops/shr.vit`](src/ops/shr.vit) · exports: 16

```vit
export proc shr_i8(left: i8, amount: usize) -> i8;
export proc shr_i16(left: i16, amount: usize) -> i16;
export proc shr_i32(left: i32, amount: usize) -> i32;
export proc shr_i64(left: i64, amount: usize) -> i64;
export proc shr_u8(left: u8, amount: usize) -> u8;
export proc shr_u16(left: u16, amount: usize) -> u16;
export proc shr_u32(left: u32, amount: usize) -> u32;
export proc shr_u64(left: u64, amount: usize) -> u64;
export proc checked_shr_i8(left: i8, amount: usize, output: &mut i8) -> bool;
export proc checked_shr_i16(left: i16, amount: usize, output: &mut i16) -> bool;
export proc checked_shr_i32(left: i32, amount: usize, output: &mut i32) -> bool;
export proc checked_shr_i64(left: i64, amount: usize, output: &mut i64) -> bool;
export proc checked_shr_u8(left: u8, amount: usize, output: &mut u8) -> bool;
export proc checked_shr_u16(left: u16, amount: usize, output: &mut u16) -> bool;
export proc checked_shr_u32(left: u32, amount: usize, output: &mut u32) -> bool;
export proc checked_shr_u64(left: u64, amount: usize, output: &mut u64) -> bool;
```

## `core::ops::sub`

Source: [`src/ops/sub.vit`](src/ops/sub.vit) · exports: 10

```vit
export proc sub_i8(left: i8, right: i8) -> i8;
export proc sub_i16(left: i16, right: i16) -> i16;
export proc sub_i32(left: i32, right: i32) -> i32;
export proc sub_i64(left: i64, right: i64) -> i64;
export proc sub_u8(left: u8, right: u8) -> u8;
export proc sub_u16(left: u16, right: u16) -> u16;
export proc sub_u32(left: u32, right: u32) -> u32;
export proc sub_u64(left: u64, right: u64) -> u64;
export proc sub_f32(left: f32, right: f32) -> f32;
export proc sub_f64(left: f64, right: f64) -> f64;
```

## `core::prelude`

Source: [`src/prelude.vit`](src/prelude.vit) · exports: 2

```vit
export type Option<T> = ?T;
export proc identity<T>(value: T) -> T;
```

## `core::primitive::bool`

Source: [`src/primitive/bool.vit`](src/primitive/bool.vit) · exports: 5

```vit
export proc bool_is_true(value: bool) -> bool;
export proc bool_is_false(value: bool) -> bool;
export proc bool_xor(left: bool, right: bool) -> bool;
export proc bool_equal(left: bool, right: bool) -> bool;
export proc bool_to_u8(value: bool) -> u8;
```

## `core::primitive::char`

Source: [`src/primitive/char.vit`](src/primitive/char.vit) · exports: 10

```vit
export proc char_to_u32(value: char) -> u32;
export proc char_is_ascii(value: char) -> bool;
export proc char_is_ascii_digit(value: char) -> bool;
export proc char_is_ascii_lowercase(value: char) -> bool;
export proc char_is_ascii_uppercase(value: char) -> bool;
export proc char_is_ascii_alphabetic(value: char) -> bool;
export proc char_is_ascii_alphanumeric(value: char) -> bool;
export proc char_is_ascii_whitespace(value: char) -> bool;
export proc char_to_ascii_lowercase(value: char) -> char;
export proc char_to_ascii_uppercase(value: char) -> char;
```

## `core::primitive::float`

Source: [`src/primitive/float.vit`](src/primitive/float.vit) · exports: 10

```vit
export proc f32_is_nan(value: f32) -> bool;
export proc f64_is_nan(value: f64) -> bool;
export proc f32_is_infinite(value: f32) -> bool;
export proc f64_is_infinite(value: f64) -> bool;
export proc f32_is_finite(value: f32) -> bool;
export proc f64_is_finite(value: f64) -> bool;
export proc f32_is_zero(value: f32) -> bool;
export proc f64_is_zero(value: f64) -> bool;
export proc f32_abs(value: f32) -> f32;
export proc f64_abs(value: f64) -> f64;
```

## `core::primitive::integer`

Source: [`src/primitive/integer.vit`](src/primitive/integer.vit) · exports: 8

```vit
export proc i64_is_zero(value: i64) -> bool;
export proc u64_is_zero(value: u64) -> bool;
export proc i64_is_even(value: i64) -> bool;
export proc u64_is_even(value: u64) -> bool;
export proc i64_is_odd(value: i64) -> bool;
export proc u64_is_odd(value: u64) -> bool;
export proc u64_is_power_of_two(value: u64) -> bool;
export proc usize_is_power_of_two(value: usize) -> bool;
```

## `core::primitive::pointer`

Source: [`src/primitive/pointer.vit`](src/primitive/pointer.vit) · exports: 4

```vit
export type PointerInteger = usize;
export proc pointer_is_null<T>(value: *const T) -> bool;
export proc pointer_is_null_mut<T>(value: *mut T) -> bool;
export proc pointer_address<T>(value: *const T) -> PointerInteger;
```

## `core::primitive::signed`

Source: [`src/primitive/signed.vit`](src/primitive/signed.vit) · exports: 5

```vit
export proc i64_is_negative(value: i64) -> bool;
export proc i64_is_positive(value: i64) -> bool;
export proc i64_signum(value: i64) -> i8;
export proc i64_unsigned_abs(value: i64) -> u64;
export proc i32_signum(value: i32) -> i8;
```

## `core::primitive::unit`

Source: [`src/primitive/unit.vit`](src/primitive/unit.vit) · exports: 2

```vit
export form Unit {}
export proc unit_value() -> Unit;
```

## `core::primitive::unsigned`

Source: [`src/primitive/unsigned.vit`](src/primitive/unsigned.vit) · exports: 7

```vit
export proc u64_is_zero(value: u64) -> bool;
export proc u64_is_even(value: u64) -> bool;
export proc u64_is_odd(value: u64) -> bool;
export proc u64_is_power_of_two(value: u64) -> bool;
export proc u32_is_power_of_two(value: u32) -> bool;
export proc u16_is_power_of_two(value: u16) -> bool;
export proc u8_is_power_of_two(value: u8) -> bool;
```

## `core::ptr::arithmetic`

Source: [`src/ptr/arithmetic.vit`](src/ptr/arithmetic.vit) · exports: 6

```vit
export proc ptr_add_bytes(pointer: *mut u8, offset: usize) -> *mut u8;
export proc ptr_sub_bytes(pointer: *mut u8, offset: usize) -> *mut u8;
export proc ptr_add_elements<T>(pointer: *mut T, count: usize) -> *mut T;
export proc ptr_sub_elements<T>(pointer: *mut T, count: usize) -> *mut T;
export proc ptr_offset_elements<T>(pointer: *mut T, offset: isize) -> *mut T;
export proc ptr_is_aligned<T>(pointer: *const T) -> bool;
```

## `core::ptr::compare`

Source: [`src/ptr/compare.vit`](src/ptr/compare.vit) · exports: 4

```vit
export proc ptr_equal<T>(left: *const T, right: *const T) -> bool;
export proc ptr_not_equal<T>(left: *const T, right: *const T) -> bool;
export proc ptr_address<T>(value: *const T) -> usize;
export proc ptr_before<T>(left: *const T, right: *const T) -> bool;
```

## `core::ptr::const_ptr`

Source: [`src/ptr/const_ptr.vit`](src/ptr/const_ptr.vit) · exports: 2

```vit
export proc const_ptr<T>(value: &T) -> *const T;
export proc const_ptr_is_null<T>(value: *const T) -> bool;
```

## `core::ptr::copy`

Source: [`src/ptr/copy.vit`](src/ptr/copy.vit) · exports: 1

```vit
export proc ptr_copy(destination: *mut u8, source: *const u8, length: usize) -> bool;
```

## `core::ptr`

Source: [`src/ptr/mod.vit`](src/ptr/mod.vit) · exports: 1

```vit
export proc ptr_is_null<T>(value: *const T) -> bool;
```

## `core::ptr::mut_ptr`

Source: [`src/ptr/mut_ptr.vit`](src/ptr/mut_ptr.vit) · exports: 2

```vit
export proc mut_ptr<T>(value: &mut T) -> *mut T;
export proc mut_ptr_is_null<T>(value: *mut T) -> bool;
```

## `core::ptr::nonnull`

Source: [`src/ptr/nonnull.vit`](src/ptr/nonnull.vit) · exports: 8

```vit
export form NonNull<T> { pointer: *mut T }
export form NonNullConst<T> { pointer: *const T }
export proc nonnull<T>(pointer: *mut T) -> ?NonNull[T];
export proc nonnull_const<T>(pointer: *const T) -> ?NonNullConst[T];
export proc nonnull_as_ptr<T>(value: &NonNull[T]) -> *mut T;
export proc nonnull_const_as_ptr<T>(value: &NonNullConst[T]) -> *const T;
export proc nonnull_is_valid<T>(value: &NonNull[T]) -> bool;
export proc nonnull_const_is_valid<T>(value: &NonNullConst[T]) -> bool;
```

## `core::ptr::null`

Source: [`src/ptr/null.vit`](src/ptr/null.vit) · exports: 2

```vit
export proc null_const<T>() -> *const T;
export proc null_mut<T>() -> *mut T;
```

## `core::ptr::offset`

Source: [`src/ptr/offset.vit`](src/ptr/offset.vit) · exports: 1

```vit
export proc ptr_offset(pointer: *mut u8, offset: isize) -> *mut u8;
```

## `core::ptr::read`

Source: [`src/ptr/read.vit`](src/ptr/read.vit) · exports: 2

```vit
export proc ptr_read<T>(source: *const T) -> ?T;
export proc ptr_read_unchecked<T>(source: *const T) -> T;
```

## `core::ptr::volatile`

Source: [`src/ptr/volatile.vit`](src/ptr/volatile.vit) · exports: 2

```vit


```

## `core::ptr::write`

Source: [`src/ptr/write.vit`](src/ptr/write.vit) · exports: 2

```vit
export proc ptr_write<T>(destination: *mut T, value: T) -> bool;
export proc ptr_write_unchecked<T>(destination: *mut T, value: T);
```

## `core::slice::access`

Source: [`src/slice/access.vit`](src/slice/access.vit) · exports: 4

```vit
export proc slice_get<T>(value: &[T], index: usize) -> ?&T;
export proc slice_get_mut<T>(value: &mut [T], index: usize) -> ?&mut T;
export proc slice_first<T>(value: &[T]) -> ?&T;
export proc slice_last<T>(value: &[T]) -> ?&T;
```

## `core::slice::binary_search`

Source: [`src/slice/binary_search.vit`](src/slice/binary_search.vit) · exports: 2

```vit
export form BinarySearchResult {
    found: bool,
    index: usize,
}
export proc slice_binary_search<T>(
    value: &[T],
    target: &T,
    compare: proc(&T, &T) -> i8
) -> BinarySearchResult;
```

## `core::slice::chunks`

Source: [`src/slice/chunks.vit`](src/slice/chunks.vit) · exports: 3

```vit
export form SliceChunk {
    valid: bool,
    start: usize,
    length: usize,
}
export proc slice_chunk_count(length: usize, chunk_size: usize) -> usize;
export proc slice_chunk_at(length: usize, chunk_size: usize, chunk_index: usize) -> SliceChunk;
```

## `core::slice::iterator`

Source: [`src/slice/iterator.vit`](src/slice/iterator.vit) · exports: 4

```vit
export proc slice_cursor_init<T>(source: &[T], front: &mut usize, back: &mut usize);
export proc slice_cursor_next<T>(source: &[T], front: &mut usize, back: &mut usize, output: &mut T) -> bool;
export proc slice_cursor_next_back<T>(source: &[T], front: &mut usize, back: &mut usize, output: &mut T) -> bool;
export proc slice_cursor_remaining(front: usize, back: usize) -> usize;
```

## `core::slice`

Source: [`src/slice/mod.vit`](src/slice/mod.vit) · exports: 2

```vit
export proc slice_length<T>(value: &[T]) -> usize;
export proc slice_is_empty<T>(value: &[T]) -> bool;
```

## `core::slice::reverse`

Source: [`src/slice/reverse.vit`](src/slice/reverse.vit) · exports: 1

```vit
export proc slice_reverse<T>(value: &mut [T]);
```

## `core::slice::rotate`

Source: [`src/slice/rotate.vit`](src/slice/rotate.vit) · exports: 2

```vit
export proc slice_rotate_left<T>(value: &mut [T], amount: usize);
export proc slice_rotate_right<T>(value: &mut [T], amount: usize);
```

## `core::slice::search`

Source: [`src/slice/search.vit`](src/slice/search.vit) · exports: 3

```vit
export proc slice_find_index<T>(value: &[T], target: &T, equal: proc(&T, &T) -> bool) -> ?usize;
export proc slice_contains<T>(value: &[T], target: &T, equal: proc(&T, &T) -> bool) -> bool;
export proc slice_count<T>(value: &[T], target: &T, equal: proc(&T, &T) -> bool) -> usize;
```

## `core::slice::sort`

Source: [`src/slice/sort.vit`](src/slice/sort.vit) · exports: 2

```vit
export proc slice_insertion_sort<T>(value: &mut [T], compare: proc(&T, &T) -> i8);
export proc slice_is_sorted<T>(value: &[T], compare: proc(&T, &T) -> i8) -> bool;
```

## `core::slice::split`

Source: [`src/slice/split.vit`](src/slice/split.vit) · exports: 3

```vit
export form SplitPoint {
    found: bool,
    index: usize,
}
export proc slice_find_separator<T>(
    value: &[T],
    equal_separator: proc(&T) -> bool
) -> SplitPoint;
export proc slice_split_segment_end(length: usize, separator: SplitPoint) -> usize;
```

## `core::slice::windows`

Source: [`src/slice/windows.vit`](src/slice/windows.vit) · exports: 2

```vit
export proc slice_window_count(length: usize, window_size: usize) -> usize;
export proc slice_window_start(length: usize, window_size: usize, index: usize) -> ?usize;
```

## `core::str::bytes`

Source: [`src/str/bytes.vit`](src/str/bytes.vit) · exports: 4

```vit
export proc str_byte_length(value: str) -> usize;
export proc str_byte_at(value: str, index: usize) -> ?u8;
export proc str_copy_bytes(value: str, destination: &mut [u8]) -> usize;
export proc str_equal_bytes(left: str, right: str) -> bool;
```

## `core::str::chars`

Source: [`src/str/chars.vit`](src/str/chars.vit) · exports: 2

```vit
export proc str_char_count(bytes: &[u8]) -> ?usize;
export proc str_char_at(bytes: &[u8], character_index: usize) -> ?u32;
```

## `core::str::compare`

Source: [`src/str/compare.vit`](src/str/compare.vit) · exports: 4

```vit
export proc str_compare(left: str, right: str) -> i8;
export proc str_equal(left: str, right: str) -> bool;
export proc str_starts_with(value: str, prefix: str) -> bool;
export proc str_ends_with(value: str, suffix: str) -> bool;
```

## `core::str::iterator`

Source: [`src/str/iterator.vit`](src/str/iterator.vit) · exports: 3

```vit
export form Utf8Iterator {
    source: &[u8],
    offset: usize,
    failed: bool,
}
export proc utf8_iterator(source: &[u8]) -> Utf8Iterator;
export proc utf8_iterator_next(iterator: &mut Utf8Iterator) -> ?u32;
```

## `core::str`

Source: [`src/str/mod.vit`](src/str/mod.vit) · exports: 2

```vit
export proc str_is_empty(value: str) -> bool;
export proc str_length(value: str) -> usize;
```

## `core::str::parse`

Source: [`src/str/parse.vit`](src/str/parse.vit) · exports: 5

```vit
export proc str_parse_i64(value: str) -> ParseI64Result;
export proc str_parse_i64_radix(value: str, radix: u32) -> ParseI64Result;
export proc str_parse_u64(value: str) -> ParseU64Result;
export proc str_parse_u64_radix(value: str, radix: u32) -> ParseU64Result;
export proc str_parse_bool(value: str) -> ParseBoolResult;
```

## `core::str::search`

Source: [`src/str/search.vit`](src/str/search.vit) · exports: 3

```vit
export proc str_find_char(value: str, target: char, result_index: &mut usize) -> bool;
export proc str_find(value: str, needle: str, result_index: &mut usize) -> bool;
export proc str_contains(value: str, needle: str) -> bool;
```

## `core::str::split`

Source: [`src/str/split.vit`](src/str/split.vit) · exports: 3

```vit
export form StrSplitPoint {
    found: bool,
    start: usize,
    end: usize,
}
export proc str_split_once(value: str, separator: str) -> StrSplitPoint;
export proc str_split_count(value: str, separator: char) -> usize;
```

## `core::str::trim`

Source: [`src/str/trim.vit`](src/str/trim.vit) · exports: 3

```vit
export proc str_trim_start(value: str) -> str;
export proc str_trim_end(value: str) -> str;
export proc str_trim(value: str) -> str;
```

## `core::str::utf8`

Source: [`src/str/utf8.vit`](src/str/utf8.vit) · exports: 4

```vit
export pick Utf8Error {
    None,
    OutOfBounds,
    InvalidLeadingByte,
    InvalidContinuationByte,
    OverlongEncoding,
    Surrogate,
    OutOfRange,
}
export form Utf8DecodeResult {
    success: bool,
    codepoint: u32,
    width: usize,
    error: Utf8Error,
    error_index: usize,
}
export proc utf8_decode(bytes: &[u8], offset: usize) -> Utf8DecodeResult;
export proc utf8_validate(bytes: &[u8]) -> bool;
```

## `core::sync::atomic`

Source: [`src/sync/atomic.vit`](src/sync/atomic.vit) · exports: 39

```vit
export form AtomicUsize { value: usize }
export form AtomicU64 { value: u64 }
export form AtomicBool { value: bool }
export form Atomic<T> { value: T }
export proc atomic_new<T>(value: T) -> Atomic[T];
export proc atomic_load_value<T>(value: &Atomic[T], order: AtomicOrdering) -> T;
export proc atomic_store_value<T>(value: &mut Atomic[T], replacement: T, order: AtomicOrdering);
export proc atomic_exchange_value<T>(value: &mut Atomic[T], replacement: T, order: AtomicOrdering) -> T;
export proc atomic_compare_exchange_value<T>(value: &mut Atomic[T], expected: T, replacement: T, success: AtomicOrdering, failure: AtomicOrdering) -> bool;
export proc atomic_fetch_add_value<T>(value: &mut Atomic[T], amount: T, order: AtomicOrdering) -> T;
export proc atomic_fetch_sub_value<T>(value: &mut Atomic[T], amount: T, order: AtomicOrdering) -> T;
export proc atomic_usize(value: usize) -> AtomicUsize;
export proc atomic_u64(value: u64) -> AtomicU64;
export proc atomic_bool(value: bool) -> AtomicBool;
export proc atomic_usize_load(value: &AtomicUsize, order: AtomicOrdering) -> usize;
export proc atomic_usize_store(value: &mut AtomicUsize, replacement: usize, order: AtomicOrdering);
export proc atomic_usize_exchange(value: &mut AtomicUsize, replacement: usize, order: AtomicOrdering) -> usize;
export proc atomic_usize_compare_exchange(value: &mut AtomicUsize, expected: usize, replacement: usize, success: AtomicOrdering, failure: AtomicOrdering) -> bool;
export proc atomic_usize_fetch_add(value: &mut AtomicUsize, amount: usize, order: AtomicOrdering) -> usize;
export proc atomic_usize_fetch_sub(value: &mut AtomicUsize, amount: usize, order: AtomicOrdering) -> usize;
export proc atomic_usize_fetch_and(value: &mut AtomicUsize, mask: usize, order: AtomicOrdering) -> usize;
export proc atomic_usize_fetch_or(value: &mut AtomicUsize, mask: usize, order: AtomicOrdering) -> usize;
export proc atomic_usize_fetch_xor(value: &mut AtomicUsize, mask: usize, order: AtomicOrdering) -> usize;
export proc atomic_u64_load(value: &AtomicU64, order: AtomicOrdering) -> u64;
export proc atomic_u64_store(value: &mut AtomicU64, replacement: u64, order: AtomicOrdering);
export proc atomic_u64_exchange(value: &mut AtomicU64, replacement: u64, order: AtomicOrdering) -> u64;
export proc atomic_u64_compare_exchange(value: &mut AtomicU64, expected: u64, replacement: u64, success: AtomicOrdering, failure: AtomicOrdering) -> bool;
export proc atomic_u64_fetch_add(value: &mut AtomicU64, amount: u64, order: AtomicOrdering) -> u64;
export proc atomic_u64_fetch_sub(value: &mut AtomicU64, amount: u64, order: AtomicOrdering) -> u64;
export proc atomic_u64_fetch_and(value: &mut AtomicU64, mask: u64, order: AtomicOrdering) -> u64;
export proc atomic_u64_fetch_or(value: &mut AtomicU64, mask: u64, order: AtomicOrdering) -> u64;
export proc atomic_u64_fetch_xor(value: &mut AtomicU64, mask: u64, order: AtomicOrdering) -> u64;
export proc atomic_bool_load(value: &AtomicBool, order: AtomicOrdering) -> bool;
export proc atomic_bool_store(value: &mut AtomicBool, replacement: bool, order: AtomicOrdering);
export proc atomic_bool_exchange(value: &mut AtomicBool, replacement: bool, order: AtomicOrdering) -> bool;
export proc atomic_bool_compare_exchange(value: &mut AtomicBool, expected: bool, replacement: bool, success: AtomicOrdering, failure: AtomicOrdering) -> bool;
export proc atomic_bool_test_and_set(value: &mut AtomicBool, order: AtomicOrdering) -> bool;
export proc atomic_bool_clear(value: &mut AtomicBool, order: AtomicOrdering);
export proc atomic_thread_fence(order: AtomicOrdering);
```

## `core::sync::once`

Source: [`src/sync/once.vit`](src/sync/once.vit) · exports: 5

```vit
export form Once {
    state: u8,
}
export proc once_new() -> Once;
export proc once_is_complete(value: &Once) -> bool;
export proc once_is_running(value: &Once) -> bool;
export proc once_call(value: &mut Once, initializer: proc() -> bool) -> bool;
```

## `core::sync::ordering`

Source: [`src/sync/ordering.vit`](src/sync/ordering.vit) · exports: 6

```vit
export pick AtomicOrdering { Relaxed, Acquire, Release, AcqRel, SeqCst }
export proc atomic_ordering_is_valid(order: AtomicOrdering) -> bool;
export proc atomic_failure_order_is_valid(order: AtomicOrdering) -> bool;
export proc atomic_load_order_is_valid(order: AtomicOrdering) -> bool;
export proc atomic_store_order_is_valid(order: AtomicOrdering) -> bool;
export proc atomic_compare_exchange_orders_are_valid(
    success: AtomicOrdering,
    failure: AtomicOrdering
) -> bool;
```

## `core::sync::spinlock`

Source: [`src/sync/spinlock.vit`](src/sync/spinlock.vit) · exports: 8

```vit
export form SpinLock<T> {
    locked: u8,
    value: T,
}
export proc spinlock<T>(value: T) -> SpinLock[T];
export proc spinlock_try_lock<T>(lock: &mut SpinLock[T]) -> bool;
export proc spinlock_lock<T>(lock: &mut SpinLock[T]);
export proc spinlock_unlock<T>(lock: &mut SpinLock[T]);
export proc spinlock_try_unlock<T>(lock: &mut SpinLock[T]) -> bool;
export proc spinlock_is_locked<T>(lock: &SpinLock[T]) -> bool;
export proc spinlock_value_mut<T>(lock: &mut SpinLock[T]) -> &mut T;
```

## `core::traits::arithmetic`

Source: [`src/traits/arithmetic.vit`](src/traits/arithmetic.vit) · exports: 11

```vit
export trait Add<Receiver, Rhs, Output> {
    proc add(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait Sub<Receiver, Rhs, Output> {
    proc sub(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait Mul<Receiver, Rhs, Output> {
    proc mul(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait Div<Receiver, Rhs, Output> {
    proc div(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait Rem<Receiver, Rhs, Output> {
    proc rem(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait Neg<Receiver, Output> {
    proc neg(receiver: &Receiver) -> Output;
}
export trait AddAssign<Receiver, Rhs> {
    proc add_assign(receiver: &mut Receiver, rhs: Rhs);
}
export trait SubAssign<Receiver, Rhs> {
    proc sub_assign(receiver: &mut Receiver, rhs: Rhs);
}
export trait MulAssign<Receiver, Rhs> {
    proc mul_assign(receiver: &mut Receiver, rhs: Rhs);
}
export trait DivAssign<Receiver, Rhs> {
    proc div_assign(receiver: &mut Receiver, rhs: Rhs);
}
export trait RemAssign<Receiver, Rhs> {
    proc rem_assign(receiver: &mut Receiver, rhs: Rhs);
}
```

## `core::traits::bitwise`

Source: [`src/traits/bitwise.vit`](src/traits/bitwise.vit) · exports: 11

```vit
export trait BitAnd<Receiver, Rhs, Output> {
    proc bitand(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait BitOr<Receiver, Rhs, Output> {
    proc bitor(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait BitXor<Receiver, Rhs, Output> {
    proc bitxor(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait Shl<Receiver, Rhs, Output> {
    proc shl(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait Shr<Receiver, Rhs, Output> {
    proc shr(receiver: &Receiver, rhs: Rhs) -> Output;
}
export trait Not<Receiver, Output> {
    proc bitnot(receiver: &Receiver) -> Output;
}
export trait BitAndAssign<Receiver, Rhs> {
    proc bitand_assign(receiver: &mut Receiver, rhs: Rhs);
}
export trait BitOrAssign<Receiver, Rhs> {
    proc bitor_assign(receiver: &mut Receiver, rhs: Rhs);
}
export trait BitXorAssign<Receiver, Rhs> {
    proc bitxor_assign(receiver: &mut Receiver, rhs: Rhs);
}
export trait ShlAssign<Receiver, Rhs> {
    proc shl_assign(receiver: &mut Receiver, rhs: Rhs);
}
export trait ShrAssign<Receiver, Rhs> {
    proc shr_assign(receiver: &mut Receiver, rhs: Rhs);
}
```

## `core::traits::callable`

Source: [`src/traits/callable.vit`](src/traits/callable.vit) · exports: 3

```vit
export trait Callable<Receiver, Arguments, Output> {
    proc call(receiver: &Receiver, arguments: Arguments) -> Output;
}
export trait CallableMut<Receiver, Arguments, Output> {
    proc call_mut(receiver: &mut Receiver, arguments: Arguments) -> Output;
}
export trait CallableOnce<Receiver, Arguments, Output> {
    proc call_once(receiver: Receiver, arguments: Arguments) -> Output;
}
```

## `core::traits::clone`

Source: [`src/traits/clone.vit`](src/traits/clone.vit) · exports: 1

```vit
export trait Clone<Receiver> {
    proc clone(receiver: &Receiver) -> Receiver;
}
```

## `core::traits::compare`

Source: [`src/traits/compare.vit`](src/traits/compare.vit) · exports: 2

```vit
export trait PartialOrd<Receiver, Rhs> {
    proc partial_cmp(receiver: &Receiver, other: &Rhs) -> PartialOrdering;
}
export trait Ord<Receiver, Rhs> {
    proc cmp(receiver: &Receiver, other: &Rhs) -> Ordering;
}
```

## `core::traits::convert`

Source: [`src/traits/convert.vit`](src/traits/convert.vit) · exports: 4

```vit
export trait From<T, Receiver> {
    proc from(value: T) -> Receiver;
}
export trait Into<Receiver, T> {
    proc into(receiver: Receiver) -> T;
}
export trait TryFrom<T, Receiver, Error> {
    proc try_from(value: T) -> Result[Receiver, Error];
}
export trait TryInto<Receiver, T, Error> {
    proc try_into(receiver: Receiver) -> Result[T, Error];
}
```

## `core::traits::debug`

Source: [`src/traits/debug.vit`](src/traits/debug.vit) · exports: 1

```vit
export trait Debug<Receiver> {
    proc fmt(receiver: &Receiver, formatter: &mut Formatter) -> bool;
}
```

## `core::traits::default`

Source: [`src/traits/default.vit`](src/traits/default.vit) · exports: 1

```vit
export trait Default<Receiver> {
    proc default() -> Receiver;
}
```

## `core::traits::dereference`

Source: [`src/traits/dereference.vit`](src/traits/dereference.vit) · exports: 2

```vit
export trait Deref<Receiver, Target> {
    proc deref(receiver: &Receiver) -> &Target;
}
export trait DerefMut<Receiver, Target> {
    proc deref_mut(receiver: &mut Receiver) -> &mut Target;
}
```

## `core::traits::display`

Source: [`src/traits/display.vit`](src/traits/display.vit) · exports: 1

```vit
export trait Display<Receiver> {
    proc fmt(receiver: &Receiver, formatter: &mut Formatter) -> bool;
}
```

## `core::traits::equal`

Source: [`src/traits/equal.vit`](src/traits/equal.vit) · exports: 2

```vit
export trait PartialEq<Receiver, Rhs> {
    proc eq(receiver: &Receiver, other: &Rhs) -> bool;
}
export trait Eq<Receiver> {}
```

## `core::traits::hash`

Source: [`src/traits/hash.vit`](src/traits/hash.vit) · exports: 1

```vit
export trait Hash<Receiver> {
    proc hash(receiver: &Receiver, state: &mut u64);
}
```

## `core::traits::index`

Source: [`src/traits/index.vit`](src/traits/index.vit) · exports: 2

```vit
export trait Index<Receiver, IndexType, Output> {
    proc index(receiver: &Receiver, index: IndexType) -> &Output;
}
export trait IndexMut<Receiver, IndexType, Output> {
    proc index_mut(receiver: &mut Receiver, index: IndexType) -> &mut Output;
}
```

## `core::traits::iterator`

Source: [`src/traits/iterator.vit`](src/traits/iterator.vit) · exports: 2

```vit
export trait Iterator<Receiver, Item> {
    proc next(iterator: &mut Receiver) -> ?Item;
}
export trait DoubleEndedIterator<Receiver, Item> {
    proc next_back(iterator: &mut Receiver) -> ?Item;
}
```

## `core::types::array`

Source: [`src/types/array.vit`](src/types/array.vit) · exports: 2

```vit
export type FixedArray<T, const N: usize> = [T; N];
export proc fixed_array_length<T, const N: usize>() -> usize;
```

## `core::types::marker`

Source: [`src/types/marker.vit`](src/types/marker.vit) · exports: 1

```vit
export type Marker = [u8;
```

## `core::types::never`

Source: [`src/types/never.vit`](src/types/never.vit) · exports: 1

```vit
export type Never = never;
```

## `core::types::option`

Source: [`src/types/option.vit`](src/types/option.vit) · exports: 8

```vit
export type Option<T> = ?T;
export proc option_none<T>() -> Option[T];
export proc option_some<T>(value: T) -> Option[T];
export proc option_is_some<T>(value: Option[T]) -> bool;
export proc option_is_none<T>(value: Option[T]) -> bool;
export proc option_or<T>(value: Option[T], fallback: Option[T]) -> Option[T];
export proc option_or_else<T>(
    value: Option[T],
    fallback: proc() -> Option[T]
) -> Option[T];
export proc option_replace<T>(value: &mut Option[T], replacement: Option[T]) -> Option[T];
```

## `core::types::ordering`

Source: [`src/types/ordering.vit`](src/types/ordering.vit) · exports: 16

```vit
export pick Ordering {
    Less,
    Equal,
    Greater,
}
export pick PartialOrdering {
    Less,
    Equal,
    Greater,
    Unordered,
}
export proc ordering_is_less(value: Ordering) -> bool;
export proc ordering_is_equal(value: Ordering) -> bool;
export proc ordering_is_greater(value: Ordering) -> bool;
export proc ordering_is_less_or_equal(value: Ordering) -> bool;
export proc ordering_is_greater_or_equal(value: Ordering) -> bool;
export proc ordering_reverse(value: Ordering) -> Ordering;
export proc partial_ordering_is_less(value: PartialOrdering) -> bool;
export proc partial_ordering_is_equal(value: PartialOrdering) -> bool;
export proc partial_ordering_is_greater(value: PartialOrdering) -> bool;
export proc partial_ordering_is_unordered(value: PartialOrdering) -> bool;
export proc partial_ordering_is_ordered(value: PartialOrdering) -> bool;
export proc partial_ordering_is_less_or_equal(value: PartialOrdering) -> bool;
export proc partial_ordering_is_greater_or_equal(value: PartialOrdering) -> bool;
export proc partial_ordering_reverse(value: PartialOrdering) -> PartialOrdering;
```

## `core::types::phantom`

Source: [`src/types/phantom.vit`](src/types/phantom.vit) · exports: 1

```vit
export type PhantomData<T> = [T;
```

## `core::types::range`

Source: [`src/types/range.vit`](src/types/range.vit) · exports: 10

```vit
export form Range {
    start: usize,
    end: usize,
}
export proc range(start: usize, end: usize) -> Range;
export proc range_empty_at(position: usize) -> Range;
export proc range_is_valid(value: &Range) -> bool;
export proc range_is_empty(value: &Range) -> bool;
export proc range_len(value: &Range) -> usize;
export proc range_contains(value: &Range, index: usize) -> bool;
export proc range_contains_range(outer: &Range, inner: &Range) -> bool;
export proc range_overlaps(left: &Range, right: &Range) -> bool;
export proc range_intersection(left: &Range, right: &Range) -> ?Range;
```

## `core::types::result`

Source: [`src/types/result.vit`](src/types/result.vit) · exports: 6

```vit
export form Result<T, E> {
    success: bool,
    value: ?T,
    error: ?E,
}
export proc result_is_ok<T, E>(result: &Result[T, E]) -> bool;
export proc result_is_err<T, E>(result: &Result[T, E]) -> bool;
export proc result_value<T, E>(result: &Result[T, E]) -> ?T;
export proc result_error<T, E>(result: &Result[T, E]) -> ?E;
export proc result_is_valid<T, E>(result: &Result[T, E]) -> bool;
```

## `core::types::slice`

Source: [`src/types/slice.vit`](src/types/slice.vit) · exports: 3

```vit
export type Slice<T> = [T];
export proc slice_length<T>(value: &[T]) -> usize;
export proc slice_is_empty<T>(value: &[T]) -> bool;
```

## `core::types::tuple`

Source: [`src/types/tuple.vit`](src/types/tuple.vit) · exports: 3

```vit
export form Pair<T, U> {
    first: T,
    second: U,
}
export form Triple<T, U, V> {
    first: T,
    second: U,
    third: V,
}
export form Quad<T, U, V, W> {
    first: T,
    second: U,
    third: V,
    fourth: W,
}
```

## `vitte::core::memory`

Source: [`src/vitte/core/memory.vit`](src/vitte/core/memory.vit) · exports: 9

```vit
export intrinsic native_allocate(size: usize) -> *mut u8;
export intrinsic native_deallocate(pointer: *mut u8);
export proc allocate(size: usize) -> *mut u8;
export proc deallocate(pointer: *mut u8);
export proc fill(destination: *mut u8, value: u8, length: usize) -> bool;
export proc copy(destination: *mut u8, source: *const u8, length: usize) -> bool;
export proc cstring_length(value: *const u8) -> usize;
export proc copy_string_bytes(destination: *mut u8, source: string, length: usize) -> bool;
export proc format(format_text: string, arguments: [string]) -> string;
```

## `vitte::core::pointer`

Source: [`src/vitte/core/pointer.vit`](src/vitte/core/pointer.vit) · exports: 8

```vit
export type uintptr = usize;
export proc is_null<T>(value: *const T) -> bool;
export proc address(value: *const u8) -> uintptr;
export proc from_address_mut(value: uintptr) -> *mut u8;
export proc add_bytes(value: *mut u8, offset: usize) -> *mut u8;
export proc add_bytes_const(value: *const u8, offset: usize) -> *const u8;
export proc read<T>(source: *const T) -> T;
export proc write<T>(destination: *mut T, value: T);
```
