# PocketNotes

A distraction-free Markdown notebook for PocketBook e-readers. It is built for the
PocketBook Era Color and should work on other PocketBook devices with firmware 6.x.

Keep journals, work notes, recipes, research, or a novel in progress. Notes are plain Markdown
files in ordinary folders, so you can open them on any computer.

<p>
  <img src="docs/screenshots/note.png" width="260" alt="A formatted note">
  <img src="docs/screenshots/keyboard.png" width="260" alt="Typing with the on-screen keyboard">
  <img src="docs/screenshots/zen.png" width="260" alt="Zen mode">
</p>
<p>
  <img src="docs/screenshots/projects.png" width="260" alt="Projects">
  <img src="docs/screenshots/project.png" width="260" alt="A project's folders and notes">
  <img src="docs/screenshots/formatting.png" width="260" alt="Formatting menu">
</p>

## Features

- **Projects:** each project is a folder of notes and sub-folders. Start one empty, or from the
  *Book* template (chapters, scenes, ideas and notes).
- **Markdown on e-ink:**
  - headings, **bold**, *italic*, ~~strikethrough~~, ==highlight==, `inline code`;
  - code blocks, quotes (nested too), dividers;
  - bulleted, numbered and nested lists, plus task lists you tick with a tap;
  - links, images (shown as their caption), tables (columns aligned).

  Markup characters stay visible in light grey, so the cursor never jumps unexpectedly.
- **Formatting menu:** applies any of the above to the current line or word. Enter continues a
  list, and Enter on an empty item ends it.
- **Zen mode:** just your text, with no toolbars and no formatting. One tap in and one tap out.
- **Quick notes:** the 💡 button saves a thought straight into the project's `Ideas` folder.
- **On-screen keyboard** built for e-ink:
  - English, Ukrainian and symbol layouts;
  - capitals at the start of sentences;
  - cursor keys, and Backspace that repeats while held.

  You can switch to PocketBook's own keyboard in Settings.
- **Physical keyboards:** supported when the device firmware passes key presses to apps (see
  *Limitations*).
- **Safe by design:**
  - saves 4 seconds after you stop typing, and whenever you leave a note or the device sleeps;
  - every save writes a temporary file first, then replaces the note in one step;
  - the 20 most recent versions of each note are kept in `.history`;
  - deleted items go to `.trash`.
- **Picks up where you left off:** reopens the last note at the cursor.
- **E-ink friendly:** only changed lines are redrawn while you type, and a clean refresh follows
  when you pause.
- **Interface languages:** English (default) and Ukrainian.

## Installing

1. Build `PocketNotes.app` (see *Building*) or take it from a release.
2. Connect the PocketBook over USB and choose *PC link* on the device.
3. Copy `PocketNotes.app` into the `applications` folder on the device.
4. Eject the device, then open **Applications → PocketNotes**.

## How notes are stored

Everything lives in the `PocketNotes` folder at the root of the device storage:

```
PocketNotes/
  The Grey City/                a project is a folder
    01 Chapter 1/               folders can be nested
      01 Scene 1.md             notes are Markdown files
      02 Scene 2.md
    Ideas/                      quick notes (the 💡 button), one file each
    Notes/
    .history/                   earlier versions of each note
    .trash/                     deleted items (move them back to restore)
  Work notes/
    01 Meeting.md
```

- The number prefix ("01 ") sets the order. Moving items up or down renumbers them.
- Folders named `Ideas`/`Ідеї` and `Notes`/`Нотатки` sort to the bottom and keep plain names.
- Notes are UTF-8 Markdown, so they open in any Markdown app (Obsidian, iA Writer, VS Code…).

## Physical keyboard shortcuts

| Keys | Action |
|---|---|
| Ctrl+Space, Alt+Shift, Ctrl+Shift | Switch English / Ukrainian |
| Alt + `-` `,` `.` `/` `'` | — « » … ’ |
| Ctrl+S | Save now |
| Arrows, Home/End, PgUp/PgDn | Move the cursor |
| Esc | Back (or leave Zen mode) |

The PocketBook's page buttons scroll by a page; so does swiping up or down.

<img src="docs/screenshots/ukrainian-keyboard.png" width="260" alt="Ukrainian keyboard">

## Building

Requirements: macOS or Linux, a C compiler, and Docker for the PocketBook build.

```
make test      # unit tests
make sim       # a simulator that renders the app's screens to PNG files
./build/sim build/shots
make device    # cross-compiles build/device/PocketNotes.app
make install   # copies it to a PocketBook mounted at /Volumes/PB700K3
make icons     # regenerates src/ui/icons_data.* from the Lucide font
```

One-time setup of the PocketBook SDK (6.8, B300 platform) in a Docker volume:

```
docker build -t pocketnotes-build tools/docker
docker volume create pbsdk
curl -LO https://github.com/pocketbook/SDK_6.3.0/releases/download/6.8/SDK-B300-6.8.7z
docker run --rm -v pbsdk:/sdk -v "$PWD:/dl" pocketnotes-build sh -c 'cd /sdk && 7z x /dl/SDK-B300-6.8.7z'
```

`tools/keytest` builds a small diagnostic app that shows which key events reach apps on your device.

## Code layout

- `src/core/`: platform-independent logic, covered by `tests/`. Includes the text document,
  line wrapping, Markdown parsing, project folders, file saving, keyboard layouts and settings.
- `src/ui/`: screens on PocketBook's InkView toolkit: projects, project outline, editor,
  pop-ups, the on-screen keyboard, and the PocketBook keyboard bridge.
- `src/host/`: a stand-in for InkView on the desktop, used by the simulator.

## Limitations

- **Physical keyboards:** stock PocketBook firmware may not pass Bluetooth or USB keyboard input
  to third-party apps. PocketNotes listens for the firmware's extended key events, and reads the
  input devices directly when it is allowed to.
- **PocketBook keyboard:** this option uses the system keyboard window; typed text is inserted
  when you tap OK. Live typing through the system keyboard is experimental. Its events are
  logged to `pocketnotes-keyboard.log` on the device as sizes and positions only, never the text.
- **Not yet available:** syncing (copy files over USB for now), undo, and copy/paste.

## License

MIT; see [LICENSE](LICENSE). Icons are from [Lucide](https://lucide.dev) (ISC licence). The
simulator uses [stb](https://github.com/nothings/stb) (public domain).
