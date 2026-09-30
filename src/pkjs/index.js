var SPRITES = require('./sprites');
var CHUNK = 900;
var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

function decode(s) {
  var out = [], buf = 0, bits = 0;
  for (var i = 0; i < s.length; i++) {
    var v = B64.indexOf(s.charAt(i));
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push((buf >> bits) & 0xFF);
    }
  }
  return out;
}

function sendChunks(id, bytes) {
  var total = Math.ceil(bytes.length / CHUNK);
  var seq = 0;
  function next() {
    if (seq >= total) return;
    var msg = {
      SprId: id, SprSeq: seq, SprTotal: total,
      SprData: bytes.slice(seq * CHUNK, (seq + 1) * CHUNK)
    };
    Pebble.sendAppMessage(msg, function () { seq++; next(); }, function () {
      console.log('chunk send failed, retrying ' + seq);
      setTimeout(next, 300);
    });
  }
  next();
}

Pebble.addEventListener('appmessage', function (e) {
  var id = e.payload.SprReq;
  if (id === undefined) return;
  var b64 = SPRITES[String(id)];
  if (!b64) { console.log('no sprite ' + id); return; }
  sendChunks(id, decode(b64));
});
