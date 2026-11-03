# AST ownership and lifetime v1

`Program` owns the parsed syntax tree. Declaration and statement collections
hold their nodes by value or `std::unique_ptr`; expression children and
arguments are owned by their parent. AST names, literal spellings, inferred
types, and resolved names are `std::string` values. AST nodes do not borrow
token text, lexer storage, or source buffers.

`SourceSpan` stores byte offsets and line/column values, not references into a
source file. A `Program` therefore remains valid after the parser, token list,
lexer result, and source object are destroyed. To display source excerpts or
render diagnostics later, the corresponding source contents still must be
retained separately (for example by `SourceManager`); spans alone cannot
reconstruct source text.

`Parser` borrows the token vector only for the lifetime of the parser and its
`parse()` call. The returned `ParseResult::program` owns its AST independently.
The current AST intentionally uses ordinary C++ ownership rather than a node
arena; callers can move `Program`/`ParseResult`, but AST values are
non-copyable where they contain `unique_ptr`.

The ownership alternative was measured with a generated function containing
10,000 statements. In one local GCC 13.3 default-build run, parsing took 27 ms
and AST destruction took 2 ms. The CTest acceptance checks that every statement
is retained and the AST is destroyed successfully; it deliberately has no
timing threshold. These machine-specific measurements do not justify adding an
arena for the current workload. Re-measure with representative production
inputs before changing the ownership model.
