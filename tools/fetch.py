import json, urllib.request, io, os
op = urllib.request.build_opener(); op.addheaders=[('User-Agent','Mozilla/5.0')]; urllib.request.install_opener(op)
from PIL import Image
D = os.path.join(os.path.dirname(__file__), '..')
names = [r['name'] for r in json.load(urllib.request.urlopen('https://pokeapi.co/api/v2/pokemon-species?limit=151'))['results']]
disp = {'nidoran-f': 'Nidoran F', 'nidoran-m': 'Nidoran M', 'mr-mime': 'Mr. Mime', 'farfetchd': "Farfetch'd"}
S = 104
for i, n in enumerate(names, 1):
    u = f'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-v/black-white/{i}.png'
    im = Image.open(io.BytesIO(urllib.request.urlopen(u).read())).convert('RGBA')
    im = im.crop(im.getbbox())
    k = min(S / im.width, S / im.height); im = im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    c.paste(im, ((S - im.width) // 2, S - im.height), im)
    a = c.split()[3].point(lambda v: 255 if v > 127 else 0)
    q = c.convert('RGB').quantize(15, method=Image.MEDIANCUT, dither=Image.NONE)
    q = q.convert('RGBA'); q.putalpha(a)
    q.save(f'{D}/resources/sprites/p{i:03d}.png', optimize=True)

names_c = ',\n'.join('  "%s"' % disp.get(n, n.capitalize()) for n in names)
open(f'{D}/src/c/names.h', 'w').write('static const char *const NAMES[151] = {\n%s\n};\n' % names_c
  + '\nstatic const uint32_t SPRITES[151] = {\n' + ',\n'.join('  RESOURCE_ID_SPRITE_P%03d' % i for i in range(1, 152)) + '\n};\n'
)
media = [{"type": "bitmap", "name": f"P{i:03d}", "file": f"sprites/p{i:03d}.png"} for i in range(1, 152)]
json.dump(media, open(f'{D}/tools/media.json', 'w'))
