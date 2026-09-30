import json, urllib.request, io, os
from PIL import Image
op = urllib.request.build_opener(); op.addheaders = [('User-Agent', 'Mozilla/5.0')]; urllib.request.install_opener(op)
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
N = 251      # sprites bundled on the watch
TOTAL = 493  # names known to the app (rest of the sprites come from the phone)
S = 56
names = [r['name'] for r in json.load(urllib.request.urlopen(f'https://pokeapi.co/api/v2/pokemon-species?limit={TOTAL}'))['results']]
disp = {'nidoran-f': 'Nidoran F', 'nidoran-m': 'Nidoran M', 'mr-mime': 'Mr. Mime', 'farfetchd': "Farfetch'd", 'ho-oh': 'Ho-Oh', 'mime-jr': 'Mime Jr.', 'porygon-z': 'Porygon-Z'}
os.makedirs(f'{D}/resources/sprites', exist_ok=True)
for i in range(1, N + 1):
    u = f'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-v/black-white/{i}.png'
    im = Image.open(io.BytesIO(urllib.request.urlopen(u).read())).convert('RGBA')
    im = im.crop(im.getbbox())
    k = min(S / im.width, S / im.height)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    c.paste(im, ((S - im.width) // 2, S - im.height), im)
    a = c.split()[3].point(lambda v: 255 if v > 127 else 0)
    q = c.convert('RGB').quantize(15, method=Image.MEDIANCUT, dither=Image.NONE)
    q = q.convert('RGBA'); q.putalpha(a)
    q.save(f'{D}/resources/sprites/p{i:03d}.png', optimize=True)
names_c = ',\n'.join('  "%s"' % disp.get(n, n.capitalize()) for n in names)
open(f'{D}/src/c/names.h', 'w').write(
    f'#define NUM_MON {TOTAL}\n#define NUM_LOCAL {N}\nstatic const char *const NAMES[NUM_MON] = {{\n{names_c}\n}};\n'
    '\nstatic const uint32_t SPRITES[NUM_LOCAL] = {\n'
    + ',\n'.join('  RESOURCE_ID_SPRITE_P%03d' % i for i in range(1, N + 1)) + '\n};\n')
pj = json.load(open(f'{D}/package.json'))
pj['pebble']['resources']['media'] = [{"type": "bitmap", "name": f"SPRITE_P{i:03d}", "file": f"sprites/p{i:03d}.png"} for i in range(1, N + 1)]
json.dump(pj, open(f'{D}/package.json', 'w'), indent=2)
