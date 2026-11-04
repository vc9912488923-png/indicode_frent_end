# Diagnostic output schema v1

**Front-end package version:** 1.0.0.
This document versions diagnostic JSON independently from the package and from
AST/HIR JSON.

The `--json-diagnostics` option writes one JSON object:

```json
{
  "schemaVersion": 1,
  "diagnostics": [
    {
      "severity": "error",
      "code": "E-SEMA-005",
      "message": "initializer type does not match declared type",
      "file": "example.indi",
      "start": {"offset": 4, "line": 1, "column": 5},
      "end": {"offset": 8, "line": 1, "column": 9},
      "fixIt": null,
      "notes": []
    }
  ]
}
```

Offsets are UTF-8 byte offsets from the start of the source; lines and columns
are one-based and columns count Unicode scalars with tabs expanded to four
column stops. A non-null `fixIt` contains `message`, `replacement`, `start`,
and `end` using the same location structure. Each note contains `message`,
`start`, and `end`. Diagnostics and notes preserve source order.

The schema is additive only within a version: unknown object members should be
ignored by consumers. Removing/renaming fields or changing coordinate units
requires a new `schemaVersion`. Diagnostic codes are stable identifiers;
messages may be clarified without changing their meaning. New codes are
added rather than reusing a code for a different error. `E-LEX-*`, `E-PARSE-*`,
`E-SEMA-*`, and `E-HIR-*` identify the producing compiler phase.
