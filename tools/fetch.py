"""Builds src/c/names.h (names + types) and src/pkjs/sprites.js (all sprites, sent to the watch on demand)."""
import base64, csv, io, json, os, subprocess, tempfile, unicodedata, urllib.request
from concurrent.futures import ThreadPoolExecutor
from PIL import Image
op = urllib.request.build_opener(); op.addheaders = [('User-Agent', 'Mozilla/5.0')]; urllib.request.install_opener(op)
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
TOTAL = 1025
S = 56
TOOLS = os.path.expanduser('~/.local/share/pebble-sdk/SDKs/4.33.1/sdk-core/pebble/common/tools')
PY = os.path.expanduser('~/.local/share/pipx/venvs/pebble-tool/bin/python')
CSV = 'https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/'

def rows(name):
    return list(csv.DictReader(io.StringIO(urllib.request.urlopen(CSV + name).read().decode())))

def ascii_name(n):
    n = n.replace('’', "'").replace('♀', ' F').replace('♂', ' M')
    return unicodedata.normalize('NFKD', n).encode('ascii', 'ignore').decode()

names = {int(r['pokemon_species_id']): ascii_name(r['name']) for r in rows('pokemon_species_names.csv')
         if r['local_language_id'] == '9' and int(r['pokemon_species_id']) <= TOTAL}
types = {i: [0, 0] for i in range(1, TOTAL + 1)}
for r in rows('pokemon_types.csv'):
    i = int(r['pokemon_id'])
    if i <= TOTAL: types[i][int(r['slot']) - 1] = int(r['type_id'])

tmp = tempfile.mkdtemp()

def sprite(i):
    p = f'versions/generation-v/black-white/{i}.png' if i <= 649 else f'{i}.png'
    im = Image.open(io.BytesIO(urllib.request.urlopen(
        f'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/{p}').read())).convert('RGBA')
    im = im.crop(im.getbbox())
    k = min(S / im.width, S / im.height)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    c.paste(im, ((S - im.width) // 2, S - im.height), im)
    a = c.split()[3].point(lambda v: 255 if v > 127 else 0)
    q = c.convert('RGB').quantize(15, method=Image.MEDIANCUT, dither=Image.NONE).convert('RGBA'); q.putalpha(a)
    q.save(f'{tmp}/{i}.png')

with ThreadPoolExecutor(16) as ex: list(ex.map(sprite, range(1, TOTAL + 1)))
subprocess.check_call([PY, '-c', f"""import sys; sys.path.insert(0, {TOOLS!r}); import png2pblpng as p
for i in range(1, {TOTAL + 1}): p.convert_png_to_pebble_png('{tmp}/%d.png' % i, '{tmp}/%d.pbl' % i, 'pebble64')"""])
out = {str(i): base64.b64encode(open(f'{tmp}/{i}.pbl', 'rb').read()).decode() for i in range(1, TOTAL + 1)}
os.makedirs(f'{D}/src/pkjs', exist_ok=True)
open(f'{D}/src/pkjs/sprites.js', 'w').write('module.exports = ' + json.dumps(out) + ';\n')

open(f'{D}/src/c/names.h', 'w').write(
    f'#define NUM_MON {TOTAL}\nstatic const char *const NAMES[NUM_MON] = {{\n'
    + ',\n'.join('  "%s"' % names[i].replace('"', '\\"') for i in range(1, TOTAL + 1)) + '\n};\n'
    '\n/* type ids: 1 normal 2 fighting 3 flying 4 poison 5 ground 6 rock 7 bug 8 ghost 9 steel\n'
    '   10 fire 11 water 12 grass 13 electric 14 psychic 15 ice 16 dragon 17 dark 18 fairy */\n'
    'static const uint8_t TYPES[NUM_MON][2] = {\n'
    + ',\n'.join('  {%d,%d}' % tuple(types[i]) for i in range(1, TOTAL + 1)) + '\n};\n')
print(TOTAL, 'names,', os.path.getsize(f'{D}/src/pkjs/sprites.js') // 1024, 'KB sprites.js')
