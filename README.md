# Terminal Air Hockey (C, multi-threaded)

A two-player, real-time air hockey / pong hybrid that runs directly in your
terminal. Both paddles move in two dimensions within their own half of the
table, a ball bounces between them with simple "spin" physics, and the game
is driven by three cooperating threads (input, game logic, rendering)
instead of one blocking loop.

```
 Alice      1 : 2       Bob

 +--------------------+
 |          :         |
 |          :         |
 |  #       :         |
 |  #       :      #  |
 |O #       :      #  |
 |          :      #  |
 |          :         |
 |          :         |
 |          :         |
 +--------------------+

 Left: W/A/S/D    Right: I/J/K/L    Quit: Q
```

---

## Features

- Two-player local multiplayer, no dependencies beyond a POSIX libc + pthreads
- Paddles move on **both axes** within their half of the table (air-hockey
  style, not fixed to a single column like classic Pong)
- Ball "spin": where the ball strikes a paddle changes its rebound angle
- First to `WIN_SCORE` (3) points wins; final score printed on exit
- Named players (prompted before the match starts)
- Clean `Ctrl+C` / `SIGTERM` handling — the terminal is always restored to
  its original mode on exit, even if the game is killed mid-match

## Controls

| Player       | Up | Down | Left | Right |
|--------------|----|------|------|-------|
| Left (Alice) | W  | S    | A    | D     |
| Right (Bob)  | I  | K    | J    | L     |

Press **Q** at any time to quit immediately.

## Build & Run

```bash
gcc -Wall -Wextra -O2 -pthread -o air_hockey air_hockey.c -lrt
./air_hockey
```

`-lrt` is only needed on older glibc versions; on modern Linux (glibc ≥ 2.17)
`clock_nanosleep` lives in libc itself and `-pthread` alone is sufficient.

Requires a real terminal (it uses `termios` and ANSI escape codes), so run it
in an actual terminal emulator, not through a plain pipe.

---

## Architecture

The game is split across three threads that share one `GameState` behind a
single mutex, plus a condition variable used to hand frames from the logic
thread to the render thread:

```
                 ┌───────────────┐
   stdin ──────▶ │ Input Thread  │──┐
                 └───────────────┘  │
                                    │ writes paddle
                                    ▼ positions (mutex)
                              ┌───────────┐        frame_ready + signal
                              │ GameState │◀────────────────┐
                              └───────────┘                 │
                                    ▲                       │
                                    │ reads/updates          │
                                    │ ball, score (mutex)    │
                 ┌───────────────┐  │                ┌───────────────┐
                 │ Logic Thread  │──┘                │ Render Thread │──▶ stdout
                 └───────────────┘                   └───────────────┘
```

- **Input thread** — waits on `select()` for a keypress, translates it into
  paddle movement, and updates `GameState` under the mutex.
- **Logic thread** — runs at a fixed tick rate, advances the ball, resolves
  wall/paddle collisions and scoring, then signals the render thread that a
  new frame is ready.
- **Render thread** — sleeps until signaled, copies the state, and draws one
  frame to the terminal.

This is the same overall shape as the original code, but the *scheduling
policy* of each thread changed — which is what actually affects smoothness.

---

## Performance & Smoothness Fixes

The original version compiled logically but had three classic terminal-game
performance problems. Here's what changed and why it matters:

### 1. The input thread was busy-spinning
The original `read()` loop on a non-blocking fd had no wait/timeout at all —
it would spin continuously checking for input, pegging one CPU core at
~100% even when the player wasn't touching the keyboard.

**Fix:** the input thread now blocks on `select()` with a short timeout
(50 ms). It only wakes up when a key is actually available (or to check
`shutdown_flag` periodically). Measured CPU usage while idle dropped to
effectively 0.

### 2. Naive sleep-based pacing drifts over time
A typical "fix" for game loops is `usleep(TICK_MS * 1000)` between updates.
The problem: that only accounts for the *sleep* — not the time spent doing
the update and render work in between. Over a long match this drifts, so
the ball gradually speeds up or the game stutters unpredictably depending
on system load.

**Fix:** the logic thread computes an absolute, ever-advancing deadline
(`next_tick`) using `CLOCK_MONOTONIC`, and sleeps to that exact deadline with
`clock_nanosleep(..., TIMER_ABSTIME, ...)`. Each tick lands on a precise,
non-drifting schedule regardless of how long the previous iteration took.

### 3. Full-screen clear + multiple writes per frame causes flicker
Calling `\033[2J` (clear screen) followed by many small `printf` calls each
frame is a common source of visible terminal flicker and unnecessary syscall
overhead — the screen goes blank, then repaints piece by piece.

**Fix:** the render thread builds the *entire* frame — header, borders,
board, footer — into one buffer, and repositions the cursor with `\033[H`
(cursor home) instead of clearing. A single `write()` syscall flushes the
whole frame at once, and `\033[K` clears any stray trailing characters per
line without blanking the whole screen. Net effect: no flicker, far fewer
syscalls.

### Bonus: lock discipline
The render thread holds the mutex only long enough to `memcpy` the (small)
`GameState` struct out; the relatively slow `write()` to the terminal happens
entirely *outside* the lock. This means a slow terminal (e.g. over SSH) can
never stall the physics/input threads — gameplay stays responsive even if
drawing lags behind.

---

## Known Simplifications / Ideas for Further Enhancement

- Ball movement is grid-stepped (one cell per tick) rather than
  sub-pixel/interpolated — true to the original design and keeps the physics
  simple and deterministic.
- No pause/serve delay after a point is scored; the ball resets and play
  continues immediately. Adding a brief "point scored" pause would be a
  natural next step.
- No color — kept to plain ASCII for maximum terminal compatibility. Adding
  ANSI color for the paddles/ball/score is a straightforward follow-up.
- Speed does not currently ramp up with rally length; could be added by
  shortening `TICK_MS` slightly after each paddle hit.

## Files

- `air_hockey.c` — complete, single-file source
- `README.md` — this file
