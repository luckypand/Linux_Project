---
name: new_project_analyze
description: 'Use only when the user explicitly invokes /new_project_analyze to perform systematic codebase comprehension for a new project. Do not auto-trigger for general coding requests.'
argument-hint: 'Provide: project_path, optional focus_area, experience_level (junior/mid/senior)'
user-invocable: true
---

# New Project Analyze

## Trigger Contract
- This skill is command-gated.
- Execute this skill only when the user explicitly invokes `/new_project_analyze`.
- If the command is not explicitly invoked, do not use this skill.

## Input Variables
- `project_path` (required): repository path or uploaded source path.
- `focus_area` (optional): e.g. performance, security, architecture, maintainability.
- `experience_level` (optional, default `mid`): `junior` | `mid` | `senior`.

## Output Rules
- Respond in Chinese, keep technical terms in English.
- Use Markdown with clear sections, tables, and bullet lists.
- Mark inferred content as `[推测]`.
- Mark items needing verification as `[需确认]`.
- Prioritize evidence from code structure, build scripts, and entry points.
- Avoid obvious filler; each conclusion should map to observable project artifacts.

## Standard Workflow
Run these phases in order unless user asks for a shorter path.

1. Project Positioning
- Classify project type and core problem solved.
- Identify target users and usage scenarios.
- Estimate scale and maturity from repository signals.
- Summarize in 3-5 sentences.

2. Tech Stack Overview
- Identify languages, runtime, framework/library, build chain, package manager.
- Describe architecture and data flow using ASCII text diagram.
- Identify external dependencies and security-sensitive hardcoded endpoints if any.

3. Directory Structure
- Provide a tree view at depth 2-3 for key directories.
- Map directory intent, key files, and likely change frequency.
- Locate entry points: app start, config, tests.
- Mark 3 must-read directories for newcomers.

4. Data Flow Tracing
- Pick one representative flow: request handling, persistence, async task, or callback.
- Show end-to-end path: input -> transform -> output/store.
- Identify validation, error handling, retry, and concurrency risk points.

5. Core Business Logic
- Identify 3-5 core logic units.
- For each: problem domain, location, I/O contract, critical rules, edge cases.
- Provide simplified pseudocode for the most complex unit.

6. Onboarding Guide
- Provide 5-minute setup checklist with concrete commands.
- Provide dev workflow: formatting/lint/testing/debug hints.
- List common pitfalls and fixes.
- Provide a change route map for a typical feature update.

## Optional Deep Modules
Use only if user asks or `focus_area` requires it.

- State Management: lifecycle, mutation points, consistency and race risks.
- Code Quality Assessment: reliability, security, maintainability, extensibility, debt triage.
- Legacy Analysis: debt catalog and staged refactor path.
- Performance Profiling: hotspots, bottlenecks, optimization ROI and benchmarking plan.
- Security Audit: input validation, authz/authn, secret handling, dependency risk.
- Architecture Evolution: 10x growth scenarios, milestones, and acceptance criteria.

## Depth Control by Experience
- `junior`: explain concepts and commands step-by-step.
- `mid`: balance architecture and implementation details.
- `senior`: focus on tradeoffs, risks, and evolution strategy.

## Deliverable Template
Use this output skeleton:

1. 项目本质摘要 (3-5句)
2. 技术栈与架构图
3. 目录结构与入口定位
4. 一条典型数据流追踪
5. 核心业务逻辑清单 (3-5项)
6. 快速上手指南 (含命令)
7. 风险清单与优先级 (立即修复/计划修复/接受风险)
8. [可选] 针对 `focus_area` 的深度分析

## Guardrails
- Never claim certainty without evidence.
- If data is missing, explicitly state `[需确认]` and list how to verify.
- Prefer minimal assumptions and keep analysis actionable.
