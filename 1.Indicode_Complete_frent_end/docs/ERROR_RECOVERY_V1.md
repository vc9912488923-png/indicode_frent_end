# Parser recovery v1

Parser recovery is best-effort and does not change whether a program is valid:
any syntax diagnostic still makes the parse unsuccessful for compilation.
The parser keeps successfully parsed nodes before and after malformed input.

At statement boundaries, recovery tracks nested parentheses and brackets so a
newline or semicolon inside a delimited expression does not end synchronization.
At depth zero it stops before the containing `}` or before a statement keyword
(`function`, `record`, `if`, `return`, `let`, `var`, `while`, `repeat`, or
`for`). Otherwise it advances through a separator and stops at the following
token, retaining identifier-led assignment statements. The block parser owns
the closing brace.

At declaration boundaries, recovery stops before a top-level `function` or
`record`, at a line break before a declaration body starts, or after consuming
the closing brace of a malformed declaration body. Nested braces are counted
so a body close is not mistaken for a nested block close.

Delimited expression lists (call arguments and list elements) synchronize at
their next top-level comma or closing delimiter, retaining valid sibling
expressions. Other expression errors propagate to the containing statement;
the block or declaration parser then performs synchronization. The parser
guarantees progress after a failed parse attempt, preventing malformed input
from repeating the same error forever. Recovery may emit more than one
diagnostic; tests require later valid statements/declarations to survive and
guard against cascading duplicate errors for a single malformed statement.
