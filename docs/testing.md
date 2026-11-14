# Testing

## Running tests

```bash
make test
```

This builds and runs five C unit test executables:

- `build/tracey_lexer_test` — lexer tests.
- `build/tracey_source_test` — source loading tests.
- `build/tracey_argument_test` — command-line parsing tests.
- `build/tracey_parser_test` — parser tests.
- `build/tracey_semantic_test` — semantic analysis tests.

It then runs the end-to-end language suite: every `tests/case_*.trc` is
compiled with `traceyc`, the resulting binary is run, and its exit code and
stdout are checked against the expectations written in the case header. The
suite currently has 33 cases. On hosts that cannot run AArch64 binaries the
end-to-end step is skipped instead of failing.

Run only the end-to-end cases with:

```bash
make test-e2e
```

## Adding tests

- Lexer tests: `test/test_lexer.c`.
- Parser tests: `test/test_parser.c`.
- Semantic tests: `test/test_semantic.c`.
- End-to-end language tests: add a `tests/case_XX.trc` file. The header
  comments declare the expectations:
  - `// expect_exit: N` — expected process exit code (default 0).
  - `// expect_stdout: S` — expected stdout (optional).
  - `// expect_stdin: S` — input fed to the program on stdin (optional).

Keep tests deterministic. When changing a documented rule (for example the
lexer), update the corresponding unit tests in the same change.

## Fuzz-lite

The lexer tests include a deterministic fuzz-lite case driven by a fixed
seed. Do not change the seed without a reason.
