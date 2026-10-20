"""Maps the app's icons to glyph names in each icon font and prints C codepoint tables.
Usage: python3 -I tools/icons/icon_names.py <icon-download-dir>"""
import re, sys, os

ICONS = [  # app icon id, Phosphor, Lucide, Material Icons Round
    ("BACK", "caret-left", "chevron-left", "arrow_back_ios_new"),
    ("CLOSE", "x", "x", "close"),
    ("PLUS", "plus", "plus", "add"),
    ("BULB", "lightbulb", "lightbulb", "lightbulb"),
    ("TEXT", "text-aa", "a-large-small", "text_fields"),
    ("KEYBOARD", "keyboard", "keyboard", "keyboard"),
    ("DOTS", "dots-three-vertical", "ellipsis-vertical", "more_vert"),
    ("FOLDER", "folder-simple", "folder", "folder"),
    ("DOC", "file-text", "file-text", "description"),
    ("LEFT", "arrow-left", "arrow-left", "arrow_back"),
    ("RIGHT", "arrow-right", "arrow-right", "arrow_forward"),
    ("UP", "arrow-up", "arrow-up", "arrow_upward"),
    ("DOWN", "arrow-down", "arrow-down", "arrow_downward"),
    ("SHIFT", "arrow-fat-up", "arrow-big-up", "arrow_circle_up"),
    ("SHIFT_LOCK", "arrow-fat-line-up", "arrow-big-up-dash", "keyboard_capslock"),
    ("BACKSPACE", "backspace", "delete", "backspace"),
    ("ENTER", "arrow-elbow-down-left", "corner-down-left", "keyboard_return"),
    ("HIDE", "arrow-line-down", "arrow-down-to-line", "keyboard_hide"),
    ("CHEVRON_DOWN", "caret-down", "chevron-down", "expand_more"),
    ("CHEVRON_RIGHT", "caret-right", "chevron-right", "chevron_right"),
    ("BOOK", "notebook", "notebook", "menu_book"),
]

def lucide_table(d):
    return css_map(os.path.join(d, "lucide/package/font/lucide.css"), r"icon-")

def css_map(path, prefix):
    text = open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r'\.' + prefix + r'([a-z0-9-]+):+before\s*\{\s*content:\s*"\\([0-9a-fA-F]+)"', text):
        out.setdefault(m.group(1), int(m.group(2), 16))
    return out

def main(d):
    ph = css_map(os.path.join(d, "phosphor/package/src/regular/style.css"), r"ph\.ph-")
    luc = css_map(os.path.join(d, "lucide/package/font/lucide.css"), r"icon-")
    mat = {}
    for line in open(os.path.join(d, "MaterialIconsRound-Regular.codepoints")):
        name, hexv = line.split()
        mat[name] = int(hexv, 16)
    missing = []
    for set_name, table, col in (("phosphor", ph, 1), ("lucide", luc, 2), ("material", mat, 3)):
        print("static const unsigned %s_cp[] = {" % set_name)
        for row in ICONS:
            cp = table.get(row[col])
            if cp is None:
                missing.append("%s:%s" % (set_name, row[col]))
                cp = 0
            print("    0x%04x, /* %s %s */" % (cp, row[0], row[col]))
        print("};")
    print("static const char *icon_ids[] = { %s };" % ", ".join('"%s"' % r[0] for r in ICONS))
    print("#define ICON_COUNT %d" % len(ICONS))
    if missing:
        sys.stderr.write("missing: " + ", ".join(missing) + "\n")

main(sys.argv[1])
