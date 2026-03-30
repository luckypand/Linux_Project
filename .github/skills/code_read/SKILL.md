---
name: code_read
description: 'Use only when the user explicitly invokes /code_read to read unfamiliar code with a fixed SOP: call-chain first, function input/output contracts, role-name mapping for variables, and anti-forgetting notes. Do not auto-trigger for general coding requests.'
argument-hint: 'Provide: scope_path (required), optional entry_file, optional focus_flow, optional depth (quick/standard/deep)'
user-invocable: true
---

# Code Read

## Trigger Contract
- This skill is command-gated.
- Execute this skill only when the user explicitly invokes `/code_read`.
- If the command is not explicitly invoked, do not use this skill.

## Goal
Build a stable reading process for unfamiliar code so the user can keep context without being trapped by original variable names.

## Input Variables
- `scope_path` (required): module path or project path to analyze.
- `entry_file` (optional): starting entry file if user already knows it.
- `focus_flow` (optional): e.g. logging flow, request flow, process lifecycle.
- `depth` (optional, default `standard`): `quick` | `standard` | `deep`.

## Core Reading SOP
Always follow this order unless user asks otherwise.

1. Call Chain First
- Start from entry point and produce one main chain line first.
- Use arrow flow: caller -> callee -> branch -> sink.
- Identify trigger conditions for each branch.

2. Contract Before Implementation
- For each key function, summarize:
  - function responsibility (one sentence)
  - input contract (params + external state)
  - output contract (return + side effects)
  - failure points (most likely runtime breakpoints)

3. Role Names Instead of Raw Variable Names
- Translate original variable names into role labels.
- Keep a role mapping table and reference role labels in explanations.
- Do not rely on memorizing raw variable names.

4. Anti-Forgetting Notes
- After each function, write 5 fixed lines:
  - Responsibility
  - Inputs
  - Outputs/Side effects
  - Risks/Failure points
  - Upstream/Downstream

5. Close the Loop
- End with:
  - minimal executable mental model (3-5 lines)
  - top 3 misconceptions to avoid
  - debug checklist aligned to the call chain

## Output Rules
- Respond in Chinese; keep APIs and symbols in original code spelling.
- Be evidence-based; if uncertain, mark as `[需确认]`.
- Keep structure stable and repeatable so the user can review quickly.
- Prefer concise but complete sections over long prose.

## Required Output Template
Use this exact skeleton.

1. 入口与主调用链
- Main chain: A -> B -> C -> D
- Branch conditions:
  - condition 1: ...
  - condition 2: ...

2. 关键函数契约卡片
- Function: X
  - Responsibility:
  - Inputs:
  - Outputs:
  - Failure points:
  - Upstream/Downstream:

3. 变量角色翻译表
- original_name -> role_name (职责词)
- original_name -> role_name (职责词)

4. 防遗忘五行笔记（按函数）
- Responsibility:
- Inputs:
- Outputs/Side effects:
- Risks:
- Upstream/Downstream:

5. 易错点与调试清单
- Most likely causes:
- Step-by-step debug order:
- Quick verification commands/checkpoints:

## Depth Modes
- `quick`: one main chain + top 3 functions + top 3 risks.
- `standard`: one main chain + 5-8 functions + role mapping + debug checklist.
- `deep`: multiple chains + full key-module contracts + assumptions and validation plan.

## Guardrails
- Do not start from line-by-line details before the call chain is clear.
- Do not output only variable-by-variable commentary without contracts.
- Do not skip failure scenarios.
- Do not present inferred behavior as certainty.
