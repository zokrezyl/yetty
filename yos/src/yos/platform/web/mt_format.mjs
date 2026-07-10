// printf formatter with correct 4-vs-8-byte integer widths (%lld).
const dec = new TextDecoder();

export function format(memory, fmtPtr, vaPtr) {
  const view = new DataView(memory.buffer), u8 = new Uint8Array(memory.buffer);
  const cstr = (p) => { let e = p; while (u8[e]) e++; return dec.decode(u8.slice(p, e)); };
  const fmt = cstr(fmtPtr); let va = vaPtr; let out = "";
  const n4 = () => { va = (va + 3) & ~3; const x = view.getInt32(va, true); va += 4; return x; };
  const u4 = () => { va = (va + 3) & ~3; const x = view.getUint32(va, true); va += 4; return x; };
  const n8 = () => { va = (va + 7) & ~7; const lo = view.getUint32(va, true), hi = view.getInt32(va + 4, true); va += 8; return hi * 4294967296 + lo; };
  for (let i = 0; i < fmt.length; i++) {
    if (fmt[i] !== "%") { out += fmt[i]; continue; }
    let spec = "%"; i++;
    while ("-+ #0".includes(fmt[i])) spec += fmt[i++];
    let w = ""; while (/[0-9]/.test(fmt[i])) { w += fmt[i]; spec += fmt[i++]; }
    if (fmt[i] === ".") { spec += fmt[i++]; while (/[0-9]/.test(fmt[i])) spec += fmt[i++]; }
    let lm = ""; while ("hljztLq".includes(fmt[i])) { lm += fmt[i]; spec += fmt[i++]; }
    const wide = /ll|j|q/.test(lm), conv = fmt[i], width = w ? parseInt(w, 10) : 0;
    const pad = (s) => (width && s.length < width ? (spec.includes("-") ? s.padEnd(width) : s.padStart(width)) : s);
    switch (conv) {
      case "d": case "i": out += pad(String(wide ? n8() : n4())); break;
      case "u": out += pad(String(wide ? n8() >>> 0 : u4())); break;
      case "x": out += pad((wide ? n8() : u4()).toString(16)); break;
      case "c": out += String.fromCharCode(n4() & 0xff); break;
      case "s": out += pad(cstr(u4())); break;
      case "f": case "g": case "e": { va = (va + 7) & ~7; out += pad(String(view.getFloat64(va, true))); va += 8; break; }
      case "%": out += "%"; break;
      default: out += spec + (conv || "");
    }
  }
  return out;
}
