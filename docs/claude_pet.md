# Claude Pet: a desk display for many Claude Code sessions

The box shows a small herd of pets, one per local Claude Code session. A pet that waits for you stands out enough that you notice it from the corner of your eye. You answer on the keyboard as usual. The box has no buttons, no sound and no text input for this app. The laptop runs one daemon. It turns Claude Code hook events into a small state snapshot and writes the snapshot to the box over the existing BLE link.

Facts about Claude Code below come from code.claude.com/docs (hooks, statusline, monitoring-usage, sub-agents), read on 2026-10-20. Facts about the box come from this repo, with file:line.

## 1. Goals and non-goals

Goals
- Show at a glance, across up to 6 sessions at once, which sessions need me, which work, and which are done.
- A session that needs me must never be hidden, even with 10 sessions open.
- Each pet keeps its place and its look for the session's whole life, so I learn "the frog top-left is the widgets repo".
- Never slow down or break a Claude Code session, even when the daemon or the box is off.

Non-goals
- No input on the box. Answering happens in the terminal.
- No sound. The piezo stays for the vario.
- No prompt, tool input or output text leaves the daemon.
- Cloud sessions. Their hooks run in the cloud and can't reach a daemon on the Mac.

## 2. Terms

| Term | Meaning |
|---|---|
| session | One Claude Code session, keyed by `session_id` from the hook input |
| subagent | An agent inside a session; hook input carries `agent_id` and `agent_type`. It is counted on its session's pet, not shown as its own pet |
| state | The session's current state, one of section 4 |
| slot | One of 6 places on the screen. A visible session owns one slot for its whole life |
| snapshot | The full state of all slots, sent as one BLE write |
| daemon | `host/claude_pet.py` on the laptop |
| pet app | The new app on the box |

## 3. Architecture

```
Claude Code sessions (terminal, IDE, desktop, background jobs)
  │ hooks: type "http", POST JSON to 127.0.0.1:47800/hook   (no script, no process per event)
  │ statusline wrapper: POST the statusline JSON to /statusline, then run the user's own statusline
  │ optional OpenTelemetry: cost and tokens per session.id
  ▼
host/claude_pet.py (daemon, launchd agent)
  - session table, state machine (section 4), slot assignment (section 5)
  - snapshot encoder (section 7), BLE writer with reconnect (bleak)
  ▼ BLE write, opcode 0x30 PET_SNAPSHOT, on every change + heartbeat every 10 s
box: LaptopBle callback → decodeCommand → static PetMailbox (latest snapshot wins)
  ▼ IPetFeed port
pet app: change-driven drawing, animation timing on the box
```

Why the daemon sends a full snapshot, not events
- A lost write, a reconnect, a box reboot or a daemon restart heals with the next snapshot.
- The box needs no session logic. All rules live in one place, the daemon, where they can be unit-tested with recorded hook sequences.

## 4. Session state machine (daemon)

### 4.1 States, ranked by urgency

| Rank | State | Meaning | Pet |
|---|---|---|---|
| 1 | `PERMISSION` | Blocked on a permission prompt | Waves, jumps high; its cell inverts (black) and blinks at 1 Hz |
| 2 | `QUESTION` | Asked me something (AskUserQuestion, an MCP elicitation, a background agent that needs input) | Same as `PERMISSION`, holds a "?" sign |
| 3 | `FAILED` | The turn ended with an API error (`StopFailure`) | Lies down, sick; cell border dashed |
| 4 | `DONE` | Finished its turn; the result is fresh | Sits facing out, small happy bounce every few seconds |
| 5 | `WORKING` | A turn is running, or background tasks run after the turn | Tool animation (section 6) |
| 6 | `IDLE` | Done and stale, or never prompted | Asleep, still, "z" every few seconds |

Ranks 1 and 2 are the only states meant to catch peripheral vision. Everything else moves little or not at all. Peripheral vision reacts to motion and large contrast, not to detail.

### 4.2 Transitions

