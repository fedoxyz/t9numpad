# t9numpad

**T9-style predictive text input daemon for USB and touchpad numpads on Linux.**

Type words on your numpad using T9 key sequences; t9numpad predicts the word and
emits it via a virtual keyboard (uinput). Works in terminals, X11 applications,
and browsers — no GUI framework required.

```
 [7=pqrs]  [8=tuv]   [9=wxyz]
 [4=ghi]   [5=jkl]   [6=mno]
 [1=abc]   [2=def]   [3=symbols]
 [0=SPC]   [-=DEL]   [/=multitap]  [*=NUM]  [ENTER=commit]
```

---

## Key Behaviour

### TEXT mode (default)

| Key          | Action                                                          |
|--------------|-----------------------------------------------------------------|
| `1`–`9`      | Append digit to T9 sequence; preview updates live               |
| `0`          | Commit current word + insert space                              |
| `ENTER`      | Commit current word + newline                                   |
| `KP.`        | Commit current word (no newline or space)                       |
| `-`          | Delete last digit from sequence; backspace if buffer is empty   |
| symbol key   | Cycle through `. , ! ? - '` on repeated press; commit with `0` or `ENTER` |
| `*`          | Switch to NUMBER mode                                           |
| `/`          | Toggle MULTI-TAP mode                                           |

### NUMBER mode

Every key immediately emits its digit. `-` is backspace. `*` returns to TEXT mode.

### MULTI-TAP mode (activated with `/`)

Each key cycles through its letters on repeated press — classic phone input.
Press a different key or wait for the timeout to lock in the current letter.
Commit the whole word with `ENTER` or `0`. Words typed this way are
**automatically added to the learned dictionary** so T9 predicts them next time.

Example — typing "hi" with `4=ghi`:
- `4` → preview: g
- `4` `4` → preview: h
- `4` `4` `4` → preview: i  ← wait or press a different key to lock "i"
- `ENTER` → emits "hi" (full word built so far)

---

## Key Map

Default layout — matches physical numpad rows bottom-to-top:

| Digit | Letters / function | Notes                          |
|-------|--------------------|--------------------------------|
| `0`   | space              | always immediate               |
| `1`   | abc                |                                |
| `2`   | def                |                                |
| `3`   | symbols            | `. , ! ? - '` cycling          |
| `4`   | ghi                |                                |
| `5`   | jkl                |                                |
| `6`   | mno                |                                |
| `7`   | pqrs               |                                |
| `8`   | tuv                |                                |
| `9`   | wxyz               |                                |

To match classic phone layout (abc on `1`, symbols elsewhere) set
`flip_layout = true` in the config — this swaps rows 1↔7, 2↔8, 3↔9.

---

## Features

- Pure C, no runtime dependencies beyond glibc and libX11
- Hash-based dictionary with frequency adaptation — learns new words via multi-tap
- Configurable key map, symbol key, and layout via TOML config
- Auto-detects numpad input device (USB or Asus touchpad numpad)
- Config auto-loaded from `/etc/t9numpad/t9numpad.toml` without flags
- `t9preview`: standalone X11 overlay showing live prediction (no terminal needed)
- Runs as a systemd service
- AUR package available

---

## Quick Start

### From the AUR

```bash
yay -S t9numpad
# or manually:
git clone https://aur.archlinux.org/t9numpad.git && cd t9numpad && makepkg -si
```

### From Source

```bash
git clone https://github.com/youruser/t9numpad.git
cd t9numpad
make
sudo make install
```

Both `t9numpad` and `t9preview` are built and installed automatically.

---

## Setup

### 1. Permissions

```bash
sudo modprobe uinput
echo 'uinput' | sudo tee /etc/modules-load.d/uinput.conf
sudo usermod -aG input "$USER"
sudo udevadm control --reload-rules && sudo udevadm trigger
# re-login after usermod
```

### 2. Find your device

```bash
sudo bash scripts/debug-input.sh
# or
ls -l /dev/input/by-id/ | grep -i num
# or omit 'device' from the config entirely to let t9numpad autodetect
```

### 3. Configure

```bash
sudo $EDITOR /etc/t9numpad/t9numpad.toml
```

Minimal working config:

```toml
device = "/dev/input/event23"   # omit to autodetect

[dict]
path    = "/usr/share/t9numpad/en.dict"
learned = "/var/lib/t9numpad/learned.dict"

[t9]
commit_timeout_ms = 1500
multitap          = false   # starting mode; toggle live with /
flip_layout       = false   # true = phone style (abc on 1,2,3)
symbol_key        = 3       # which digit cycles punctuation

[keymap]
0 = " "
1 = "abc"
2 = "def"
3 = ".,!?-'"
4 = "ghi"
5 = "jkl"
6 = "mno"
7 = "pqrs"
8 = "tuv"
9 = "wxyz"

[log]
level = 3   # 0=silent 1=error 2=warn 3=info 4=debug
```

