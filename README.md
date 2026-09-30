# Who's That PebbleMon

A "who's that?" guessing game for Pebble Time 2 (Emery). A new mystery creature
each day (first 151) appears as a black silhouette. Press SELECT, pick the right
name from 3 choices, and the creature is revealed in colour. Press SELECT again
to keep playing with random ones. Fully offline, no phone connection needed.

## Build

Sprites are not included in this repo. Fetch and prepare them, then build:

```bash
pip install pillow
python3 tools/fetch.py     # downloads sprites (PokeAPI/sprites), writes resources/ + src/c/names.h
pebble build
pebble install --phone <ip>
```

## Disclaimer

Unofficial fan project. Not affiliated with or endorsed by Nintendo, Game Freak,
Creatures Inc. or The Pokémon Company. Pokémon and all related names and artwork
are trademarks/copyright of their respective owners. Code is MIT licensed; that
license does not cover the artwork.

## Gen 3 & 4 (sent from the phone)

Hoenn and Sinnoh sprites don't fit on the watch, so they live in the phone-side JS and are sent over AppMessage on demand. Run `python3 tools/fetch_phone.py` before `pebble build` to generate `src/pkjs/sprites.js`.