| Hook event (and filter) | New state |
|---|---|
| `SessionStart` | `IDLE` (create the session if new) |
| `UserPromptSubmit` | `WORKING` |
| `PreToolUse`, `PostToolUse`, `PostToolUseFailure` | `WORKING`; set the tool category from `tool_name` |
| `PreToolUse` with `tool_name` = `AskUserQuestion` | `QUESTION` |
| `PermissionRequest` | `PERMISSION` |
| `Notification`, `notification_type` = `permission_prompt` | `PERMISSION` |
| `Notification`, `notification_type` = `elicitation_dialog`, `elicitation_url_dialog` or `agent_needs_input` | `QUESTION` |
| `Elicitation` | `QUESTION` |
| `ElicitationResult`, `PermissionDenied` | `WORKING` |
| `Notification`, `notification_type` = `idle_prompt` | no change (the session is already `DONE` or `IDLE`) |
| `Notification`, `notification_type` = `agent_completed` | `DONE` |
| `Stop` with an empty `background_tasks` | `DONE` |
| `Stop` with a non-empty `background_tasks` | `WORKING` (background) |
| `StopFailure` | `FAILED` |
| `SessionEnd` | remove the session (section 5.3) |
| `DONE` for 20 minutes with no event | `IDLE` |
| `FAILED` for 60 minutes with no event | `IDLE` |

Rules that apply to every event
- Any event of a session that carries no `agent_id` is from the main agent. An event with an `agent_id` updates the subagent set (4.3) and never moves the session out of `PERMISSION` or `QUESTION`. A subagent that works while the main agent asks me must not hide the question.
- Clearing `PERMISSION`: the docs name no event for "the user answered the prompt". The next `PreToolUse`, `PostToolUse`, `PostToolUseFailure`, `PermissionDenied` or `UserPromptSubmit` of the main agent clears it. Verify in a test session that `PostToolUse` of the approved tool arrives (open question 1).
- An event for an unknown `session_id` creates the session. This covers a daemon restart in the middle of a session.
- Store per session: `state`, `since` (time of the last state change), `last_event`, `tool_category`, `subagents`, `repo_key`, `context_pct`, `cost_usd`.

### 4.3 Subagents

- `SubagentStart` adds `agent_id` to the session's set; `SubagentStop` removes it.
- The set is a set, not a counter: a duplicate or a missing stop can't drift it below zero.
- A subagent with no event for 30 minutes is dropped from the set (a crash leaves no `SubagentStop`).
- The pet shows up to 4 small followers. Above 4 it shows 4 and a "+".
- `SessionEnd` clears the set.

### 4.4 Several sessions in one repo

Working in parallel often means several sessions in the same repo, each in its own worktree. The `repo_key` is the git common dir of `cwd` (`git rev-parse --git-common-dir`, cached per `cwd`), so all worktrees of a repo share it. Outside git, `repo_key` is `cwd`.
- `species` = hash(`repo_key`) mod the number of species. One repo always gets the same animal.
- `variant` = the lowest variant (hat, scarf, glasses, none) not used by another live session with the same `repo_key`. Two sessions in one repo are the same animal with different accessories.
- `label`: the first 4 characters of the repo name, upper case, under the pet. Six identical frogs without labels would be guesswork. The label is the only text on the screen.

## 5. Slots: many sessions on 6 places

### 5.1 Stable places

- A visible session keeps its slot until it ends or is evicted. Pets never reshuffle by urgency: a moving layout breaks "top-left is the widgets repo".
- A new session takes the lowest free slot.

### 5.2 More than 6 sessions

- When all 6 slots are taken, a new session goes to the hidden list.
- When a hidden session becomes `PERMISSION` or `QUESTION`, it takes the slot of the visible session with the lowest rank. On a tie it takes the slot of the session with the oldest `last_event`. A waiting session is never hidden while a non-waiting one is visible.
- If all 6 visible sessions wait and a 7th starts to wait, the top bar shows the number of hidden waiting sessions, inverted.
- When a slot frees up, the hidden session with the best rank (then the newest `last_event`) takes it.
- The top bar always shows the hidden count, for example "+3".

### 5.3 Ending and stale sessions

- `SessionEnd` frees the slot after a short "waves goodbye" (the box plays it when a slot disappears from the snapshot).
- A killed terminal sends no `SessionEnd`. A session with no event for 2 hours, in `IDLE` or `DONE`, is removed.
- A session in `PERMISSION` or `QUESTION` is never removed for staleness by the 2-hour rule; it is removed after 12 hours.
- `SessionEnd` with `reason` = `clear` followed by `SessionStart` with `source` = `clear` in the same `cwd` within 5 s keeps the slot and the variant. The pet wakes up instead of leaving and hatching again.

## 6. Tool categories

