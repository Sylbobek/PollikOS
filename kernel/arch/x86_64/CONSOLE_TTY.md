# Interactive x86_64 console, TTY and shell

The x86_64 target has a framebuffer console, serial-backed interactive shell,
and native `.pol` desktop and Files applications. Typed commands run a userspace
shell, programs receive stdin from the console, and native TinyCC can be driven
by hand instead of only by the selfhost fixture.

## Architecture

```
host terminal emulator (Windows Terminal / QEMU serial window)
        |  bytes over COM1 (polled 16550)
kernel TTY queue (kernel/arch/x86_64/tty.c, 1 KiB ring)
        |  raw byte stream
stdin fd 0  --  read() blocks the process on the existing scheduler state
        |
/bin/pollish  (Ring 3 shell: prompt, line editor, history, tokenizer, builtins)
        |  spawnp / spawn + waitpid
children (e.g. /bin/tcc, ./hello) share fd 0/1/2 with the console
```

- `tty64_poll()` drains COM1 and polls PS/2 keyboard and mouse input on every PIT
  tick while the scheduler runs. Keyboard/UART bytes wake blocked readers from
  the same handler; a process with no input simply stays `PROCESS_BLOCKED` (the
  idle path uses `sti; hlt`), so there is no busy-spin.
- `read(0, buffer, n)` returns as soon as at least one byte is queued (raw
  semantics). The kernel performs no echo and no line editing; the shell owns
  the canonical editing experience. A future termios/canonical mode can layer
  on the same queue.
- The TTY is disabled until the normal console boot path enables it, so
  synchronous regression builds keep the historical `stdin -> ENOTSUP`
  contract and every C1-C7 fixture is unchanged.

## Console boot

The normal (non-selftest) kernel finishes its demos, prints its x86_64-ready
marker and enters `console64_run()`:

1. prints `[TTY] PollikOS console ready; starting /bin/pollish`;
2. enables the TTY and launches `/bin/pollish` with `PATH=/bin`;
3. runs the scheduler until the shell exits, then relaunches it (a login-like
   loop);
4. child processes are awaited by the shell; their exit status is the shell's
   `$?`.

## Shell (`/bin/pollish`)

Line editing: printable insert at the cursor, Backspace, Delete
(`ESC [ 3 ~`), Left/Right, Home/End (`ESC [ H` / `ESC [ F` and `1~`/`4~`/`7~`/
`8~` variants), Enter, Ctrl+C cancels the current input line, Ctrl+D exits on
an empty line.

History: bounded ring of 128 entries, Up/Down navigation, no empty or
consecutive duplicate entries; the session is appended to
`/home/.pollik_history` and restored on the next shell start.

Tokenizing: spaces, single and double quotes, backslash escapes, and `$?` /
`$NAME` expansion.

Builtins: `cd`, `pwd`, `echo`, `ls`/`dir`, `cat`/`type`, `mkdir`, `rm`/`del`,
`rmdir`, `mv`/`rename`, `touch`, `clear`/`cls`, `history [-c]`, `help`,
`env`, `set NAME=VALUE`, `status`, `exit [code]`.

External commands: anything else is executed with `spawnp` (PATH lookup,
default `/bin`) or `spawn` when the name contains `/`, then `waitpid`; child
stdout/stderr is the console. Unknown programs print
`'name' is not recognized as a PollikOS command or executable.`

## Native TinyCC without the selfhost fixture

`/bin/tcc` links programs against `/usr/lib/libc.a`, `/usr/lib/crt0.o` and
`/usr/lib/tcc/libtcc1.a`. On the first shell start in a fresh image the shell
rebuilds `libc.a` from `/usr/src/libc/*.c` with native `tcc -c` plus
`tcc -ar rcs`, then writes the `/usr/lib/.native-libc` marker; subsequent boots
skip the preparation. This is the same compatibility rule the automated
selfhost driver uses: TinyCC links objects produced by TinyCC.

