/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

/**
 * Tree-sitter grammar for the Dux programming language.
 *
 * Covers: literals, types, expressions, statements, and top-level declarations.
 * Operator precedence follows C / Python conventions.
 */

const PREC = {
  // Lowest to highest
  ASSIGN:   1,
  OR:       2,
  AND:      3,
  NOT:      4,
  CMP:      5,
  BIT_OR:   6,
  BIT_XOR:  7,
  BIT_AND:  8,
  SHIFT:    9,
  ADD:     10,
  MUL:     11,
  UNARY:   12,
  AWAIT:   13,
  CALL:    14,
  INDEX:   15,
  MEMBER:  16,
};

module.exports = grammar({
  name: "dux",

  // Extras: whitespace and comments are ignored everywhere.
  extras: ($) => [/\s/, $.line_comment, $.block_comment],

  // Inline rules (no CST node emitted).
  inline: ($) => [$._statement, $._expression, $._type, $._literal],

  // Conflicts that are resolved by GLR / precedence.
  conflicts: ($) => [
    // An expression_statement beginning with an identifier can look like the
    // start of a typed variable_declaration before the parser sees the second
    // identifier.
    [$._expression, $.variable_declaration],
    // A call_expression vs a parenthesized_expression at the start of a line.
    [$.call_expression, $.parenthesized_expression],
  ],

  word: ($) => $.identifier,

  rules: {
    // -----------------------------------------------------------------------
    // Top-level
    // -----------------------------------------------------------------------

    source_file: ($) => repeat($._top_level_item),

    _top_level_item: ($) =>
      choice(
        $.function_declaration,
        $.class_declaration,
        $.interface_declaration,
        $.enum_declaration,
        $.namespace_declaration,
        $.import_declaration,
        $.extern_declaration,
        $._statement
      ),

    // -----------------------------------------------------------------------
    // Comments
    // -----------------------------------------------------------------------

    line_comment: (_) => token(seq("//", /.*/)),

    block_comment: (_) =>
      token(seq("/*", /[^*]*\*+([^/*][^*]*\*+)*/, "/")),

    // -----------------------------------------------------------------------
    // Identifiers
    // -----------------------------------------------------------------------

    identifier: (_) => /[A-Za-z_][A-Za-z0-9_]*/,

    // -----------------------------------------------------------------------
    // Literals
    // -----------------------------------------------------------------------

    _literal: ($) =>
      choice(
        $.integer_literal,
        $.float_literal,
        $.hex_literal,
        $.binary_literal,
        $.string_literal,
        $.f_string,
        $.bool_literal,
        $.null_literal
      ),

    integer_literal: (_) => token(/[0-9]+/),

    float_literal: (_) =>
      token(
        seq(
          /[0-9]+/,
          ".",
          /[0-9]+/,
          optional(seq(/[eE]/, optional(/[+-]/), /[0-9]+/))
        )
      ),

    hex_literal: (_) => token(seq("0", /[xX]/, /[0-9A-Fa-f]+/)),

    binary_literal: (_) => token(seq("0", /[bB]/, /[01]+/)),

    // Plain string: double-quoted or single-quoted with escape sequences.
    string_literal: (_) =>
      choice(
        seq('"', repeat(choice(/[^"\\]+/, /\\./)), '"'),
        seq("'", repeat(choice(/[^'\\]+/, /\\./)), "'")
      ),

    // f-string: f"..." or f'...' — the body is parsed for interpolations.
    f_string: ($) =>
      choice(
        seq(
          'f"',
          repeat(choice($.f_string_content_double, $.interpolation)),
          '"'
        ),
        seq(
          "f'",
          repeat(choice($.f_string_content_single, $.interpolation)),
          "'"
        )
      ),

    f_string_content_double: (_) =>
      token(prec(-1, /[^"\\{]+/)),

    f_string_content_single: (_) =>
      token(prec(-1, /[^'\\{]+/)),

    interpolation: ($) =>
      seq("{", field("expression", $._expression), "}"),

    bool_literal: (_) => choice("true", "false"),

    null_literal: (_) => "null",

    // -----------------------------------------------------------------------
    // Types
    // -----------------------------------------------------------------------

    _type: ($) =>
      choice(
        $.primitive_type,
        $.generic_type,
        $.pointer_type,
        $.tuple_type,
        $.type_identifier
      ),

    primitive_type: (_) =>
      choice(
        "void",
        "int",
        "long",
        "real",
        "double",
        "str",
        "bool",
        "list",
        "dict",
        "tuple",
        "object",
        "auto"
      ),

    // ptr<T>
    pointer_type: ($) =>
      seq("ptr", "<", field("type", $._type), ">"),

    // list<T>, dict<K,V>, etc.
    generic_type: ($) =>
      seq(
        field("name", $.type_identifier),
        "<",
        commaSep1($._type),
        ">"
      ),

    // tuple(T1, T2, ...)
    tuple_type: ($) =>
      seq("tuple", "(", commaSep1($._type), ")"),

    // User-defined type name (PascalCase by convention but not enforced here)
    type_identifier: (_) => /[A-Za-z_][A-Za-z0-9_]*/,

    // -----------------------------------------------------------------------
    // Expressions
    // -----------------------------------------------------------------------

    _expression: ($) =>
      choice(
        $._literal,
        $.identifier,
        $.this_expression,
        $.super_expression,
        $.unary_expression,
        $.binary_expression,
        $.assignment_expression,
        $.call_expression,
        $.index_expression,
        $.member_expression,
        $.new_expression,
        $.await_expression,
        $.parenthesized_expression,
        $.f_string,
        $.list_literal,
        $.dict_literal,
        $.tuple_literal
      ),

    this_expression: (_) => "this",
    super_expression: (_) => "super",

    // Unary operators
    unary_expression: ($) =>
      prec(PREC.UNARY, choice(
        seq("-",   field("operand", $._expression)),
        seq("!",   field("operand", $._expression)),
        seq("~",   field("operand", $._expression)),
        seq("not", field("operand", $._expression))
      )),

    // Binary operators (arithmetic, comparison, logic, bitwise)
    binary_expression: ($) =>
      choice(
        ...[
          ["||",  PREC.OR],
          ["or",  PREC.OR],
          ["&&",  PREC.AND],
          ["and", PREC.AND],
          ["|",   PREC.BIT_OR],
          ["^",   PREC.BIT_XOR],
          ["&",   PREC.BIT_AND],
          ["==",  PREC.CMP],
          ["!=",  PREC.CMP],
          ["<",   PREC.CMP],
          [">",   PREC.CMP],
          ["<=",  PREC.CMP],
          [">=",  PREC.CMP],
          ["<<",  PREC.SHIFT],
          [">>",  PREC.SHIFT],
          ["+",   PREC.ADD],
          ["-",   PREC.ADD],
          ["*",   PREC.MUL],
          ["/",   PREC.MUL],
          ["%",   PREC.MUL],
        ].map(([op, prec_level]) =>
          prec.left(prec_level, seq(
            field("left",     $._expression),
            field("operator", op),
            field("right",    $._expression)
          ))
        )
      ),

    // Assignment (right-associative)
    assignment_expression: ($) =>
      prec.right(PREC.ASSIGN, seq(
        field("left",     $._expression),
        field("operator", choice("=", "+=", "-=", "*=", "/=", "%=",
                                 "&=", "|=", "^=", "<<=", ">>=")),
        field("right",    $._expression)
      )),

    // Function / method call
    call_expression: ($) =>
      prec(PREC.CALL, seq(
        field("function",  $._expression),
        field("arguments", $.argument_list)
      )),

    argument_list: ($) =>
      seq("(", commaSep($._expression), ")"),

    // Index: expr[expr]
    index_expression: ($) =>
      prec(PREC.INDEX, seq(
        field("object", $._expression),
        "[",
        field("index",  $._expression),
        "]"
      )),

    // Member access: expr.name
    member_expression: ($) =>
      prec(PREC.MEMBER, seq(
        field("object",   $._expression),
        ".",
        field("property", $.identifier)
      )),

    // new ClassName(args)
    new_expression: ($) =>
      seq(
        "new",
        field("type",      $._type),
        field("arguments", $.argument_list)
      ),

    // await expr
    await_expression: ($) =>
      prec(PREC.AWAIT, seq("await", field("value", $._expression))),

    // (expr)
    parenthesized_expression: ($) =>
      seq("(", field("expression", $._expression), ")"),

    // Collection literals
    list_literal: ($) =>
      seq("[", commaSep($._expression), "]"),

    dict_literal: ($) =>
      seq("{", commaSep($.dict_entry), "}"),

    dict_entry: ($) =>
      seq(
        field("key",   $._expression),
        ":",
        field("value", $._expression)
      ),

    tuple_literal: ($) =>
      seq("(", $._expression, ",", commaSep1($._expression), ")"),

    // -----------------------------------------------------------------------
    // Statements
    // -----------------------------------------------------------------------

    _statement: ($) =>
      choice(
        $.block,
        $.expression_statement,
        $.variable_declaration,
        $.if_statement,
        $.while_statement,
        $.do_while_statement,
        $.for_statement,
        $.for_in_statement,
        $.return_statement,
        $.break_statement,
        $.continue_statement,
        $.defer_statement,
        $.throw_statement,
        $.try_statement,
        $.match_statement,
        $.switch_statement
      ),

    block: ($) => seq("{", repeat($._statement), "}"),

    expression_statement: ($) => seq($._expression, ";"),

    // Variable declaration: [modifiers] type name [= expr] ;
    //                    or: const name = expr ;
    //                    or: auto name = expr ;
    variable_declaration: ($) =>
      seq(
        optional($.modifier_list),
        field("type", $._type),
        field("name", $.identifier),
        optional(seq("=", field("value", $._expression))),
        ";"
      ),

    modifier_list: ($) =>
      repeat1(choice(
        "public", "private", "protected",
        "static", "const", "thread_local", "override"
      )),

    // if / else if / else
    if_statement: ($) =>
      seq(
        "if",
        field("condition", $.parenthesized_expression),
        field("consequence", $.block),
        optional(seq(
          "else",
          field("alternative", choice($.if_statement, $.block))
        ))
      ),

    // while (cond) { }
    while_statement: ($) =>
      seq(
        "while",
        field("condition", $.parenthesized_expression),
        field("body",      $.block)
      ),

    // do { } while (cond);
    do_while_statement: ($) =>
      seq(
        "do",
        field("body",      $.block),
        "while",
        field("condition", $.parenthesized_expression),
        ";"
      ),

    // C-style for: for (init; cond; update) { }
    for_statement: ($) =>
      seq(
        "for",
        "(",
        field("init",   optional(choice($.variable_declaration, seq($._expression, ";")))),
        field("condition", optional($._expression)),
        ";",
        field("update", optional($._expression)),
        ")",
        field("body", $.block)
      ),

    // for (x in iterable) { }
    for_in_statement: ($) =>
      seq(
        "for",
        "(",
        field("variable", $.identifier),
        "in",
        field("iterable", $._expression),
        ")",
        field("body", $.block)
      ),

    return_statement: ($) =>
      seq("return", optional(field("value", $._expression)), ";"),

    break_statement: (_) => seq("break", ";"),

    continue_statement: (_) => seq("continue", ";"),

    defer_statement: ($) =>
      seq("defer", field("body", choice($.block, $.expression_statement))),

    throw_statement: ($) =>
      seq("throw", field("value", $._expression), ";"),

    // try { } catch (Type name) { } [finally { }]
    try_statement: ($) =>
      seq(
        "try",
        field("body", $.block),
        repeat1($.catch_clause),
        optional($.finally_clause)
      ),

    catch_clause: ($) =>
      seq(
        "catch",
        "(",
        field("type",       optional($._type)),
        field("parameter",  optional($.identifier)),
        ")",
        field("body",       $.block)
      ),

    finally_clause: ($) =>
      seq("finally", field("body", $.block)),

    // match expr { case pattern: statements... }
    match_statement: ($) =>
      seq(
        "match",
        field("value", $._expression),
        "{",
        repeat($.match_arm),
        optional($.default_arm),
        "}"
      ),

    match_arm: ($) =>
      seq(
        "case",
        field("pattern", $._expression),
        ":",
        repeat($._statement)
      ),

    // switch (expr) { case val: ... default: ... }
    switch_statement: ($) =>
      seq(
        "switch",
        "(",
        field("value", $._expression),
        ")",
        "{",
        repeat($.switch_case),
        optional($.default_case),
        "}"
      ),

    switch_case: ($) =>
      seq(
        "case",
        field("value", $._expression),
        ":",
        repeat($._statement)
      ),

    default_case: ($) =>
      seq("default", ":", repeat($._statement)),

    default_arm: ($) =>
      seq("default", ":", repeat($._statement)),

    // -----------------------------------------------------------------------
    // Declarations
    // -----------------------------------------------------------------------

    // fn name(params) [: return_type] { body }
    // [modifiers] return_type name(params) { body }
    function_declaration: ($) =>
      seq(
        optional($.modifier_list),
        optional("async"),
        choice(
          // fn-keyword form
          seq(
            "fn",
            field("name",        $.identifier),
            field("parameters",  $.parameter_list),
            optional(seq(":", field("return_type", $._type)))
          ),
          // typed form: return_type name(params)
          seq(
            field("return_type", $._type),
            field("name",        $.identifier),
            field("parameters",  $.parameter_list)
          )
        ),
        field("body", $.block)
      ),

    parameter_list: ($) =>
      seq("(", commaSep($.parameter), ")"),

    parameter: ($) =>
      seq(
        optional($.modifier_list),
        field("type",    optional($._type)),
        field("name",    $.identifier),
        optional(seq("=", field("default", $._expression)))
      ),

    // class Name [: Base, Iface...] { members }
    class_declaration: ($) =>
      seq(
        optional($.modifier_list),
        "class",
        field("name",       $.type_identifier),
        optional(seq(":", commaSep1($.type_identifier))),
        field("body",       $.class_body)
      ),

    class_body: ($) =>
      seq("{", repeat($._class_member), "}"),

    _class_member: ($) =>
      choice(
        $.function_declaration,
        $.variable_declaration,
        $.constructor_declaration
      ),

    constructor_declaration: ($) =>
      seq(
        optional($.modifier_list),
        field("name",       $.type_identifier),
        field("parameters", $.parameter_list),
        field("body",       $.block)
      ),

    // interface Name [: Base...] { method_signatures }
    interface_declaration: ($) =>
      seq(
        optional($.modifier_list),
        "interface",
        field("name",    $.type_identifier),
        optional(seq(":", commaSep1($.type_identifier))),
        field("body",    $.interface_body)
      ),

    interface_body: ($) =>
      seq("{", repeat($.method_signature), "}"),

    method_signature: ($) =>
      seq(
        optional($.modifier_list),
        optional("async"),
        choice(
          seq("fn", field("name", $.identifier), field("parameters", $.parameter_list),
              optional(seq(":", field("return_type", $._type)))),
          seq(field("return_type", $._type), field("name", $.identifier),
              field("parameters", $.parameter_list))
        ),
        ";"
      ),

    // enum Name { VARIANT [= expr], ... }
    enum_declaration: ($) =>
      seq(
        optional($.modifier_list),
        "enum",
        field("name", $.type_identifier),
        field("body", $.enum_body)
      ),

    enum_body: ($) =>
      seq("{", commaSep($.enum_variant), optional(","), "}"),

    enum_variant: ($) =>
      seq(
        field("name",  $.identifier),
        optional(seq("=", field("value", $._expression)))
      ),

    // namespace Name { top-level items }
    namespace_declaration: ($) =>
      seq(
        "namespace",
        field("name", $.identifier),
        "{",
        repeat($._top_level_item),
        "}"
      ),

    // import Name from "path" [as Alias]
    // import { A, B } from "path"
    import_declaration: ($) =>
      seq(
        "import",
        field("imports", choice(
          $.import_all,
          $.import_list,
          $.identifier
        )),
        "from",
        field("source", $.string_literal),
        optional(seq("as", field("alias", $.identifier))),
        ";"
      ),

    import_all: (_) => "*",

    import_list: ($) =>
      seq("{", commaSep1($.import_specifier), "}"),

    import_specifier: ($) =>
      seq(
        field("name",  $.identifier),
        optional(seq("as", field("alias", $.identifier)))
      ),

    // extern fn name(params) [: return_type] ;
    // extern "C" { declarations }
    extern_declaration: ($) =>
      choice(
        seq(
          "extern",
          optional($.string_literal),
          "fn",
          field("name",        $.identifier),
          field("parameters",  $.parameter_list),
          optional(seq(":", field("return_type", $._type))),
          ";"
        ),
        seq(
          "extern",
          field("abi",  $.string_literal),
          "{",
          repeat($._top_level_item),
          "}"
        )
      ),
  },
});

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Zero or more comma-separated items (trailing comma NOT allowed).
 * @param {RuleOrLiteral} rule
 */
function commaSep(rule) {
  return optional(commaSep1(rule));
}

/**
 * One or more comma-separated items (trailing comma NOT allowed).
 * @param {RuleOrLiteral} rule
 */
function commaSep1(rule) {
  return seq(rule, repeat(seq(",", rule)));
}
