# Noven Tarkov Support — Codex workflow

本文件适用于本仓库所有后续开发任务，除非用户明确覆盖相应规则。
These rules apply to future development in this repository unless explicitly overridden by the user.

默认节奏：高频本地提交 + 开发期间聚焦验证 + 里程碑边界完整验证。
Default rhythm: frequent local commits, focused development validation, full milestone validation.

## 1. 原子提交 / Atomic commits

一个连贯、可审查且局部稳定的开发单元完成后就提交，不等待整个大功能结束。每个提交必须回答“这一项改动添加、修改或修复了什么？”
Commit each coherent, reviewable, locally stable unit rather than waiting for a whole feature. Each commit must explain one clear addition, change or fix.

合理边界：生成器/数据、解析器/目录模型、浏览器/服务、一项通用组件、完整 UI 区块、搜索、导航、本地化、测试、文档、独立错误或性能修复。不要混入无关工作。
Good boundaries include data/generation, catalog/model, browser/service, reusable component, complete UI section, search, navigation, localization, tests, docs and isolated bug/performance fixes. Do not mix unrelated work.

手写代码尽量控制在可审查的几十到几百行；可拆分时避免超过约 500–1000 行。这是指导而非硬限制，确定性生成 TSV/JSON 不按该行数衡量。
Prefer tens to hundreds of hand-written changed lines and avoid roughly 500–1000+ where clean splitting is practical. This is guidance, not a hard limit; deterministic generated assets are exempt.

## 2. 提交后继续 / Continue after commits

普通节奏：实现 → 聚焦验证 → 本地提交 → 简短报告 → 自动继续。不得每次提交后等待用户确认。
Implement, validate, commit locally, report briefly and continue automatically; do not wait for confirmation after each commit.

仅在需要设计决定、要求不明确、验证失败、发现回归、需要破坏性/高风险操作，或达到用户指定验收点时停下询问或报告。
Stop for design decisions, ambiguity, validation failures, regressions, destructive/risky actions or the requested review/acceptance point.

## 3. 本地历史 / Local history

默认只做本地提交。未经明确要求，不 push、打标签、force-push、改写已发布历史、squash 旧里程碑或 amend 已完成的旧里程碑。推送是独立授权操作。
Local commits are expected. Do not push, tag, force-push, rewrite published history, squash prior milestones or amend completed milestones without explicit instruction. Pushing requires separate approval.

使用明确的祈使式 subject，例如 `Add native map catalog loader` 或 `Fix IME candidate positioning`；不用 progress、updates、misc、checkpoint 等含糊标题。原因、边界或非显然行为重要时添加正文。
Use specific imperative subjects, not vague progress/updates/misc/checkpoint messages. Add a body for important rationale, boundaries or non-obvious behavior.

## 4. 共享组件与独立修复 / Shared components and fixes

SearchBox、Scrollbar、PageTransition、ItemImageCache、NavigationButton、本地化或通用布局/动画的实质改动优先独立提交；只有微小且不可分的变更才可归入功能提交。
Meaningful shared-component changes should normally have their own commits; only tiny inseparable changes belong inside a feature commit.

发现无关错误时，先隔离、修复、验证、独立提交，再继续原功能。不扩大原任务或违反用户对 Scanner/OCR 等模块的限制。
Isolate, fix, validate and separately commit unrelated bugs before returning to the feature, without expanding scope or violating module restrictions.

已知构建失败、相关测试失败、正常使用崩溃、损坏数据或破坏已验收功能的状态不得提交，除非用户明确要求临时 checkpoint。
Never commit a known-broken build/test, normal-use crash, corrupt data or regression unless the user explicitly requests a temporary checkpoint.

## 5. 生成数据 / Generated data

生成器、schema/生成资产和生成器测试可作为一个连贯数据管线提交。不得手改生成资产，除非仓库定义了人工 override 机制。
Keep generator, schema/assets and generator tests together when they form a coherent pipeline. Do not manually edit generated data without a defined override mechanism.

保持身份、编码和确定性输出契约。不要仅为消除空白检查而删除 TSV 末尾空列分隔符；明确区分结构分隔符和意外源代码空白。
Preserve identity, encoding and deterministic-output contracts. Distinguish structural empty-final-column TSV separators from accidental whitespace rather than corrupting columns to satisfy checks.

## 6. 三层验证 / Three validation tiers

### A. 小型聚焦提交 / Small focused unit

生成器、纯模型、工具函数、单个 UI helper 或聚焦修复至少运行：
For a generator, pure model, utility, UI helper or focused fix, run at least:

- `git diff --check`.
- 适用的相关目标构建 / Relevant target build when applicable.
- 直接相关的单元/聚焦测试；生成器包括确定性检查 / Direct unit/focused tests, including determinism for generators.

不要每个微小提交都运行全项目矩阵。纯文档提交可只做文档差异/文件审计和 diff 检查，无需构建。
Do not run the whole matrix for every tiny commit. Documentation-only commits need diff/file inspection and whitespace checks, not builds.

### B. 中型共享集成提交 / Medium shared integration

MainWindowUi、PageHost、SearchBox、ItemImageCache、共享导航/动画/布局或本地化框架改动通常运行 Debug 构建、直接相关测试、main_navigation、localization_ui、受影响页面/组件测试，以及 `git diff --check`。按实际风险增加其他聚焦回归。
For shared integration, normally run a Debug build, direct tests, main_navigation, localization_ui, affected page/component tests and diff checks. Add focused regressions based on actual risk.

