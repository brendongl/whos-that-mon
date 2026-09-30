"""Builds src/pkjs/sprites.js: Gen 3+4 sprites (ids 252-493) as Pebble PNGs, sent to the watch on demand."""
import base64, io, json, os, subprocess, sys, tempfile, urllib.request
from PIL import Image
op = urllib.request.build_opener(); op.addheaders = [('User-Agent', 'Mozilla/5.0')]; urllib.request.install_opener(op)
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
S = 56
TOOLS = os.path.expanduser('~/.local/share/pebble-sdk/SDKs/4.33.1/sdk-core/pebble/common/tools')
PY = os.path.expanduser('~/.local/share/pipx/venvs/pebble-tool/bin/python')
out = {}
tmp = tempfile.mkdtemp()
for i in range(252, 494):
    u = f'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-v/black-white/{i}.png'
    im = Image.open(io.BytesIO(urllib.request.urlopen(u).read())).convert('RGBA')
    im = im.crop(im.getbbox())
    k = min(S / im.width, S / im.height)
    im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    c = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    c.paste(im, ((S - im.width) // 2, S - im.height), im)
    a = c.split()[3].point(lambda v: 255 if v > 127 else 0)
    q = c.convert('RGB').quantize(15, method=Image.MEDIANCUT, dither=Image.NONE).convert('RGBA'); q.putalpha(a)
    src, dst = f'{tmp}/a.png', f'{tmp}/b.png'
    q.save(src)
    subprocess.check_call([PY, '-c', f"import sys; sys.path.insert(0,'{TOOLS}'); import png2pblpng as p; p.convert_png_to_pebble_png('{src}','{dst}','pebble64')"])
    out[str(i)] = base64.b64encode(open(dst, 'rb').read()).decode()
os.makedirs(f'{D}/src/pkjs', exist_ok=True)
open(f'{D}/src/pkjs/sprites.js', 'w').write('module.exports = ' + json.dumps(out) + ';\n')
print(len(out), 'sprites,', os.path.getsize(f'{D}/src/pkjs/sprites.js') // 1024, 'KB')
