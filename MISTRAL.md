# MISTRAL.md - Session Guidelines for Mistral AI

This file provides operational guidance for Mistral AI assistants working in this repository.

## Operational Conventions

- **PR Creation**: When explicitly asked to create a pull request, use `gh pr create` directly. Do not only push a branch and instruct the user to create it themselves.

- **Commit Messages**: Use clear, descriptive commit messages that follow the project's existing conventions.

- **Branch Naming**: Use descriptive branch names with a `vibe/` prefix for AI-generated work (e.g., `vibe/display-common-refactor-833f1a`).

- **File Modifications**: Only modify files as explicitly requested or as necessary to complete the stated task. Do not add personal state or preferences to the repository.

- **QMK Conventions**: Prefer weak function declarations (`__attribute__((weak))`) over function pointer structs for keyboard-specific overrides. This aligns with QMK's established patterns.

- **Compile-time vs Runtime**: Prefer compile-time polymorphism (macros, weak functions) over runtime configuration when possible. This reduces overhead and aligns with embedded firmware constraints.

- **Unit Tests**: Add unit tests for critical code paths, especially those involving matrix scanning, display logic, or any code that runs on every keypress. Tests should use weak function overrides to mock hardware-specific behavior.

## Project-Specific Notes

This repository is part of the PolyKybd ecosystem. See `CLAUDE.md` for detailed project-specific conventions, build instructions, and architectural guidance.

## Response Style

- Be concise and direct
- Answer questions fully but without unnecessary elaboration
- When asked to perform a task, do it end-to-end
- When asked a question, answer only
- Keep the user informed of progress - report what you're doing and why before starting work

## Learnings from Past Sessions

- **Session Isolation**: Each session is independent with no persistent memory. Preferences or conventions must be documented in version-controlled files (like this one) to persist across sessions.
- **Explicit Instructions**: Always follow explicit user requests precisely. When in doubt, ask for clarification rather than making assumptions.
- **Transparency**: When asked what you're doing, explain the plan before executing. Users want to understand the approach, not just see the results.
