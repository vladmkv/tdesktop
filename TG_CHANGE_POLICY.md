# TG Change Fence Policy
Applies to the tg-cli branch and all future rebases/merges.

## Purpose
Make every TG-specific modification to an existing upstream file easy to locate, audit, reapply, and resolve during upstream merges.

## Mandatory Rule
Every TG-specific addition or replacement in an existing upstream-owned file must be enclosed by a named, language-valid marker pair.

C and C++:
```cpp
// TG_CHANGE_BEGIN: session-account-capabilities
THE CHANGE
// TG_CHANGE_END: session-account-capabilities
```

CMake, Python, PowerShell, and shell:
```cmake
# TG_CHANGE_BEGIN: tg-cli-build-option
THE CHANGE
# TG_CHANGE_END: tg-cli-build-option
```

Markdown:
```markdown
<!-- TG_CHANGE_BEGIN: planning-reference -->
THE CHANGE
<!-- TG_CHANGE_END: planning-reference -->
```

## Marker IDs
- Use lowercase kebab-case.
- Describe the stable intent, not an implementation detail or date.
- BEGIN and END IDs must match exactly.
- IDs must be unique within a file.
- Do not nest fenced blocks.

## New Files
Wholly new TG-owned files do not require fences. Examples:
- Telegram/tg_cli/**
- TG_PROBES/**
- TG_CHANGE_POLICY.md

If an upstream file is copied into a TG-owned directory, the copy is TG-owned but its origin and upstream revision must be documented at the top.

## Existing Files
- Keep each fenced block as small and cohesive as practical.
- Do not combine unrelated changes in one block.
- Prefer additive overloads, adapters, and guarded branches over changing existing signatures/call sites.
- Preserve the original desktop API and behavior whenever possible.
- Never edit generated files, vendored dependencies, or prepared build outputs to create a seam.

## Branch Changes Requiring Retrofit
Telegram/CMakeLists.txt:
- `tg-cli-build-option`: BUILD_TG_CLI option.
- `tg-cli-subdirectory`: guarded add_subdirectory(tg_cli).
- `tg-executable-name`: output_name change to tg.

## Automated Enforcement
Add a TG-owned checker under Telegram/tg_cli/tools/ before the first protected-source seam edit.

The checker must:
1. Compare the working tree or requested commit range against a supplied upstream/base revision.
2. Ignore wholly new TG-owned files.
3. For every modified existing file, parse language-appropriate markers.
4. Reject unmatched, nested, duplicate, or empty marker blocks.
5. Reject changed/additional lines outside a fenced block.
6. Reject every hunk containing deletions (including mixed add/delete hunks) unless the resulting hunk overlaps a named fenced replacement block.
7. Print file, hunk, and nearest marker ID for each violation.

Run the checker:
- before every TG commit
- after every upstream merge/rebase
- before marking any stage complete

## Review Checklist
- Marker intent remains valid after upstream changes.
- Desktop behavior outside the marker is unchanged.
- The fenced block is still necessary; remove it when upstream provides an equivalent seam.
- New upstream calls that bypass a capability interface are identified.
- Full desktop and tg_cli validation appropriate to the stage has passed.
