# Coding Principles

Principles to guide implementation decisions and resolve merge request debates.

---

## 1. Naming

**Rule**: Use prefixes to signal intent.

| Prefix | Meaning | Example |
|--------|---------|---------|
| `m_` | Member variable | `m_position` |
| `f` | Float/double value | `fDtSec` |
| `p` | Pointer (raw or smart) | `pWindow` |
| `I` | Abstract interface | `IProcess` |
| `b` | Boolean | `bPaused` |

**Why**: Reduces cognitive load; you know what `m_fRadius` is without checking the declaration.

---

## 2. Ownership

**Rule**: Choose the weakest ownership that works.

| Type | When to Use |
|------|-------------|
| `T&` | Non-null, non-owning, short-lived |
| `weak_ptr<T>` | Non-owning, may outlive, check before use |
| `shared_ptr<T>` | Shared ownership required |
| `unique_ptr<T>` | Exclusive ownership |

**Why**: Clearer lifetimes, fewer leaks, easier refactoring.

---

## 3. Units

**Rule**: Use canonical units everywhere; encode in names or document inline.

| Quantity | Unit | Abbreviation |
|----------|------|--------------|
| Time | seconds | `s`, `Sec` |
| Length | micrometers | `µm`, `Um`, `Microm` |
| Force | femtoNewtons | `fN`, `Fn` |
| Concentration | molecules/µm³ | `PerUm3` |
| Angle | radians | `Rad` |

- No milliseconds, nanometers, or picoNewtons — convert at boundaries if needed
- Append unit to constants: `MT_VGROW_MAX_UM_PER_S`
- Append unit to parameters: `float fRadiusMicrom`
- Comment if non-obvious: `double fDt; // seconds`

**Why**: Consistent units eliminate conversion bugs and make code reviewable without unit-tracing.

---

## 4. Interfaces over Implementations

**Rule**: Depend on abstract interfaces (`I*`), not concrete classes.

- Factory functions return `unique_ptr<IFoo>` or `shared_ptr<IFoo>`
- Consumers store interface pointers, not implementations
- Implementations live in platform-specific folders (`d3d12/`, `desktop/`)

**Why**: Enables platform/backend swaps (D3D12 ↔ Vulkan, Desktop ↔ VR).

---

## 5. Abstract Projects Structure

**Rule**: Separate interface, shared code, and implementation.

```
moduleName/
├── include/    # Public headers (IFoo.h, Context.h)
├── common/     # Platform-agnostic implementation
└── platform/   # Platform-specific (d3d12/, desktop/, vr/)
```

**Applies to**: Projects with one abstract interface and multiple implementations (e.g., `visLib`, `userInput`). Not required for single-implementation modules.

**Why**: Clear boundaries; implementations can vary without touching public API.

---

## 6. Comments Describe Current State

**Rule**: Comments explain *what is*, not *what was*.

- ❌ "Removed the old cache logic"
- ✅ "Computes force directly without caching"

**Why**: History belongs in git; code comments aid current readers.

---

## 7. Biology over Workarounds

**Rule**: Fix root causes, not symptoms.

- ❌ Maternal seeding hack to compensate for missing transcription
- ✅ Implement transcription pathway properly

Temporary instrumentation for diagnosis is fine; remove before merge.

**Why**: Workarounds compound; proper biology is maintainable.

---

## 8. Validation against Data

**Rule**: Simulation behavior must match wet-lab data.

- Cite sources (PMID/DOI) in `data/wetLab/`
- Validation tests in `src/organisms/worm/` check constraints
- Failing validation blocks merge

**Why**: The simulation's value is accuracy, not speed.

---

## 9. Simplest Solution

**Rule**: Avoid over-engineering.

- No helpers for one-time operations
- No abstractions for hypothetical futures
- No error handling for impossible cases

**Why**: Complexity has cost; pay only when needed.

---

## 10. Encapsulation

**Rule**: Variables are always private; avoid protected members.

- ❌ `public: int m_count;`
- ❌ `protected: int m_count;`
- ✅ `private: int m_count;` with `int getCount() const;`

Methods:
- `public` — part of the class API
- `private` — internal implementation
- `protected` — avoid; use only when inheritance genuinely requires it

**Why**: Private variables enforce invariants through accessors; fewer protected members means simpler inheritance.

---

## 11. Small Functions and Classes

**Rule**: Keep functions and classes focused and concise.

- Functions: aim for < 50 lines; split if larger
- Classes: aim for < 500 lines; extract sub-classes if larger
- Each function does one thing; each class has one responsibility

**Why**: Small units are easier to test, review, and reuse.

---

## 12. No Duplication

**Rule**: Define once, reference everywhere.

- All simulation constants live in `src/chemistry/molecules/simConstants.h`
- No magic numbers in code — extract to named constants
- No copy-pasted logic — extract to shared functions
- Comments explain *why*, not *what value*:
  - ❌ `constexpr double FOO = 0.5; // 0.5`
  - ✅ `constexpr double FOO = 0.5; // Srayko et al. 2005, Table 1`

