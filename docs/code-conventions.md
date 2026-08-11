# Code conventions for Dusk

## The original game's names are Japanese — leave them alone

Twilight Princess was written by a Japanese team, and the decompilation keeps
its symbol names. So identifiers throughout the original codebase are romaji
(Japanese in Latin letters), abbreviated Japanese, or English spelled by ear:
`kankyo` (環境) is *environment*, `dKyr_drawSibuki` draws 飛沫 *shibuki* — spray,
and `dKyw_wether_move` is the weather system.

**Do not rename them and do not correct the misspellings** (`wether`,
`Schejule`, `Sord`, `Blure`, `parcent`, `vectle`, `resorce`, `tresure`). A
rename breaks the match with the upstream decompilation we PR fixes back to, and
breaks everyone's grep. If a name needs explaining, add a comment.

**Code we write ourselves** — everything under `src/dusk/` — uses ordinary
English `camelCase`. Do not romanize anything new.

Full reference, including how to decode an unfamiliar name and why a search can
come back empty for a symbol that exists: [`japanese-naming.md`](japanese-naming.md).

## Upstream when appropriate

Bug fixes, documentation improvements, code cleanup, etc that also apply to the [original decompilation project](https://github.com/zeldaret/tp) should preferably be PR'd there.

## Properly indicate Dusk-modified code

When modifying original game code (i.e. in decomp) for Dusk's purposes, please clearly delineate such code as being Dusk-specific. Generally, this can be done by using `#if TARGET_PC` and keeping the original code in place. Use `#if AVOID_UB` for Undefined Behavior fixes to the original codebase.

## Miscellaneous things

* The original codebase makes heavy use of global `operator new` and similar overloads to allocate into a strict tree of heaps. This would cause many linkage headaches for us, so effectively all uses of `new` or `delete` in the original game code have been replaced with `JKR_NEW`, `JKR_DELETE`, or similar macros. See `JKRHeap.h` for the full list.  