### C. 最终里程碑 / Final milestone

宣称完成前运行：
Before declaring a milestone complete, run:

- MSVC x64 Debug clean/full build.
- MSVC x64 Release clean/full build.
- 完整 Debug CTest / Complete Debug CTest.
- 完整 Release CTest / Complete Release CTest.
- 所有相关生成器和 Catalog generator / Relevant generators and Catalog generator.
- i18n validator tests、资源覆盖及 placeholder validation / i18n tests, coverage and placeholders.
- `git diff --check`，仓库/文件审计 / Diff checks and repository/file audit.
- 用户要求的人工/实机验证 / Requested manual/live validation.

只跑聚焦测试不得称为完整验证；未执行项必须明确报告，构建有警告不得称为 warning-clean。
Focused tests are not full validation. Report unperformed checks and remaining warnings honestly.

## 7. 回归处理 / Regression handling

验证发现回归时停止最终里程碑提交，调查实际原因。单独重跑可以用于诊断，但不能抹去整套失败。记录整套结果、单项重跑和是否复现的区别。
Stop the final milestone commit on a regression and investigate. Individual reruns diagnose but do not erase full-suite failures; report each result distinctly.

不能以反复跑绿、无原因加超时、禁用测试、削弱断言、任意 sleep 或标记忽略掩盖问题。修复与功能逻辑独立时，优先单独提交。
Do not hide failures with retries-until-green, unjustified timeout increases, disabled tests, weakened assertions, arbitrary sleeps or ignored tests. Prefer an isolated fix commit when logically separate.

## 8. 每次提交前 / Before every commit

1. 检查 `git status --short` 和预期差异 / Inspect status and intended diff.
2. 运行 `git diff --check`，完成该层级验证 / Run diff checks and the applicable validation tier.
3. 只暂存目标文件 / Stage intended files only.
4. 检查 `git diff --cached`、`git diff --cached --stat` 和暂存文件清单 / Inspect staged diff, stats and file list.
5. 确认无意外文件，必要时运行 `git diff --cached --check` / Verify exclusions and staged whitespace.

不提交 build、日志、截图、recent-scans 运行时数据、economy/price-history/item-image cache、临时文件、__pycache__ 或调查产物，除非它们明确属于产品源码/资产。
Exclude build/log/screenshot/runtime/cache/temp/Python/investigation artifacts unless explicitly intended product source/assets.

## 9. 每次提交后 / After every commit

简短报告 short SHA、subject、文件数量和本次聚焦验证，然后自动继续。例如：
Report short SHA, subject, file count and focused validation, then continue. Example:

```text
abc1234 Add native map catalog loader
7 files changed
Catalog tests 12/12 PASS; git diff --check PASS
```

## 10. 文档与验收 / Documentation and acceptance

数据契约随数据提交、组件契约随组件提交，最终 REVIEW.md 随里程碑完成；不要把所有文档拖到最后。
Document durable data/component contracts alongside their commits and final review notes at the milestone boundary.

UI 仍在验收时，可提交稳定基础和小批量视觉调整，但用户要求人工验收的里程碑必须取得批准后才完成。最终提交可仅含集成、清理、文档、本地化或小修饰，不必重新包含整个功能。
During UI review, commit stable foundations and small polish units. Required visual acceptance must precede milestone completion. The final commit may contain integration/cleanup/docs/localization/finishing work only.

不要人为把全部功能 squash 成巨型 `Implement full X browser`，除非用户明确要求。后续共享功能不能回写旧里程碑，历史 REVIEW 必须保持边界准确。
Do not artificially squash the feature into a giant commit unless requested. Later shared features must not be retroactively attributed to older milestones.

## 11. 项目技术与安全边界 / Project boundaries

使用原生 C++23、Win32、Direct2D、DirectWrite 和 Windows 技术栈；不引入 Electron、WebView、.NET 或生产 Python 运行时。Python 仅用于开发期生成器/测试。
Use native C++23/Win32/Direct2D/DirectWrite. No Electron, WebView, .NET or production Python dependency; Python is development-time tooling only.

禁止 EFT DLL 注入、进程内存读取、抓包、隐蔽游戏状态提取和自动游戏操作。
No EFT DLL injection, process-memory reading, packet sniffing, hidden game-state extraction or automated gameplay.

代码备注中文在前、英文在后，重点解释架构、所有权、不变量、线程边界、稳定身份和数据语义，不逐行注释简单代码。
Comments are Chinese first, English second, explaining architecture, ownership, invariants, threading, stable identity and semantics rather than trivial lines.

## 12. 默认阶段 / Default feature phases

数据/生成器 → 原生模型 → 基础 UI → 交互导航 → 共享组件 → 修饰/本地化/测试/文档 → 完整回归及用户验收。
Data/generator → native model → basic UI → interaction/navigation → shared components → polish/localization/tests/docs → full regression and user acceptance.

各阶段按完整稳定单元执行聚焦验证并本地提交，过程中自动继续；最终汇报完整验证。用户另有要求时以其明确指令为准。不自动 push 或打标签。
Validate and locally commit stable units throughout, continue automatically, then report full validation. Explicit user instructions override defaults. No automatic push or tags.
