# Case file schemas

**Decision (P1 -- Case System, TODO.md section 18): validation is native
C++, not executed JSON Schema.**

This directory intentionally holds no `.schema.json` files. `CaseReader`
(`src/io/case/*.cpp`) validates every case file's structure and content
directly in C++ against `nlohmann::json`, producing the codebase's own
`cfd::CaseConfigurationError` with a file/field/constraint/received-value
message (see `src/io/case/JsonUtil.hpp`) rather than a JSON Schema
validator's error format.

Populating this directory with schema files that are never actually
evaluated against a case would let the two definitions drift and would
misrepresent what CFDApp does at runtime -- TODO.md section 18 explicitly
warns against exactly that. If a real external schema (for editor
autocomplete, CI linting of case files independent of the CLI, etc.)
becomes worth adding later, add both the schema *and* the dependency that
actually evaluates it against every case file this project ships, in the
same change.

The authoritative format description is
[docs/user_guide/case_format.md](../docs/user_guide/case_format.md); the
parsers in `src/io/case/` are the authoritative validation behavior.