| Category | `tool_name` | Animation |
|---|---|---|
| `THINK` | none yet in this turn | Thinks, thought bubble |
| `READ` | `Read`, `Grep`, `Glob`, `LSP` | Reads a book with glasses |
| `EDIT` | `Edit`, `Write`, `NotebookEdit` | Writes with a pencil |
| `SHELL` | `Bash` | Hammers |
| `WEB` | `WebFetch`, `WebSearch` | Fishes with a rod |
| `AGENT` | `Agent`, `Task*`, `Workflow` | Talks to the followers |
| `MCP` | `mcp__*` | Talks on a phone |
| `OTHER` | anything else | Walks around |

The tool category comes only from the main agent. Subagents show as followers, each walking.

## 7. Link: BLE protocol

### 7.1 What exists

- The laptop link is one service (`D15A0001-5B7A-4C3E-9F10-2A6E8C4B7D01`) with one write characteristic (`D15A0002-...`), WRITE and WRITE_WO_RESP, max 244 bytes (`firmware/src/apps/vario/hw/laptop_ble.cpp:13-15`, `laptop_ble.h:33-34`).
- The box never sends back; there is no notify (`laptop_ble.cpp:13-15`).
- Non-draw opcodes go through `decodeCommand` (`apps/vario/app/command_decode.cpp:7-54`) into a 4-entry `SpscRing<Command,4>` (`laptop_ble.h:43`). Only the vario reads it (`apps/vario/app/app.cpp:455`). Unknown opcodes are dropped (`command_decode.cpp:53`).
- `LaptopBle laptop` is static in `main_box.cpp:74` and outlives apps.
- The box allows one laptop connection (`Bluefruit.begin(1, 1)`, `main_box.cpp:291`).
- `LaptopGate` stops advertising and disconnects while the active app is in game mode (`main_box.cpp:78-86`, `platform/ports.h:114-121`). **So the pet app must be change-driven (`frameIntervalMs() == 0`).**

### 7.2 Changes

1. New opcode `0x30 PET_SNAPSHOT`. `decodeCommand` routes it, not to the vario's ring, but to a new static `PetMailbox` in `main_box.cpp`: one buffer, latest write wins, with a sequence number. The BLE callback only copies bytes (CLAUDE.md rule: callbacks only copy into queues). A ring would fill up while the pet app isn't open; a mailbox can't.
2. New port `IPetFeed` (`platform/ports.h`): `bool latest(PetSnapshot& out, uint32_t& seq)` and `uint32_t lastWriteMs()`. `main_box.cpp` passes it to the pet app, like the other ports.
3. Writes use WRITE (with response), so the daemon knows a snapshot arrived. Snapshots are small and rare, so the cost is nothing.
4. Add a notify characteristic only if open question 3 says so. Nothing in this spec needs it.

### 7.3 Snapshot format (version 1)

All integers are unsigned, little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | opcode `0x30` |
| 1 | 1 | version `1` |
| 2 | 1 | seq (wraps) |
| 3 | 1 | flags: bit 0 `bowl_is_limit`, bit 1 `daemon_paused` |
| 4 | 1 | bowl: 0..100, see section 8 |
| 5 | 1 | hidden sessions (cap 99) |
| 6 | 1 | hidden waiting sessions (cap 99) |
| 7 | 1 | slot count n (0..6) |
| 8 | 12 × n | slots |

