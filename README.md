# Omacalc

A dead-simple calculator built with Qt Quick and C++ that automatically follows the Omarchy theme and system dark/light mode.

<img width="800" alt="Omacalc with the Tokyo Night theme" src="screenshots/omacalc.png" />


## Install

Install via the Omarchy Package Repository via the `omacalc` package.

## Usage

The keypad covers the basics: the four arithmetic operators with the usual
precedence (`×` and `÷` before `+` and `−`), decimals, percent, sign toggle,
and backspace. The expression builds up above the display and stays visible
with the result after `=`, so `42 × 3 + 7` reads back exactly as it was entered.
From the keyboard, `^` raises to a power ahead of everything else, so
`2 × 3 ^ 2` gives `18`.

As you type, the answer so far appears in the accent color above the display,
before you press `=`. Long numbers are grouped with thousands separators on
screen, while copy and paste keep plain digits.

`FX` above the display opens three rows of scientific keys: parentheses,
powers (`xʸ`), square root, `sin`, `cos`, `tan`, factorial (`x!`), `ln`,
`log`, `π`, and `e`. Trig works in degrees or radians; click `DEG`/`RAD` to
switch. Open parentheses close themselves on `=`, so `2 × (3 + 4 =` gives
`14`, and an operand next to a group or constant multiplies it: `2(3)` is `6`
and `2π` is twice pi. Inside a group, `−` negates: `(−2 ^ 2)` is `-4`.

`HISTORY` lists past calculations, newest first, and keeps them between
launches. Click one to drop its exact result into the current expression.

Percent follows the familiar calculator convention: with a pending `+` or `−`
it takes that percentage of the running total, or of the enclosing group's
total inside parentheses (`200 + 10 % =` gives `220`),
while with `×`, `÷`, or `^` — or on its own — it simply divides by 100.

Everything works from the keyboard too:

- `0-9`, `.`, `+`, `-`, `*` or `x`, `/`, `^`, and `%` enter digits and operators.
  The numpad enters digits whether or not NumLock is on.
- `Enter` or `=` calculates.
- `(`, `)`, and `!` enter parentheses and factorial; `p` enters π, `e` enters
  Euler's number, and `r` opens a square root.
- `Backspace` deletes the last digit, then steps back through the expression.
- `C` or `Escape` clears.
- `S` toggles the sign.
- `Ctrl+C` or `Super+C` copies the result.
- `Ctrl+V` or `Super+V` pastes a number.
- `Ctrl+T` shows or hides the scientific keys, `Ctrl+D` switches between
  degrees and radians, and `Ctrl+H` opens history (`Escape` closes it).

Colors follow the current Omarchy theme (`~/.local/state/omarchy/current/theme/colors.toml`)
and re-tint live when the theme changes. Text follows the desktop text size —
`omarchy display text size`, or GNOME's `text-scaling-factor`.

## Requirements

- Qt 6: `qt6-base`, `qt6-declarative`
- `xdg-desktop-portal` and a portal backend

The iA Writer Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on
IBM Plex, copyright IBM Corp.
