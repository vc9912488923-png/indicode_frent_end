# Indicode Front-End Language Specification

**Front-end package version:** 0.9.8 (pre-release; not a complete compiler).

**Grammar version:** 1.0.0
**Status:** Normative for the syntax accepted by this front-end release.

This specification defines the source syntax parsed by the C++20 front end. It
does not define execution behavior, code generation, or runtime behavior.
Changes to accepted syntax require a grammar-version change and corresponding
parser, lexer, and conformance-test updates. Bug fixes that reject malformed
source or accept syntax already described here do not change the grammar
version.

Static binding and typing rules are specified separately in
[SEMANTICS_V1.md](./SEMANTICS_V1.md).

## Source and tokens

Source is UTF-8. Invalid UTF-8 is diagnosed. Identifiers are ASCII and have the
form `[A-Za-z_][A-Za-z0-9_]*`. The reserved words are:

`function record return if else repeat times for in while let var true false move borrow borrow_mut`

Integer literals are decimal, binary (`0b`), octal (`0o`), or hexadecimal
(`0x`), with underscores permitted only between valid digits. Decimal floating
literals have digits after a decimal point and/or a decimal exponent. A string is a
double-quoted, single-line sequence supporting `\"`, `\\`, `\n`, `\r`, `\t`,
`\0`, and Unicode scalar escapes `\u{...}`. Numeric suffixes, raw strings,
Unicode identifiers, and multiline strings are not part of v1.

Punctuation and operators are `(` `)` `{` `}` `[` `]` `,` `:` `.` `..` `;`
`->` `+` `-` `*` `/` `%` `!` `~` `=` `==` `!=` `<` `<=` `>` `>=` `&&` and
`||`. `#` line comments and nested `/* ... */` block comments are trivia.
Newlines remain syntax separators, including newlines inside block comments.

## Grammar

The notation below uses `*` for zero or more, `+` for one or more, and `?`
for optional. Newline and semicolon are statement separators; repeated
separators are allowed between declarations and statements.

```ebnf
program          = { separator }, { declaration, { separator } }, EOF ;
declaration      = record-declaration | function-declaration | statement ;
record-declaration
                 = "record", identifier, "{", { separator },
                   [ field, { (",", { separator } | separator), field }, [","], { separator } ],
                   "}" ;
field            = identifier, ":", type ;
function-declaration
                 = "function", identifier, [ generic-parameters ],
                   "(", [ parameter, { ",", parameter } ], ")",
                   [ "->", type ], [ effect-annotation ], block ;
generic-parameters
                 = "<", generic-parameter, { ",", generic-parameter }, ">" ;
generic-parameter
                 = identifier, [ ":", ( "Numeric" | "Integer" ) ] ;
parameter        = identifier, ":", [ ownership-mode ], type ;
ownership-mode   = "move" | "borrow" | "borrow_mut" ;
effect-annotation
                 = "effects", "(", [ effect-name, { ",", effect-name } ], ")" ;
effect-name      = identifier ;
type             = "Tensor", "<", type, ",", "[", dimension, { ",", dimension }, "]", ">"
                 | identifier, [ "<", type, ">" ] ;
dimension        = identifier | integer-literal ;

block            = "{", { separator }, { statement, { separator } }, "}" ;
statement        = "let", identifier, [ ":", type ], "=", expression
                 | "var", identifier, [ ":", type ], "=", expression
                 | type, identifier, "=", expression
                 | identifier, "=", expression
                 | "return", [ expression ]
                 | conditional
                 | "while", expression, block
                 | "repeat", expression, "times", block
                 | "for", identifier, "in", expression, block
                 | expression ;
conditional      = "if", expression, block, [ "else", (block | conditional) ] ;
expression       = prefix-expression, { binary-operator, prefix-expression } ;
prefix-expression
                 = ( "!" | "+" | "-" | "~" | "move" | "borrow" | "borrow_mut" ),
                   prefix-expression
                 | postfix-expression ;
postfix-expression
                 = primary, { index | member } ;
primary          = identifier, [ call ] | integer-literal | float-literal | string-literal
                 | "true" | "false" | list-literal | "(", expression, ")" ;
list-literal     = "[", [ expression, { ",", expression } ], "]" ;
call             = "(", [ positional-arguments, [ ",", named-arguments ]
                   | named-arguments ], ")" ;
positional-arguments
                 = expression, { ",", expression } ;
named-arguments  = named-argument, { ",", named-argument } ;
named-argument   = identifier, ":", expression ;
index            = "[", expression, "]" ;
member           = ".", identifier ;
binary-operator  = "||" | "&&" | ".." | "==" | "!=" | "<" | "<=" | ">" | ">="
                 | "+" | "-" | "*" | "/" | "%" ;
separator        = newline | ";" ;
```