t9numpad loads `/etc/t9numpad/t9numpad.toml` automatically on startup.
Pass `-c /path/to/other.toml` to override.

### 4. Enable the service

```bash
systemctl enable --now t9numpad.service
systemctl status t9numpad.service
```

---

## Live Preview (t9preview)

`t9preview` is a small X11 overlay window that shows the current T9 prediction.
It is installed alongside the daemon and requires no configuration.

```
┌──────────────────────────────────────┐
│ TEXT │ hello▌                        │
└──────────────────────────────────────┘
```

Badge colours: **red** = TEXT mode · **amber** = NUMBER mode · **green** = MULTI-TAP

### i3 setup

Add to `~/.config/i3/config`:

```
exec --no-startup-id t9numpad -c /etc/t9numpad/t9numpad.toml
exec --no-startup-id t9preview
```

By default `t9preview` appears in the bottom-right corner. Pass coordinates to
reposition it:

```
exec --no-startup-id t9preview 0 0        # top-left
exec --no-startup-id t9preview 960 0      # top-centre on a 1920 px screen
exec --no-startup-id t9preview -1 -1      # bottom-right (default)
```

The window is always-on-top, has no decorations, never steals focus, and does
not appear in the taskbar or pager. No `for_window` rule needed.

### Optional: sharper font

```bash
sudo pacman -S terminus-font
```

t9preview tries Terminus first and falls back to the built-in `fixed` font.

### i3blocks (alternative — text in your bar instead of overlay)

Add to `~/.config/i3blocks/config`:

```ini
[t9]
command=cat /tmp/t9numpad_preview 2>/dev/null || echo "[t9: off]"
interval=1
```

---

## Building the Dictionary

A starter list ships in `data/en.dict` (word + frequency, one per line).
To build a richer one:

```bash
# From any plain-text corpus:
python3 scripts/generate_dict.py --min-freq 5 corpus.txt > data/en.dict

# From /usr/share/dict/words (uniform frequency):
python3 scripts/generate_dict.py --wordlist /usr/share/dict/words > data/en.dict
```

### Corpus sources

- [Project Gutenberg](https://www.gutenberg.org/cache/epub/feeds/pg_catalog.txt) —
  plain-text books, public domain. Good for general English.
- [OpenSubtitles](https://opus.nlpl.eu/OpenSubtitles.php) — conversational language,
  better for everyday words than literary corpora.
- [Wikipedia dumps](https://dumps.wikimedia.org/enwiki/latest/enwiki-latest-pages-articles.xml.bz2) —
  broad vocabulary, needs stripping (`wikextractor` tool helps).
- `/usr/share/dict/words` — already on your system, no frequencies but works as
  a fallback: `python3 scripts/generate_dict.py --wordlist /usr/share/dict/words`

### Other languages

The engine is fully data-driven. To add a language:

1. Provide a word-frequency `.dict` file in that language
2. Set `[keymap]` entries to include the correct characters (e.g. `1 = "abcåä"`)
3. Point `dict.path` at the new file

No code changes needed.

---

## Learned Words

Words spelled via multi-tap are added to the in-memory dictionary with a small
frequency boost and written to `learned.dict` on clean shutdown. On next startup
t9numpad loads both dictionaries, so new words are predicted immediately.

To save learned words without waiting for shutdown:

```bash
systemctl stop t9numpad.service   # clean shutdown triggers save
```

---

## Project Layout

```
t9numpad/
├── src/          C sources (main, t9, uinput, config, log, t9preview)
├── include/      Public headers
├── data/         Word-frequency dictionaries
├── packaging/    PKGBUILD + t9numpad.install (AUR)
├── systemd/      Service unit
├── man/          Man page source
├── scripts/      Helper scripts (debug-input.sh, generate_dict.py)
└── tests/        Unit tests
```

---

## Contributing to the AUR

```bash
# 1. Bump pkgver in packaging/PKGBUILD
# 2. Create release tarball and update checksums:
make dist
sha256sum t9numpad-*.tar.gz
# 3. Prepare AUR directory and test:
bash scripts/prepare-aur.sh
makepkg --printsrcinfo > .SRCINFO
makepkg -si
# 4. Push
git push aur main
```

---

## License

GPL-2.0-or-later — see [COPYING](COPYING).