Manual workflow (verified by `tests/x86_64_console.py`):

```
PollikOS:/home> cat hello.c
PollikOS:/home> tcc hello.c -o hello.pol
PollikOS:/home> ./hello.pol
Hello from self-hosted PollikOS C!
PollikOS:/home> echo $?
42
```

The `.pol` suffix identifies a PollikOS executable; its contents remain a
statically linked ELF64, which the loader validates independently of the name.

## Native `.pol` windows

`<pollikos/window.h>` exposes the x86_64 window API. `pollikos_window_create`
returns a process-owned XRGB8888 pixel buffer; the kernel validates its mapping
and composites it inside a titled, bordered window when the app calls
`pollikos_window_present`. `pollikos_input_read` is nonblocking and returns
keyboard key-down/up events, modifier bits, absolute mouse coordinates, button
state/change bits, wheel delta and a close event. Printable keys use ASCII;
arrows, Home, End, Delete and modifier keys use `POLLIKOS_KEY_*` constants. The
top window receives input; dragging its titlebar moves it and clicking the red
control sends `POLLIKOS_INPUT_WINDOW_CLOSE`. Process exit reclaims both surface
mappings and removes the window.

After successful login, `/bin/desktop.pol` starts automatically and opens the
native `.pol` Terminal. The Dock keeps Files at the left of a thin separator;
the other Dock items launch Terminal and Window Demo. Files lists `.pol` apps in
`/bin`; double-click or select one and press Enter to launch it in a separate
window. `F`, `T` and `D` launch Files, Terminal and Window Demo from the desktop.
The COM1 shell remains available for diagnostics and recovery. If the Desktop
cannot start, login falls back to that text shell.

Each process can own one window. There is no separate fixed window-count cap:
the manager has a slot per process-table entry, and the actual count is bounded
by the active process/RAM capacity and by framebuffer and mapping memory.
Window dimensions are bounded by the framebuffer and current ABI caps.
`/bin/windowdemo.pol` is a real ELF64 application that exercises drawing,
keyboard, wheel/button input, dragging, close and framebuffer restoration.

## Scope and limitations

- The COM1 console is one shell session at a time and relaunches the shell after
  exit. The graphical Terminal runs its own shell process. x86_64 routes
  keyboard input to the top window. The full i386 desktop is not migrated, but
  x86_64 provides native `.pol` Desktop, Files and Terminal applications.
- The framebuffer console draws a guest-side PS/2 mouse pointer and exposes
  mouse buttons and wheel events to the top window. Text caret, scrollback, font
  selection and resize remain outside this fixed-size console.
- `clear`/`cls` emits `ESC [ 2J ESC [ 3J ESC [ H` to clear the framebuffer and,
  where supported, request scrollback clearing from the COM1 host terminal.
- Ctrl+C cancels shell input or sends `SIGINT` to the foreground pipeline's
  process group.
- Pipes and redirection work in the shell; full job control, `termios` and tab
  completion are not implemented.
- The shell and utilities are SDK-built C (`sdk/apps/pollish.c`); only the TTY
  queue and stdin plumbing live in the kernel.

## Verification

`python tests/x86_64_console.py` boots the normal kernel with a TCP serial
link, types the whole session and checks: prompt/cwd, `cd`, `ls`, `cat`,
quoting and `$?`/`$PATH` expansion, file commands, native `tcc` compile/link/run
with exit status 42, mid-line editing (Left/Delete/Home/End), history recall,
unknown-command reporting, output retention, `clear`, and exit/relaunch with
persistent history. `python tests/x86_64_graphical_console.py` verifies automatic
Desktop startup after login, the real shell in the graphical Terminal, Files
launching a `.pol` app, multiple windows, framebuffer composition, PS/2
keyboard/modifier routing, pointer, mouse button/wheel delivery, titlebar drag,
close and backing-screen restoration.
`tests/x86_64_boot.py` runs the serial console test as the final x86_64 step.
