---
name: bug_suggestion
description: 'Use when the user uploads a bug report, logs, screenshots, or related materials and wants help analyzing likely root causes without directly modifying code. Focus on evidence-based diagnosis, ranked hypotheses, and verification steps.'
argument-hint: 'Provide: bug_report_path or symptom_summary, optional related_materials, optional codebase_path, optional depth (quick/standard/deep)'
user-invocable: true
---

# Bug Suggestion

## Trigger Contract
- This skill is user-invocable and analysis-only.
- Use it when the user wants help understanding why a bug may happen, based on reports, logs, traces, screenshots, or related materials.
- Do not use it to implement fixes, edit code, or propose a direct patch as the primary outcome.

## Goal
Help the user build a clear, reusable debugging path so they can identify the cause themselves and decide the fix with confidence.

## Input Variables
- `bug_report_path` or `symptom_summary` (required): the failing symptom, report, or attached issue description.
- `related_materials` (optional): logs, stack traces, screenshots, repro steps, test output, perf data, or issue comments.
- `codebase_path` (optional): project or module path if code inspection is needed for reasoning.
- `depth` (optional, default `standard`): `quick` | `standard` | `deep`.

## Standard Workflow
Follow these phases in order unless the user explicitly asks for a narrower scope.

1. Symptom Reconstruction
- Restate the bug in one concise paragraph.
- Separate observed facts from inferred assumptions.
- Extract: expected behavior, actual behavior, scope, frequency, environment, and reproducibility.

2. Evidence Inventory
- List all available evidence types.
- For each item, note what it proves, what it does not prove, and any missing context.
- Mark weak or indirect evidence as `[需确认]`.

3. Timeline and Trigger Analysis
- Reconstruct the execution path or user action sequence that likely leads to the bug.
- Identify the earliest failing point, the trigger condition, and the downstream symptom.
- If the timeline is incomplete, clearly say what is still unknown.

4. Root-Cause Hypothesis Ranking
- Produce 3-5 plausible causes ordered by confidence.
- For each cause, include:
  - why it fits the evidence
  - what evidence is missing
  - how to falsify it quickly
- Distinguish between:
  - immediate cause
  - underlying cause
  - contributing factors

5. Verification Plan
- Propose concrete checks the user can run next.
- Prefer low-cost validations first: log checks, targeted assertions, minimal repro, config comparison, or boundary tests.
- Avoid telling the user to rewrite code before verifying the hypothesis.

6. Guidance, Not Patch
- End with a decision tree or checklist the user can follow.
- If code changes are obviously needed, describe the direction at a high level only.
- Do not provide a direct code modification as the main answer.

## Output Rules
- Respond in Chinese, keep technical terms and symbols in original spelling when useful.
- Use Markdown with clear sections.
- Mark inference as `[推测]` and missing evidence as `[需确认]`.
- Prefer concise, actionable language over long prose.
- Do not mix diagnosis with implementation unless the user explicitly asks for a fix.

## Required Output Template
Use this skeleton.

1. 问题复述
- 现象:
- 预期:
- 实际:
- 复现条件:

2. 证据清单
- Evidence 1:
  - What it shows:
  - Limitation:
- Evidence 2:
  - What it shows:
  - Limitation:

3. 可能原因排序
- Cause 1 (highest confidence):
  - Why it fits:
  - Missing evidence:
  - Fast verification:
- Cause 2:
  - Why it fits:
  - Missing evidence:
  - Fast verification:

4. 排查顺序
- Step 1:
- Step 2:
- Step 3:

5. 结论与下一步
- Most likely direction:
- What to verify next:
- What not to change yet:

## Depth Modes
- `quick`: summarize symptom, list top 3 causes, and give 3 checks.
- `standard`: full evidence inventory, ranked hypotheses, and verification plan.
- `deep`: include execution-path reasoning, alternative hypotheses, and a decision tree.

## Guardrails
- Do not present a fix as the default deliverable.
- Do not claim certainty without evidence.
- Do not skip alternative explanations just because one cause seems likely.
- Do not broaden scope into unrelated code review unless the user asks.
