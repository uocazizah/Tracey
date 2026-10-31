# Maintenance

## Coding style

All C/C++ code follows `AGENTS.md` at the repository root:

- `lowercase_with_underscores` for identifiers.
- 4 spaces, no tabs, Allman braces.
- Pointer variables prefixed with `p_`.
- Linux-kernel-style comments (`/* ... */`).

## Formatting

C/C++ files are formatted with `clang-format` using the repository
`.clang-format`. Run:

```bash
clang-format -i <files>
```

## Pointer naming

Pointer parameters and variables use the `p_` prefix. `clang-tidy`
(`readability-identifier-naming`) is used to audit this.

## Documentation

Keep `docs/` in sync with code changes. When a public rule changes,
update the relevant doc in the same change.
```