**Why**: Single source of truth; changes propagate automatically; easier to cite sources.

---

## 13. No Raw Pointers for Storage

**Rule**: Member variables must not be raw pointers.

- ❌ `Cell* m_pCell;`
- ✅ `std::weak_ptr<Cell> m_pCell;`
- ✅ `std::shared_ptr<Cell> m_pCell;`

Raw pointers are acceptable for:
- Function parameters (non-owning, call-duration only)
- Return values when lifetime is guaranteed by caller

**Why**: Smart pointers make ownership explicit and prevent use-after-free bugs.

---

## 14. No Multiple Inheritance

**Rule**: A class may inherit from at most one base class.

- ❌ `class Foo : public Bar, public Baz`
- ✅ `class Foo : public Bar` (single inheritance)
- ✅ Composition: `class Foo : public Bar { Baz m_baz; }`

**Why**: Avoids diamond problem, simplifies object layout, makes code easier to reason about.

---

## 15. No Circular Dependencies

**Rule**: Dependencies between folders/libraries must be acyclic.

- If folder A includes from folder B, folder B cannot include from folder A
- Draw a dependency graph; it must be a DAG (no cycles)
- ❌ `biology/` ↔ `chemistry/` (mutual includes)
- ✅ `biology/` → `chemistry/` → `geometry/` (one-way chain)

**Why**: Circular dependencies cause build order issues and indicate tangled design.

---

## 16. No Callbacks

**Rule**: Avoid `std::function` and lambdas for callbacks; use interface pointers instead.

- ❌ `std::function<void(int)> onComplete;`
- ❌ `registerCallback([this](int x) { handle(x); });`
- ✅ `std::weak_ptr<IListener> m_pListener;` with `m_pListener.lock()->onComplete(x);`
- ✅ `std::shared_ptr<IHandler> m_pHandler;` with `m_pHandler->handle(x);`

**Why**: Interface pointers are debuggable, have clear ownership, and avoid hidden captures.

---

## 17. Minimal Templates

**Rule**: Use templates only for simple utility types, not domain objects.

- ✅ `vector<T, n>`, `matrix<T, m, n>` — math utilities
- ✅ `std::shared_ptr<T>`, `std::vector<T>` — standard library
- ❌ `Organelle<T>`, `Process<Config>` — domain objects
- ❌ Template metaprogramming

**Why**: Templates increase compile times, produce cryptic errors, and obscure domain logic.

---

## 18. No Pointer Cycles

**Rule**: Object ownership must be acyclic; avoid back-references when possible.

- If A holds `shared_ptr<B>`, B cannot hold `shared_ptr<A>`
- ❌ `shared_ptr` in both directions (memory leak)
- ⚠️ `weak_ptr` for back-reference (acceptable but not ideal)
- ✅ No back-reference at all (best)

Prefer designs where child objects don't reference parents.

**Why**: Pointer cycles cause memory leaks with `shared_ptr` and complicate reasoning about lifetimes.

---

## 19. Small Projects

**Rule**: Split large libraries into focused sub-libraries.

- Aim for < 20 source files per project
- Each project has a single, clear responsibility
- Prefer more small projects over fewer large ones

**Why**: Small projects build faster, have clearer dependencies, and are easier to understand.

---

## 20. Unique File Names

**Rule**: Avoid duplicate file names across the codebase.

- ❌ `biology/Cell.h` and `chemistry/Cell.h`
- ✅ `biology/Cell.h` and `chemistry/GridCell.h`

**Why**: Prevents include ambiguity and confusion when searching/navigating.

---

## 21. No Nested Namespaces

**Rule**: Use flat, single-level namespaces only.

- ❌ `namespace visLib::internal { }`
- ❌ `namespace biology { namespace organelles { } }`
- ✅ `namespace visLib { }`

**Why**: Nested namespaces add verbosity without proportional benefit in a single-team codebase.

---

## 22. Include Paths

**Rule**: Includes use either same-folder or full path from solution root.

- ✅ `#include "Foo.h"` — file in same folder
- ✅ `#include "biology/organelles/Cell.h"` — full path from `src/`
- ❌ `#include "../utils/Helper.h"` — relative path with `..`
- ❌ `#include "Cell.h"` — ambiguous (which Cell.h?)

**Why**: Explicit paths prevent ambiguity and make file locations obvious.

---

## 23. Formatting

**Rule**: Consistent whitespace.

- 4 spaces for indentation (no tabs)
- Preserve existing style in untouched lines
- Match surrounding brace placement

**Why**: Reduces diff noise; makes reviews faster.

---

## Using These Principles

When reviewing a MR with conflicting approaches:

1. Identify which principle applies
2. The approach that better satisfies the principle wins
3. If no principle applies, add a new one via separate MR
