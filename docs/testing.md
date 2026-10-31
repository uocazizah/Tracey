# Testing

## Running tests

```bash
make test
```

This builds and runs three doctest executables:

- `build/tracey_lexer_test` — lexer tests.
- `build/tracey_parser_test` — parser tests.
- `build/tracey_semantic_test` — semantic analysis tests.

## Adding tests

- Lexer tests: `test/test_lexer.cxx`.
- Parser tests: `test/test_parser.cxx`.
- Semantic tests: `test/test_semantic.cxx`.

Keep tests deterministic. When changing a documented rule (e.g. lexer),
update the corresponding unit tests in the same change.

## Fuzz-lite

The lexer tests include a deterministic fuzz-lite case driven by a fixed
seed; do not change the seed without a reason.
```
