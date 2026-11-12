const PREC = {
  assign: 1,
  coalesce: 2,
  or: 3,
  and: 4,
  bitOr: 5,
  bitXor: 6,
  bitAnd: 7,
  equality: 8,
  compare: 9,
  shift: 10,
  add: 11,
  multiply: 12,
  range: 13,
  unary: 14,
  call: 15,
  member: 16,
};

module.exports = grammar({
  name: 'vitte',

  extras: $ => [/\s/, $.comment, $.block_comment],

  word: $ => $.identifier,

  externals: $ => [$.trailing_decimal_float],

  conflicts: $ => [
    [$.primary_expression, $.generic_variant_path],
    [$.defer_statement, $.primary_expression],
    [$.match_arm, $.primary_expression],
  ],

  rules: {
    source_file: $ => repeat($.declaration),

    declaration: $ => seq(repeat($.modifier), $.declaration_body),

    modifier: _ => choice('pub', 'export', 'async', 'unsafe', 'comptime'),

    declaration_body: $ => choice(
      $.space_declaration,
      $.use_declaration,
      $.export_declaration,
      $.value_declaration,
      $.type_declaration,
      $.opaque_declaration,
      $.form_declaration,
      $.pick_declaration,
      $.trait_declaration,
      $.implementation_declaration,
      $.procedure_declaration,
      $.external_procedure_declaration,
      $.intrinsic_declaration,
      $.macro_declaration,
      $.test_declaration
    ),

    space_declaration: $ => seq('space', $.path, choice(';', $.block)),

    use_declaration: $ => seq(
      'use',
      $.module_path,
      optional(choice(
        seq('as', $.identifier),
        seq(choice('::', '.'), choice('*', seq('{', commaSep1Trailing($.path_alias), '}')))
      )),
      ';'
    ),

    path_alias: $ => seq($.path, optional(seq('as', $.identifier))),

    export_declaration: $ => seq(
      'export',
      choice('*', $.path_alias, seq('{', commaSep1Trailing($.path_alias), '}')),
      ';'
    ),

    value_declaration: $ => seq(
      choice('const', 'static'),
      $.identifier,
      optional(seq(':', $.type)),
      '=',
      $.expression,
      ';'
    ),

    type_declaration: $ => seq(
      'type',
      $.identifier,
      optional($.generic_parameters),
      optional($.where_clause),
      '=',
      $.type,
      ';'
    ),

    opaque_declaration: $ => seq(
      'opaque',
      $.identifier,
      optional($.generic_parameters),
      optional($.where_clause),
      ';'
    ),

    form_declaration: $ => seq(
      'form',
      $.identifier,
      optional($.generic_parameters),
      optional($.where_clause),
      '{',
      optional($.field_list),
      '}'
    ),

    field_list: $ => seq(
      $.field,
      repeat(seq(choice(',', ';'), $.field)),
      optional(choice(',', ';'))
    ),

    field: $ => seq($.identifier, ':', $.type),

    pick_declaration: $ => seq(
      'pick',
      $.identifier,
      optional($.generic_parameters),
      '{',
      optional(commaSep1Trailing($.variant)),
      '}'
    ),

    variant: $ => seq(
      $.identifier,
      optional(seq('(', commaSep1($.type), ')'))
    ),

    trait_declaration: $ => seq(
      'trait',
      $.identifier,
      optional($.generic_parameters),
      optional($.where_clause),
      $.declaration_block
    ),

    implementation_declaration: $ => seq(
      'impl',
      optional($.generic_parameters),
      $.type,
      optional(seq('for', $.type)),
      optional($.where_clause),
      $.declaration_block
    ),

    procedure_declaration: $ => seq(
      'proc',
      $.identifier,
      optional($.generic_parameters),
      $.parameter_list,
      optional(seq('->', $.type)),
      optional($.where_clause),
      repeat($.contract_clause),
      $.block
    ),

    contract_clause: $ => seq(choice('requires', 'ensures'), $.expression, ';'),

    external_procedure_declaration: $ => seq(
      'extern',
      optional($.string_literal),
      'proc',
      $.identifier,
      optional($.generic_parameters),
      $.parameter_list,
      optional(seq('->', $.type)),
      optional($.where_clause),
      ';'
    ),

    intrinsic_declaration: $ => seq(
      'intrinsic',
      $.identifier,
      optional($.generic_parameters),
      $.parameter_list,
      optional(seq('->', $.type)),
      ';'
    ),

    macro_declaration: $ => seq(
      'macro',
      $.identifier,
      optional($.parameter_list),
      $.block
    ),

    test_declaration: $ => seq('test', optional(choice($.identifier, $.string_literal)), $.block),

    declaration_block: $ => seq('{', repeat($.declaration), '}'),

    generic_parameters: $ => seq(
      '<',
      optional(commaSep1($.generic_parameter)),
      '>'
    ),

    generic_parameter: $ => choice(
      seq($.identifier, optional(seq(':', $.type, repeat(seq('+', $.type))))),
      seq('const', $.identifier, ':', $.type)
    ),

    where_clause: $ => seq(
      'where',
      $.type,
      ':',
      $.type,
      repeat(seq('+', $.type)),
      repeat(seq(',', $.type, ':', $.type, repeat(seq('+', $.type))))
    ),

    parameter_list: $ => seq('(', optional(commaSep1Trailing($.parameter)), ')'),

    parameter: $ => seq($.identifier, ':', $.type),

    type: $ => choice(
      $.reference_type,
      $.pointer_type,
      $.array_type,
      $.optional_type,
      $.path_type
    ),

    reference_type: $ => seq(choice('&', 'ref'), optional('mut'), $.type),

    optional_type: $ => seq('?', $.type),

    pointer_type: $ => seq('*', $.type),

    array_type: $ => seq('[', $.expression, ']', $.type),

    path_type: $ => seq(
      $.path,
      optional(choice(
        seq('<', optional(commaSep1($.type)), '>'),
        seq('[', optional(commaSep1($.type)), ']')
      ))
    ),

    path: $ => prec.right(seq($.identifier, repeat(seq(choice('::', '/'), $.identifier)))),

    module_path: _ => token(/[A-Za-z_\u0080-\uFFFF][A-Za-z0-9_\u0080-\uFFFF]*(?:(?:::|\/)[A-Za-z_\u0080-\uFFFF][A-Za-z0-9_\u0080-\uFFFF]*)*/),

    block: $ => seq('{', repeat($.statement), '}'),

    statement: $ => choice(
      $.declaration,
      $.let_statement,
      $.set_statement,
      $.return_statement,
      $.jump_statement,
      $.defer_statement,
      $.assert_statement,
      $.if_statement,
      $.while_statement,
      $.loop_statement,
      $.for_statement,
      $.match_statement,
      $.unsafe_statement,
      $.assembly_statement,
      $.expression_statement
    ),

    let_statement: $ => seq(
      'let',
      optional('mut'),
      $.identifier,
      optional(seq(':', $.type)),
      optional(seq('=', $.expression)),
      ';'
    ),

    set_statement: $ => seq('set', $.expression, ';'),

    return_statement: $ => seq(choice('return', 'give'), optional($.expression), ';'),

    jump_statement: _ => seq(choice('break', 'continue'), ';'),

    defer_statement: $ => seq('defer', choice($.block, seq($.expression, ';'))),

    assert_statement: $ => prec(1, seq('assert', choice(
      seq('(', $.expression, ')'),
      $.expression
    ), ';')),

    if_statement: $ => seq(
      'if',
      $.expression,
      $.block,
      optional(seq('else', choice($.if_statement, $.block)))
    ),

    while_statement: $ => seq('while', $.expression, $.block),

    loop_statement: $ => seq('loop', $.block),

    for_statement: $ => seq('for', $.identifier, 'in', $.expression, $.block),

    match_statement: $ => seq(
      'match',
      $.expression,
      '{',
      optional(commaSep1Trailing($.match_arm)),
      '}'
    ),

    match_arm: $ => seq($.expression, '=>', choice($.block, $.expression)),

    unsafe_statement: $ => seq('unsafe', $.block),

    assembly_statement: $ => seq(
      'asm',
      choice(
        seq('(', optional(commaSep1($.expression)), ')'),
        $.string_literal
      ),
      ';'
    ),

    expression_statement: $ => seq($.expression, optional(';')),

    expression: $ => choice(
      $.assignment_expression,
      $.coalesce_expression
    ),

    assignment_expression: $ => prec.right(PREC.assign, seq(
      $.coalesce_expression,
      choice('=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '<<=', '>>='),
      $.expression
    )),

    coalesce_expression: $ => choice(
      prec.right(PREC.coalesce, seq($.logical_or_expression, '??', $.coalesce_expression)),
      $.logical_or_expression
    ),

    logical_or_expression: $ => binaryChain($, $.logical_and_expression, ['or', '||'], PREC.or),
    logical_and_expression: $ => binaryChain($, $.bitwise_or_expression, ['and', '&&'], PREC.and),
    bitwise_or_expression: $ => binaryChain($, $.bitwise_xor_expression, ['|'], PREC.bitOr),
    bitwise_xor_expression: $ => binaryChain($, $.bitwise_and_expression, ['^'], PREC.bitXor),
    bitwise_and_expression: $ => binaryChain($, $.equality_expression, ['&'], PREC.bitAnd),
    equality_expression: $ => binaryChain($, $.comparison_expression, ['==', '!='], PREC.equality),
    comparison_expression: $ => binaryChain($, $.shift_expression, ['<', '<=', '>', '>='], PREC.compare),
    shift_expression: $ => binaryChain($, $.additive_expression, ['<<', '>>'], PREC.shift),
    additive_expression: $ => binaryChain($, $.multiplicative_expression, ['+', '-'], PREC.add),
    multiplicative_expression: $ => binaryChain($, $.range_expression, ['*', '/', '%'], PREC.multiply),
    range_expression: $ => binaryChain($, $.unary_expression, ['..', '..='], PREC.range),

    unary_expression: $ => choice(
      prec(PREC.unary, seq(choice('+', '-', '!', '~', '*', '&', 'not', 'move', 'ref', 'await'), $.unary_expression)),
      $.postfix_expression
    ),

    postfix_expression: $ => choice(
      $.primary_expression,
      prec.left(PREC.call, seq($.postfix_expression, '(', optional(commaSep1Trailing($.expression)), ')')),
      prec.left(PREC.member, seq($.postfix_expression, '[', $.expression, ']')),
      prec.left(PREC.member, seq($.postfix_expression, '.', $.identifier)),
      prec.left(PREC.member, seq($.postfix_expression, '?'))
    ),

    primary_expression: $ => choice(
      $.literal,
      $.path,
      $.generic_variant_path,
      'self',
      $.array_literal,
      $.parenthesized_expression,
      $.block
    ),

    generic_variant_path: $ => prec.dynamic(1, seq($.path, '<', optional(commaSep1($.type)), '>', '::', $.identifier)),

    array_literal: $ => seq('[', optional(commaSep1Trailing($.expression)), ']'),

    parenthesized_expression: $ => seq('(', $.expression, ')'),

    literal: $ => choice(
      $.float_literal,
      $.integer_literal,
      $.string_literal,
      $.character_literal,
      'true',
      'false',
      'null'
    ),

    escape_sequence: _ => token(/\\(?:\\|'|"|0|a|b|f|n|r|t|v|x[0-9a-fA-F]{2}|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8})/),

    integer_literal: _ => token(choice(
      /[0-9](?:_?[0-9])*/,
      /0[bB][01](?:_?[01])*/,
      /0[oO][0-7](?:_?[0-7])*/,
      /0[xX][0-9a-fA-F](?:_?[0-9a-fA-F])*/
    )),

    float_literal: $ => choice(
      token(/[0-9](?:_?[0-9])*\.[0-9](?:_?[0-9])*(?:[eE][+-]?[0-9](?:_?[0-9])*)?/),
      $.trailing_decimal_float,
      token(/[0-9](?:_?[0-9])*(?:[eE][+-]?[0-9](?:_?[0-9])*)/),
      $._hexadecimal_float
    ),

    _hexadecimal_float: _ => token(/0[xX][0-9a-fA-F](?:_?[0-9a-fA-F])*(?:\.(?:[0-9a-fA-F](?:_?[0-9a-fA-F])*)?)?[pP][+-]?[0-9](?:_?[0-9])*/),

    string_literal: $ => seq('"', repeat(choice(/[^"\\\r\n]/, $.escape_sequence)), '"'),

    character_literal: $ => seq("'", choice(/[^'\\\r\n]/, $.escape_sequence), "'"),

    identifier: _ => /[A-Za-z_\u0080-\uFFFF][A-Za-z0-9_\u0080-\uFFFF]*/,

    comment: _ => token(seq('//', /[^\r\n]*/)),

    block_comment: $ => seq(
      '/*',
      repeat(choice($.block_comment, /[^*]/, /\*+[^*/]/)),
      /\*+/,
      '/'
    ),

  }
});

function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

function commaSep1Trailing(rule) {
  return seq(rule, repeat(seq(',', rule)), optional(','));
}

function binaryChain($, rule, operators, precedence) {
  return prec.left(precedence, seq(rule, repeat(seq(choice(...operators), rule))));
}
