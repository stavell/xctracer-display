# Claude Pet daemon (step 3: no BLE yet)

Turns Claude Code hook events into the box's pet snapshot (opcode 0x30, version 1). This step writes the
snapshots to a feed file (`--fake-ble`) that the box emulator plays.

    .venv/bin/python host/claude_pet.py serve --fake-ble /tmp/pet_feed.txt   # 127.0.0.1:47800
    .venv/bin/python host/claude_pet.py status                              # session table, no session text
    .venv/bin/python host/claude_pet.py install                             # prints hooks JSON + launchd plist, writes nothing
    .venv/bin/python host/claude_pet/test_claude_pet.py                     # unittest; also run by scripts/check.sh

Feed format (do not change; the emulator plays it): text, one snapshot per line `<ms> <hex>`, ms since the file was
opened (decimal), snapshot bytes as lowercase hex without spaces (starts with `30`); `#` lines are comments.

State numbers in the snapshot: IDLE 0, WORKING 1, DONE 2, FAILED 3, QUESTION 4, PERMISSION 5 (higher = more
urgent). Tool categories: THINK 0, READ 1, EDIT 2, SHELL 3, WEB 4, AGENT 5, MCP 6, OTHER 7.

Beyond the spec's list, `serve` also answers `GET /status` (used by `status`) and `statusline` is a small wrapper command
(posts the statusline JSON to `/statusline` with a 200 ms timeout, then runs `--then CMD` with the same stdin).
The bowl falls back to `--daily-budget` USD (default 0 = off).

## Claude Code docs check (read 2026-10-20)

Sources: https://code.claude.com/docs/en/hooks and https://code.claude.com/docs/en/statusline

Confirmed
- `type: "http"` hooks exist: `{"type":"http","url":...,"timeout":seconds,"headers":{...},"allowedEnvVars":[...]}`.
  Default timeout is 600 s for http hooks, so we set `timeout: 1`.
- Failure handling: a connection failure, a timeout and a non-2xx reply are non-blocking errors, unless the hook sets
  `onFailure: "block"` (we never set it). A 2xx with an empty body is success with no decision; a 2xx with a
  non-JSON body is a non-blocking error (so the empty body matters).
- Events used all exist: SessionStart, SessionEnd, UserPromptSubmit, PreToolUse, PostToolUse, PostToolUseFailure,
  PermissionRequest, PermissionDenied, Notification, Elicitation, ElicitationResult, SubagentStart, SubagentStop, Stop,
  StopFailure, PostCompact.
- Common input: `session_id`, `cwd`, `hook_event_name`, `agent_id`, `agent_type` (the last two only inside a subagent), plus
  `transcript_path`, `permission_mode`, ... (we drop them).
- Notification: field `notification_type`; values `permission_prompt`, `idle_prompt`, `auth_success`,
  `elicitation_dialog`, `elicitation_url_dialog`, `elicitation_complete`, `elicitation_response`, `agent_needs_input`,
  `agent_completed`, `quota_auto_resume_*`.
- Stop input has `background_tasks` (array of in-flight tasks; empty when nothing runs) and `session_crons`.
  SubagentStop carries `background_tasks` too, scoped to the parent session (we ignore it).
- StopFailure: field `error` (`rate_limit`, `overloaded`, ... `unknown`); `last_assistant_message` holds the API error
  text. We read none of it.
- SessionEnd: field `reason` (`clear`, `resume`, `logout`, `prompt_input_exit`, `other`); default hook budget 1.5 s
  (`CLAUDE_CODE_SESSIONEND_HOOKS_TIMEOUT_MS`). SessionStart: field `source` (`startup`, `resume`, `clear`, `compact`, `fork`).
- PermissionRequest input: `tool_name`, `tool_input`, `permission_suggestions`, `mcp_server`. PermissionDenied: `tool_name`,
  `tool_input`, `tool_use_id`, `reason`.
- Statusline JSON: `session_id`, `context_window.used_percentage` (may be null early), `cost.total_cost_usd` (list-price
  estimate; resets to 0 when `/clear` starts a new session), `rate_limits.five_hour.used_percentage` (0..100; only for
  Pro/Max and only after the first API response, each window may be absent). Updates are debounced by 300 ms; the
  line is hidden during permission prompts. Settings: `"statusLine": {"type":"command","command":...}` with optional
  `refreshInterval`.

Where the docs differ from, or add to, the spec
1. `SessionStart` has the source `compact` (after a compaction, mid-turn). The spec says every SessionStart gives `IDLE`;
   we keep the state for `source: compact`, otherwise a working session would show asleep.
2. The Notification type list in the spec is incomplete: `elicitation_complete`, `elicitation_response`, `auth_success` and
   `quota_auto_resume_*` exist; we ignore them (no state change). `agent_completed` and `agent_needs_input` are confirmed.
3. `/clear` resets `cost.total_cost_usd`; the daemon keeps the last cost of a session id only, so the cost bowl drops
   after a `/clear` (not a spec statement, a consequence).
4. The docs print `agent_id` as "the subagent or in-process teammate the hook fires in". We treat any event with an
   `agent_id` as a subagent event (spec 4.2). A `SubagentStart` of an `IDLE` or `DONE` session sets `WORKING`
   (the spec only says subagent events never leave a wait).
5. The docs did not show the full input of `Elicitation`, `ElicitationResult` and `PostCompact` in the page parts I read; we
   rely only on `hook_event_name` for them (and the matcher is the MCP server name for Elicitation, which we do not use).
6. Not verified, still open (spec section 13): that `PostToolUse` of an approved tool arrives right after the prompt is answered,
   whether `-p` and background sessions send `Notification` / run the statusline.

Choices where the spec is silent
- A `WORKING` or `FAILED` session with no event for 12 hours is removed like a waiting one (2 h only for `IDLE` / `DONE`).
- A `PermissionRequest` for `AskUserQuestion` gives `QUESTION`, not `PERMISSION`.
- The "minutes in this state" counters do not trigger a send; they ride on the 10 s heartbeat.
- `/clear`: `SessionEnd(reason=clear)` keeps the session (as `IDLE`) for 5 s; a `SessionStart(source=clear)` in the same
  `cwd` takes it over with the new `session_id`, slot and variant. Without a follower it is removed after 5 s.

## Privacy

`model.filter_hook` keeps only `session_id`, `agent_id`, `hook_event_name`, `tool_name`, `notification_type`, `cwd`, `reason`,
`source` and the length of `background_tasks`; the statusline filter keeps `session_id` and three numbers. The request
body is dropped right after the filter (before anything is stored or queued). The handler logs nothing. The tool name is only
mapped to a category; the `cwd` is used for the git lookup and the 4-character label and is never printed by `status`.
