# Contribution Process

## Before You Start

Clarify the scope of the issue: does it affect the public API, engine internals, build scripts, or examples and documentation? When a change spans multiple areas, describe the dependencies and expected behavior first. Do not bundle unrelated changes into a single patch.

## Comment and Commit Language

Comments, documentation, commit subjects, and commit bodies may be written in:

- Simplified Chinese
- Traditional Chinese
- English

Terminology, type names, paths, commands, and external API names may be kept in their original form. Do not use mixed-encoding text that cannot be consistently displayed or read by team members.

## Implementation

1. Make changes on a dedicated branch. Do not modify files unrelated to the current goal.
2. When adding public capabilities, determine the entry header, module, and lifecycle first; keep implementation details under the corresponding internal directory.
3. When modifying threading, rendering, resource, or serialization logic, clarify ownership, calling thread, failure behavior, and compatibility impact.
4. Comments should only explain constraints, decisions, or non-obvious behavior. Do not restate what the code already expresses.

## Verification

Before committing, complete verification appropriate to the scope of the change:

1. Build the affected targets.
2. Run relevant tests; if no tests exist, document the manual verification scenarios and results.
3. Check that new or changed public interfaces, configuration items, resource formats, and documentation are consistent.
4. Confirm no new errors, warnings, or assertion failures appear in logs.

## Commit and Review

1. Split work into commits that can be understood and verified independently.
2. Commit subjects describe behavioral changes; commit bodies explain the rationale, compatibility impact, and verification results.
3. Review descriptions should list the scope of changes, verified items, and remaining risks.
4. After receiving feedback, update the implementation, tests, or description while keeping the commit history readable.
