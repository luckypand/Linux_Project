---
name: linux-command-direct
description: 'Use only when the user explicitly invokes /linux-command-direct to request direct executable Linux shell commands. Do not auto-trigger for general coding or chat requests.'
argument-hint: 'Describe the Linux task and target environment'
user-invocable: true
---

# Linux Command Direct Output

## Trigger Contract
- This skill is command-gated.
- Execute this skill only when the user explicitly invokes `/linux-command-direct`.
- If the command is not explicitly invoked, do not use this skill.

## Purpose
Produce Linux commands that the user can copy and run immediately, without long explanations.

## When to Use
- User asks for Linux commands, shell commands, bash snippets, or terminal instructions.
- User wants fast execution-ready output instead of tutorials.
- User requests setup, troubleshooting, deployment, monitoring, filesystem, networking, or process commands.

## Output Rules
1. Default output is commands only.
2. Use one bash code block unless multiple stages are required.
3. Do not add introductions, background theory, or long prose.
4. Keep placeholders explicit and easy to replace, such as APP_PORT or PROJECT_DIR.
5. Prefer safe and idempotent commands when practical.
6. If a command is destructive, include a non-destructive preview step first.
7. If critical context is missing, ask only the minimum blocking questions.

## Procedure
1. Identify the requested Linux objective.
2. Detect required context:
   - distro or package manager (apt, dnf, yum, pacman, zypper)
   - privilege level (sudo availability)
   - target paths, ports, service names, process names
3. Branch:
   - If context is sufficient, output executable commands directly.
   - If context is partially missing but a safe default exists, provide defaults and clearly marked placeholders.
   - If context is missing and no safe default exists, ask 1-3 short blocking questions.
4. Validate command set quality before returning:
   - commands are syntactically coherent for Linux shell
   - order of operations is runnable end-to-end
   - required tools are installed or installation step is included
   - no unnecessary text is mixed into command blocks
5. Return final result in copy-paste format.

## Completion Checks
- Output can be executed line-by-line in a Linux shell.
- Any variables are declared before use.
- Any destructive action has a preview or verification command first.
- Response is concise and execution-first.

## Response Shapes

### Shape A: Direct commands only
```bash
# stage: optional short label
command 1
command 2
command 3
```

### Shape B: Minimal blocking questions
Use only when execution-safe output is impossible.

Q1: <required context>
Q2: <required context>

### Shape C: Multi-stage runbook (still command-first)
```bash
# 1) Install dependencies
...

# 2) Configure
...

# 3) Verify
...
```

## Quality Bar
- Execution-first
- Minimal text
- Linux-compatible
- Safe-by-default
- Copy-paste ready