Slot (12 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | slot index 0..5 |
| 1 | 1 | state (section 4.1, 0 = `IDLE` ... 5 = `PERMISSION`) |
| 2 | 1 | tool category (section 6) |
| 3 | 1 | subagents (0..15) |
| 4 | 1 | species |
| 5 | 1 | variant |
| 6 | 1 | context use 0..100, 255 = unknown |
| 7 | 1 | minutes in this state (cap 255) |
| 8 | 4 | label, ASCII, padded with spaces |

Six slots = 80 bytes. That fits one write (244) and the `Command` text buffer (200, `apps/vario/app/ports.h:34-38`).

Rules
- The box rejects a snapshot with a wrong version, a length that doesn't match n, a slot index above 5 or an unknown state. It keeps the last good snapshot. A unit test covers each case (CLAUDE.md: check external input where it enters).
- "Minutes in this state" lets the box escalate a long wait after a reconnect, without a clock of its own.
- The daemon sends a snapshot on every change, at most 5 per second (changes inside 200 ms merge into one), plus a heartbeat every 10 s.
- No snapshot for 30 s: the box shows the herd asleep and a small "unplugged" sign in the top bar. A paused daemon (`daemon_paused`) shows the same sign without the sleep.

## 8. Cost, limits and context

The top bar shows one "food bowl" that fills up as the day's use grows.

| Source | Field | Used for |
|---|---|---|
| statusline JSON | `rate_limits.five_hour.used_percentage` (Pro/Max only) | Bowl, when present (`bowl_is_limit` = 1) |
| OpenTelemetry | `claude_code.cost.usage` per `session.id` | Bowl as today's cost: 0..100 of a daily budget from the daemon config |
| statusline JSON | `cost.total_cost_usd` | Fallback for cost, per session |
| statusline JSON | `context_window.used_percentage` | The pet's belly (slot byte 6) |

- The statusline runs on each new assistant message, debounced by 300 ms, and is hidden during permission prompts (statusline docs). That is enough for the belly and the bowl. It isn't enough for state: state comes only from hooks.
- The docs don't say whether the statusline runs in `-p` or background sessions. Without it, the belly shows "unknown" and cost comes from OpenTelemetry.
- The statusline wrapper must keep the user's own statusline: it posts the JSON to the daemon in the background with a 200 ms timeout, then runs the user's command with the same stdin and prints its output.
- Cost is a client-side list-price estimate (statusline docs). The bowl is a mood, not an invoice.
- The pets get rounder as the bowl fills (3 steps). After `PostCompact` the belly empties with a short animation.

## 9. Daemon (`host/claude_pet.py`)

- Python 3, stdlib `http.server` (threaded) bound to `127.0.0.1` only, and `bleak` for BLE.
- It answers every POST at once with `200` and an empty body. An HTTP hook's response body is read as hook output, so an empty body makes no decision. Work happens after the reply.
- Privacy: it reads only `session_id`, `agent_id`, `hook_event_name`, `tool_name`, `notification_type`, `cwd`, `background_tasks` (length only), `reason`, `source`. It drops the rest of the body at once and never logs prompts, tool input or output.
- Connects to the box by the service UUID, keeps the connection and reconnects with backoff (1, 2, 5, 10 s). After a Mac wake, it reconnects and sends a snapshot at once.
- `claude_pet.py pause` / `resume` releases the BLE link, so `ctl.py` and `igc_replay.py` can connect (only one laptop link fits, section 7.1).
- `claude_pet.py status` prints the session table (no text from the sessions).
- `claude_pet.py install` writes the launchd agent and prints the hook and statusline config to paste. It never edits `~/.claude/settings.json` itself.
- `--fake-ble FILE` writes snapshots to a file instead of BLE. The emulator plays that file (section 11).

### 9.1 Hook configuration

One `http` hook per event, all to `http://127.0.0.1:47800/hook`, with `timeout` 1. A daemon that isn't running gives "connection refused" right away. The docs say a connection failure or a non-2xx reply is a non-blocking error, so no session waits on the daemon.

Events: `SessionStart`, `SessionEnd`, `UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `PostToolUseFailure`, `PermissionRequest`, `PermissionDenied`, `Notification`, `Elicitation`, `ElicitationResult`, `SubagentStart`, `SubagentStop`, `Stop`, `StopFailure`, `PostCompact`.

`SessionEnd` hooks have a 1.5 s budget by default (hooks docs); a local POST fits.

## 10. Pet app (box)

- Name in the registry: "Agents", sub "Claude Code", startable, so the box can boot into it on the desk.
- Change-driven (`frameIntervalMs() == 0`), required by `LaptopGate` (section 7.1). It animates through `wantsFrame` and `nextWakeMs`:

| What is on screen | Frame rate |
|---|---|
| Any slot in `PERMISSION` or `QUESTION` | 6 fps for the jumps, the cell blinks at 1 Hz |
| Something `WORKING`, nothing waiting | 2 fps |
| Only `DONE` and `IDLE` | 1 frame every 3 s ("z", the bounce) |
| No snapshot for 30 s | no frames until a snapshot arrives |

- Escalation of a long wait, from "minutes in this state": after 2 minutes the jump grows; after 10 minutes the pet holds a sign. Still no sound.
- Layout, 240 × 400 portrait: a 40 px top bar (bowl, hidden "+n", hidden waiting count, "unplugged"), then 2 columns × 3 rows of 120 × 120 cells. Each cell: pet, followers, belly, 4-character label.
- Sprites: 4-bit `Gray4` with transparency, converted with `host/img2gray.py` (`platform/gfx/gray.h:13-18`). 8 species × 4 variants × (6 states + 8 tool animations) × 2-3 frames. Draw the variants as small overlays (hat, scarf, glasses) over the species, not as separate sprites, to keep flash use down.
- When a slot disappears from the snapshot, the pet waves for 2 s, then the cell is empty. When a slot appears, an egg hatches for 2 s.
- RAM: the app lives in the `AppArena` (`platform/lazy_app.h`). It holds the last snapshot and per-slot animation state, well under 1 kB. Sprites stay in flash. `scripts/ram_check.py` must pass (48 kB heap floor, `scripts/ram_check.py:32`).
- The box image registers 6 apps (`main_box.cpp:323-330`), so a 7th fits under `MAX_APPS = 8` (`app_host.h:136`). The examples image already has 8 (`main_box.cpp:309-317`): leave the pet app out of it.

## 11. Tests

Daemon (pytest, no BLE)
- Recorded hook sequences as JSON fixtures, replayed through the state machine, with the expected snapshot after each step:
  1. Prompt, tools, `Stop`: `WORKING` → `DONE` → `IDLE` after 20 minutes (fake clock).
  2. Permission prompt approved: `PERMISSION` → `WORKING` on the next main-agent event.
  3. Permission prompt while 2 subagents work: stays `PERMISSION`, followers = 2.
  4. `AskUserQuestion`, then the answer.
  5. `Stop` with background tasks: stays `WORKING`.
  6. `StopFailure`: `FAILED`.
  7. 8 sessions, then a hidden one asks for permission: it takes the slot of the lowest-ranked visible one; the top bar counts stay right.
  8. 3 sessions in 3 worktrees of one repo: same species, 3 variants, same label.
  9. Daemon restart in the middle of a session: the first event recreates it.
  10. A killed terminal: removed after 2 hours; a waiting one after 12 hours.
  11. `/clear`: same slot, same variant.
- Encoder: byte layout of each field; 6 slots = 80 bytes.
- The HTTP handler returns 200 with an empty body, and drops the request body.

Box (emulator, `firmware/emu`, following `docs/PLATFORM.md` section 7)
- `pet_tests.h`: the decoder rejects each bad case in 7.3 and keeps the last good snapshot.
- `pet_shots.h`: PNG shots of one screen per state, of the full herd with 6 slots, with followers, with "+3" and hidden waiting, and "unplugged".
- `./build.sh play pet --feed FILE`: plays a `--fake-ble` file from the daemon. That gives an end-to-end run without hardware: real Claude Code sessions → daemon → file → emulator window.
- `scripts/check.sh` passes before each commit (CLAUDE.md).

On the device
- A 1-hour run with 4 real sessions; check that no waiting state is missed and that the link recovers after the Mac sleeps.
- Note the battery drop over a night on the desk, as there is no current sensor (`NOTES.md:211-220`).

## 12. Delivery, in PR order

1. Box: opcode `0x30`, `PetMailbox`, `IPetFeed`, decoder and its tests. No app yet.
2. Box: pet app with placeholder shapes, change-driven timing, shots, `play --feed`.
3. Daemon: state machine, slots, encoder, fixtures, `--fake-ble`. No BLE yet.
4. Daemon: HTTP hooks, BLE writer, `pause`/`resume`, `install`, launchd.
5. Cost and context: statusline wrapper, optional OpenTelemetry.
6. Art: the real sprites for 8 species, variants and tool animations.

Each step works on its own: after step 3, the emulator already shows the herd of your real sessions.

## 13. Open questions

1. Does `PostToolUse` of an approved tool arrive right after the permission prompt is answered, so `PERMISSION` clears at once? Check in a test session; if not, `PreToolUse` of the next tool clears it later.
2. Do background jobs and `-p` sessions send `Notification` and run the statusline? The docs don't say. Without `Notification`, a background job's `PERMISSION` comes only from `PermissionRequest`.
3. Should the box report back (a notify characteristic): the snapshot `seq` it shows and its battery level? It would let the daemon show "box battery low" in `status`. Not needed for version 1.
4. Daily budget for the cost bowl, when there is no rate-limit field: what number?
5. Later, not in version 1: pets that grow over days of use, stored in `BoxSettings`.

## Amendments (2026-10-20)

These changes were approved after the first review. Where they differ from the text above, they win.

### A. The laptop link is a platform service

- Code layout (box side):
  - `platform/link/opcode_dispatch.h` (pure): consumers register an opcode range with `OpcodeDispatch::add(lo, hi, sink)`. The BLE write callback calls `dispatch(data, len, millis())`. A sink only copies bytes; decoding happens later in the loop.
  - `platform/hw/laptop_link.{h,cpp}`: the service, the write characteristic, the status characteristic, advertising, connect and disconnect. It knows no opcode.
  - `apps/vario/hw/laptop_ble.{h,cpp}`: the vario's drawing opcodes (0x01 to 0x04, frame buffer, `hostDrawing`) as one sink.
  - `apps/vario/app/command_queue.h` (pure): the vario's commands (0x10 to 0x16, 0x70 and 0x71) as another sink. It is the old `decodeCommand` plus the 4-entry ring, unchanged.
  - `platform/pet/`: `pet_snapshot.h` (strict decoder, `Mailbox`), `pet_feed.h` (`pet::Feed`, the sink for opcode 0x30 and the `IPetFeed` implementation), `pet_status.h` (the status the box reports back).
- The laptop protocol did not change for the vario. The status characteristic is new and only adds.
- `Mailbox` is a seqlock over one raw buffer: the callback copies the bytes and the time (`put`), the loop decodes the newest write (`latest`). A bad newest write leaves the last good snapshot in place and does not refresh its time stamp.
- `IPetFeed` has a third method, `markShown(seq)`: the pet app tells which snapshot it drew, and the status reports it.

### Enumerations (the spec left the numbers implicit)

| State | Value | Tool category | Value |
|---|---|---|---|
| `IDLE` | 0 | `THINK` | 0 |
| `WORKING` | 1 | `READ` | 1 |
| `DONE` | 2 | `EDIT` | 2 |
| `FAILED` | 3 | `SHELL` | 3 |
| `QUESTION` | 4 | `WEB` | 4 |
| `PERMISSION` | 5 | `AGENT` | 5 |
| | | `MCP` | 6 |
| | | `OTHER` | 7 |

Species is 0 to 7 and variant 0 to 3 (none, hat, scarf, glasses); the box takes larger numbers modulo what it can draw.

### Decoder rules (the box rejects, keeps the last good snapshot)

Wrong opcode or version; length not exactly 8 + 12 n; n above 6; a slot index above 5 or used twice; state above 5; tool category above 7; bowl above 100; hidden or hidden waiting above 99; subagents above 15; context use 101 to 254 (255 = unknown); a label byte outside 0x20 to 0x7e.

### B. The box reports back (open question 3, now in version 1)

A second characteristic in the same service: `D15A0003-5B7A-4C3E-9F10-2A6E8C4B7D01`, READ + NOTIFY, 8 bytes. The box notifies on a change (a new shown snapshot, the active app, the battery percent) and every 30 s while connected, and at once after a connection. The daemon enables notifications on connect. A read always returns the last value.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | version `1` |
| 1 | 1 | `seq` of the last snapshot the pet app drew (valid when flag bit 0 is set) |
| 2 | 1 | flags: bit 0 = the pet app has drawn a snapshot since the box started, bit 1 = the pet app is the active app |
| 3 | 1 | battery percent, 255 = unknown |
| 4 | 2 | battery millivolts, little-endian, 0 = unknown |
| 6 | 1 | active app id: the app's index in the box image (0 vario, 1 Tetris, 2 compass, 3 Settings, 4 home, 5 sim, 6 intro, 7 Agents), 255 = none |
| 7 | 1 | reserved, 0 |

The daemon can show "box battery low" and "box is not showing the herd" (another app is active) from this.

### C. No clock sleep under the pet app

The box has no idle auto-sleep: the clock sleep starts only from the system sheet (`Sleep`) or the power button logic in `power_nrf`. The pet app does not request it and nothing times out under it. The user can still choose Sleep in the sheet. The app does not hold the screen against that.

Desk use means USB power. A box on its battery with a refreshing display and a BLE link will not last a night; the pet app draws at most 6 fps while a session waits and one frame every 3 s otherwise, but the radio is on all the time.

### Step 2: the pet app and the feed file

- The app is `apps/pet/` ("Agents", sub "Claude Code", startable), app index 7 in the box image. It is left out of the examples image (that one already has 8 apps).
- Feed file for the emulator (shared with the daemon's `--fake-ble FILE`): text, one snapshot per line, `<ms> <hex>`; `<ms>` is milliseconds since the file start (decimal), `<hex>` is the full snapshot bytes as lowercase hex without spaces, starting with `30`; lines starting with `#` are comments. `./build.sh play pet --feed FILE` plays it; `firmware/emu/fixtures/pet_demo.feed` is a hand-made example that walks through every state and tool.
- Placeholder art is drawn from shapes in `pet_art.cpp` (8 silhouettes, 3 overlays, per-state and per-tool motion); the real sprites are step 6 of the delivery plan and replace only that file.
