# Changelog

## 1.4.2

- Now maintained by POEFixer: new settings tab in the POEFixer style (cards, clear labels, a
  tooltip on every control, a live "on screen / highlighted" status).
- Tablets in your inventory are highlighted again (the backpack grid was skipped).
- No more stutter with large stashes: tablets are found and read in the background, and the
  highlights follow tab switches sooner.
- Labels stay inside their tablet's cell at one even size; uses left is a ×N corner badge, and
  matched bonuses show as names or a ✓N count when the cell is too small.
- Highlights draw behind the POEFixer window while it is open instead of over it.
- With "Read tablet modifiers" off, every enabled type is highlighted by type, as described
  (bonus and uses-left filters no longer hide everything).
- Settings save about a second after each change and can no longer be lost to a half-written
  file; a damaged file is kept as settings.json.corrupt. After an area change, tablets are
  re-read instead of reusing an old reading.
- The duplicate "Enable Tablet Helper" and "Show overlay" switches were removed: use the
  plugin's Enabled switch.

## 1.4.1 and earlier

Released by Omer Faruk ARPA at https://github.com/omrfarukarpa/poefixer-tablet-helper.