The parser rejects positional call arguments after a named argument. Calls and
record construction use the same syntax; semantic analysis resolves the name.
The `if` production is recursively nestable, so any-length `else if` chains
are accepted. The legacy `identifier identifier = expression` declaration form is the
non-generic `type identifier` alternative above. Generic function declarations
support inferred type parameters and the `Numeric` and `Integer` constraints;
generic record declarations are not supported. Parameter ownership modes and
function effect annotations are part of the parsed syntax and are checked by
the semantic analyzer.

Modules/imports, overload declarations, and declaration visibility are
explicitly outside grammar v1. `List<T>` and `Range<T>` use the single-type-
argument generic type form accepted by the parser; tensor types use the
dedicated shape form shown above.

## Expression precedence and associativity

All binary operators are left-associative. A larger number binds more tightly.
Calls, indexing, and member access are postfix forms and bind more tightly
than prefix operators.

| Precedence | Operators | Associativity |
|---:|---|---|
| 1 | `||` | left |
| 2 | `&&` | left |
| 3 | `..` | left |
| 4 | `==`, `!=` | left |
| 5 | `<`, `<=`, `>`, `>=` | left |
| 6 | `+`, `-` | left |
| 7 | `*`, `/`, `%` | left |

Prefix operators are right-nested. Parentheses override precedence. Ownership
operators are syntactically prefix forms; semantic rules restrict where they
may be used.

## Scope and compatibility

This version freezes syntax only. Type rules, diagnostics, symbol resolution,
and HIR compatibility have separate acceptance requirements and are not
implied by a successful parse. A future incompatible syntax change must update
this version, the public `grammar_version` constant, examples, and tests
together.

The v1 namespace model has separate value, function, and type lookup. A local
value may shadow an outer value, but duplicate declarations in the same scope
are errors. Record names and function names cannot collide because both use
identifier-call syntax. Functions and record types have stable per-program
symbol identities; module/import identities and overload identities do not
exist in v1 because modules, imports, and overloads are not supported.

The v1 tensor type is a value type with a numeric element type and a non-empty
list of non-negative integer or symbolic dimensions. Assignments, returns,
record fields, and function arguments require exact element-type, rank, and
dimension agreement; symbolic names match literally. Tensor operations,
broadcasting, dynamic shape guards, and device placement are unsupported in
v1, so there is no broadcast or device compatibility rule to apply.

Ownership modes are a deliberately limited static check: `move`, `borrow`, and
`borrow_mut` are checked against parameter modes at call sites; use after move,
conflicting borrows in one call, and mutable borrowing of immutable bindings
are diagnosed. This is not a general lifetime, aliasing, or unsafe-code model.
Effect annotations accept `io`, `allocation`, and `mutation`; these effects are
inferred from supported operations and transitive calls and checked against an
annotation when one is present. The effect model does not describe runtime or
backend behavior.

Control-flow analysis treats `return` as terminating, and a conditional as
terminating only when it has an `else` and every branch terminates. `while`,
`repeat`, and `for` loops are never assumed to execute; a return inside a loop
does not prove a function returns. A statement following a guaranteed return
is an error (`E-SEMA-050`).

Constant evaluation v1 is limited to integer and Boolean literal expressions,
the supported unary operators, arithmetic, comparisons, and short-circuit
Boolean operators. It checks signed 64-bit overflow and division by zero and
uses a deterministic 256-node evaluation budget. Non-constant expressions are
not errors from the evaluator; the ordinary type checker handles them.
