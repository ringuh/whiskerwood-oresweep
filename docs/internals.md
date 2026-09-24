# OreSweep internals

Notes from reverse-engineering `Whiskerwood-Win64-Shipping.exe`, game version **0.7.207** (UE 5.6). Addresses are for that build with the default image base `0x140000000`. At runtime the mod finds everything by signature scan, so the addresses are for reading along in a disassembler only.

## Why this can't be a Blueprint mod

- `AMineTool` (`/Script/ProjectArco.MineTool`) exposes only `GetInfo()` → `FMineToolInfo` and `GetSurveyInfo()` → `FSurveyInfo` to Blueprint. `HandleHudAction` / `ReceiveHudAction` are native only.
- No Blueprint-callable function reads a terrain cell's contents or adds/removes a mining mark. `ETerrainFlag` (Surveyed, Tilled, Solid, Terraformed, WasBlocked) exists, but nothing exposes it per cell.
- The ore tab bar in the mining tool (All Ores / Gold Ore / ...) is the surveyor overlay (`FSurveyInfo.surveyButtons`, `currentButtonIdx`). It only changes what is highlighted, not what gets selected.

## Loading

The exe statically imports `DSOUND.dll` by ordinals 1, 3, 6, 8, 11 and 12. `dsound.dll` is not a KnownDLL, so Windows loads a copy placed next to the exe first. OreSweep's `dsound.dll` exports all 12 DirectSound entry points with the original ordinals. Each is a `jmp` through a pointer that `DllMain` resolves from `%SystemRoot%\System32\dsound.dll`. `DllMain` then starts a worker thread that scans for the game code and installs the hook.

## Game state

- `GetGameState` at `0x1449ace60` is `mov rax, [rip+X]; ret`. The mod reads that global pointer directly.
- The mining-tool drag state is at `[game + 0x3508]`:
  - `+0x2e8`: the set of currently selected cells (a `TSet`/sparse array; 40-byte elements with an `FIntVector` at +0).
  - `+0x4a9`: a byte flag. When it is non-zero the commit function takes its other branch, which emits the same actions with the add byte = 0 (most likely the remove/cancel mode; not verified in game).
- The terrain grid is at `game + 0x2a58` (the offset is read from `add rax, imm32` in the mine-tool info code):

  | Offset | Field |
  |---|---|
  | `+0x00` | origin `FIntVector` |
  | `+0x0C` | size `FIntVector` |
  | `+0x18` | `cells*`; cell index = `((z*sizeY + y) * sizeX + x)`, 0x1C bytes per cell |
  | `+0x1C0` | ore name table (`FName*`), indexed by ore index |

  The game's own lookup is `0x144622660 (grid, const FIntVector*) -> cell*`. The mod re-implements it and checks the `imul rax,rax,0x1C; add rax,[rcx+0x18]` bytes to confirm the layout is unchanged.

- Terrain cell:

  | Offset | Meaning |
  |---|---|
  | `+0` byte | flags: `0x80` = surveyed, `0x20` = has resource slots, low 3 bits = terrain type |
  | `+1` byte | resource amount |
  | `+4` u16 | 8 two-bit slots; the first slot equal to `0b11` gives the ore index (`0x14463d210`). No such slot means plain **stone** |

  This matches `AMineTool::GetInfo` (`≈0x144871870`): unsurveyed cells go to `unsurveyedCellCount` (+0x78), ore index < 0 is counted as the FName `stone`, and a surveyed cell without `0x20` logs "Invalid mine tool drag."

## The hook

The commit function (`≈0x14486d960`) runs when a drag is released. It optionally shows the `assignMines` tutorial hint, then loops over the selected-cell set and appends one 80-byte sim action per cell to a `TArray`:

```
WORD  [+0x00] = 0x2B          ; mine-mark action
IntVector [+0x0C] = cell
BYTE  [+0x48] = 1 (add) / 0 (remove)
```

OreSweep patches only the **add** loop (loop head `0x14486de11`, identified by the whole 232-byte loop body including `mov byte [rbx+0x48], 1`). The 14 bytes at loop head + 0x1B:

```
33 C9                    xor  ecx, ecx
48 C7 44 24 74 00000000  mov  qword [rsp+0x74], 0
0F 57 C0                 xorps xmm0, xmm0
```

are replaced by `jmp qword [rip+0]; dq ore_tramp`. On entry `rax` = element index and `rdx` = the set's data pointer.

`ore_tramp` (in `src/stubs.S`) saves the volatile registers and xmm0-5, calls `OreFilter_ShouldSkip(&cells[rax].pos)`, and restores everything. Then:

- **Keep:** it replays the 14 overwritten bytes and jumps back to hook + 14.
- **Skip:** it advances the set iterator exactly as the game's loop tail does (`mov eax,[rbp-0x34]; not eax; and [rbp-0x28],eax; lea rcx,[rbp-0x38]; call TSetIterator::operator++` (`0x14118d760`)), then jumps to the loop head. No sim action is created for that cell.

`OreFilter_ShouldSkip` returns "skip" only when the hotkey is down (`GetAsyncKeyState`) and the cell is surveyed plain stone, or unsurveyed (unless `KeepUnsurveyed=1`). Filtering happens before the sim actions are built, so the game's deterministic simulation and replays only see normal mining orders.

## Useful strings in the exe

`mineTool.bedrock`, `mineTool.unknown`, `mineTool.invalid`, `error.toodeep`, `Invalid mine tool drag.`, `resource.allores`, `OreIndex`, `MineIndicator`, `SurveyorWidget`, `assignMines`, `stone`. Blueprint → C++ HUD actions go through `APlayerController_Play::HandleHudAction(FHudAction)` (an action string plus param fields).

## If a game update breaks it

`OreSweep.log` lists the match count for each signature (`scan: loop=1 getter=2 grid=1 lookup=1` is healthy). Re-find the commit loop by searching for `66 C7 03 2B 00` (`mov word [rbx], 0x2B`) near `C6 43 48 01`, then update the pattern and the `+0x1B` / `+0xDE` / `+0xE3` offsets in `InstallHook`.
