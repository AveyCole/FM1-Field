// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// The FM-1 USB-MIDI update protocol (M-UPGRADE compatible) over WebMIDI.
// Step 1: the running firmware reads parts of the package and stages the update
// loader. Step 2: the loader reads the whole image and writes it. In both steps
// the device sends cmd 0x30 read requests on the logical image and we answer.

const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const MAXDATA = 512;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const IS_OTA = (id) => /^ota-/i.test(id.model);

// M-VAVE's FM1 Open firmware (V10, 2026-10): its updater takes only a package it has been told about
// first (cmd 49 reads its state, cmd 50 arms it with the image length and the package's SHA-256); then
// the update runs as usual. Only M-VAVE's own FM-1 firmware is installed over it: its loader rewrites the
// whole flash, boot head included, which returns the FM-1 to stock.
const OPEN_RE = /^FM1_(OFFICIAL_OPEN|COMMUNITY_\d+)$/;
// M-VAVE's FM-1 firmware, any version: "FM-1_0" and two digits ("FM-1_016"; fm1-base's dev builds are
// "FM-1_x00", its releases longer; m-bootloader's installer is FM-1_099). Whether a package is safe to write is the device's to check
// (felucca-bsp chip/ota.c: a well-made writer, a bootable boot area), not a list of known files
const IS_STOCK = (product) => /^FM-1_0\d\d$/.test(product) && product !== "FM-1_099";

// errors carry a code the pages translate: notfound, model, stopped, noloader, lost, noreturn,
// mismatch (detail: the identity the device reports), badreq
function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

function parseIdentity(pkt) {
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const txt = String.fromCharCode(...d.slice(6, 33)).replace(/\0+$/, "");
  if (OPEN_RE.test(txt)) return { text: txt, model: "FM-1", version: 0, open: true };
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  return m ? { text: txt, model: m[1], version: parseInt(m[2], 10) } : null;
}

// one MIDI in/out pair with a SysEx queue
class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  // false when the port is gone (unplugged): WebMIDI throws on a disconnected output
  send(bytes) {
    try { this.output.send(bytes); return true; } catch (_) { return false; }
  }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  async close() {
    this.input.onmidimessage = null;
    if (this.waiter) { const wake = this.waiter; this.waiter = null; wake(); }
    // Release the native MIDI handles as well as the JS callback. Windows can
    // retain the old handles while the FM-1 re-enumerates as its update loader.
    await Promise.allSettled([this.input, this.output].map(async (port) => {
      if (port.close) await port.close();
    }));
  }
}

async function handshake(link, tries = 3) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = Date.now() + 1000;
    while (Date.now() < end) {
      const p = await link.read(end - Date.now());
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

function frame(cmd, payload = []) {
  let s = 0;
  for (const b of payload) s += b;
  return [0xF0, ...pack7([0x00, 0x59, cmd, payload.length & 0xFF, payload.length >> 8, 0, ...payload, ~s & 0xFF]), 0xF7];
}

// send a command -> the payload of its reply, or null
async function query(link, cmd, payload = []) {
  link.drain();
  if (!link.send(frame(cmd, payload))) return null;
  const end = Date.now() + 1500;
  while (Date.now() < end) {
    const p = await link.read(end - Date.now());
    if (!p) break;
    if (p[0] !== 0xF0 || p[p.length - 1] !== 0xF7) continue;
    const u = unpack7(p.slice(1, -1));
    if (u.length < 7 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== cmd) continue;
    let s = 0;
    for (let i = 6; i < u.length; i++) s += u[i];
    if ((s & 0xFF) === 0xFF) return u.slice(6, 6 + (u[3] | (u[4] << 8)));
  }
  return null;
}

export class Updater {
  constructor(access, log, refreshAccess) {
    this.access = access;
    this.refreshAccess = refreshAccess;
    this.log = log || (() => {});
    this.links = new Set();
  }

  // Only probe known FM-1 product/loader names, then verify its identity.
  async find(filter) {
    const known = (port) => /fm-?1|felucca|ota|composite|sinco|usb[- _]?midi|\bfield\b|\baporee\b/i.test((port.name || "") + " " + (port.manufacturer || ""));
    const outs = [...this.access.outputs.values()].filter((p) => p.state !== "disconnected" && known(p));
    for (const input of [...this.access.inputs.values()]) {
      if (input.state === "disconnected" || !known(input)) continue;
      const same = outs.filter((p) => p.name === input.name);
      const candidates = [...same, ...outs.filter((p) => !same.includes(p))];
      for (const output of candidates) {
        // Each failed probe must release both ports, including partial opens.
        const link = new Link(input, output);
        let keep = false;
        try {
          this.log(`MIDI probe: ${input.name} → ${output.name}`);
          await input.open(); await output.open();
          if (link.lost) continue;
          const id = await handshake(link, 2);
          if (id && (!filter || filter(id))) {
            keep = true;
            this.links.add(link);
            return { link, id, name: input.name };
          }
        } catch (e) { this.log(`MIDI open/probe failed: ${e.name || "Error"}: ${e.message}`); }
        finally { if (!keep) await link.close(); }
      }
    }
    return null;
  }

  logPorts() {
    const describe = (ports) => [...ports.values()].map((p) =>
      `${p.name || "(unnamed)"} [${p.state}/${p.connection}]`).join("; ") || "none";
    this.log("MIDI inputs: " + describe(this.access.inputs));
    this.log("MIDI outputs: " + describe(this.access.outputs));
  }

  async release() {
    await Promise.allSettled([...this.links].map((link) => link.close()));
    this.links.clear();
  }

  async portsChanged(ms) {
    // Listen without replacing another consumer's statechange handler. Polling
    // remains a fallback for browsers that report the re-enumeration late.
    const access = this.access;
    await new Promise((resolve) => {
      const done = () => {
        clearTimeout(timer);
        access.removeEventListener?.("statechange", done);
        resolve();
      };
      const timer = setTimeout(done, ms);
      access.addEventListener?.("statechange", done);
    });
  }

  async waitFor(filter, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const dev = await this.find(filter);
      if (dev) return dev;
      await this.portsChanged(Math.max(0, Math.min(500, end - Date.now())));
      if (this.refreshAccess) {
        try { this.access = await this.refreshAccess(); }
        catch (e) { this.log(`MIDI access refresh: ${e.message}`); }
      }
    }
    return null;
  }

  // serve read requests until the device asks for one of the finish addresses; stops early
  // when the port disappears (lost: true) instead of waiting out idleMs
  async serve(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, bytes = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === 0xE0000000 || r.addr === 0xF0000000) {
        link.send(response(r.addr, [...new TextEncoder().encode("success"), 0]));
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      await sleep(10);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl))) {
        return { served, finished: false, lost: true };
      }
      served++;
      bytes += r.len;
      if (onProgress) onProgress(served, bytes);
    }
  }

  // full install: running firmware -> loader -> new firmware
  async install(image, product, onStep, sha256) {
    const step = onStep || (() => {});
    // One discovery path accepts either application or loader, including a
    // loader that appears while the browser is still refreshing its port map.
    const dev = await this.waitFor(() => true, 15000);
    if (!dev) {
      this.logPorts();
      throw fail("notfound", "Neither running firmware nor update loader answered. See the MIDI port list below.");
    }
    const [model] = product.split("_");
    const isLoader = IS_OTA(dev.id);
    if (dev.id.model !== (isLoader ? "ota-" + model : model)) {
      await dev.link.close(); throw fail("model", `the device is ${dev.id.text}, the package is for ${product}`);
    }
    this.log(`Connected: ${dev.name} — ${dev.id.text}`);
    step("start", dev.id.text);
    let ota = dev;
    if (!isLoader) {
      if (dev.id.open) await this.arm(dev, image, product, sha256);
      dev.link.send(UPGRADE);
      await sleep(2000);
      const s1 = await this.serve(dev.link, image, 0xE0000000, 8000, (n, b) => step("verify", n, b));
      await dev.link.close();
      if (!s1.finished) throw fail(s1.lost ? "lost" : "stopped", `the device ${s1.lost ? "was disconnected" : "stopped"} after ${s1.served} requests: nothing was written`);
      step("loader");
      await sleep(3000);
      ota = await this.waitFor(IS_OTA, 30000);
      if (!ota) {
        this.log("No update loader answered after switching modes.");
        this.logPorts();
        throw fail("noloader", "The update loader did not answer. See the MIDI connection details below.");
      }
      this.log(`Loader: ${ota.name} — ${ota.id.text}`);
    }
    const s2 = await this.write(ota, image, step);
    if (!s2.finished) throw fail(s2.lost ? "lost" : "stopped", `the loader ${s2.lost ? "was disconnected" : "stopped"} after ${s2.served} requests. Replug and press Install again to resume.`);
    step("reboot");
    await sleep(3000);
    const back = await this.waitFor((id) => !IS_OTA(id), 40000);
    if (!back) throw fail("noreturn", "the device did not come back: power-cycle it");
    await back.link.close();
    if (back.id.text !== product) throw fail("mismatch", `installed, but the device reports ${back.id.text}`, back.id.text);
    step("done", back.id.text);
    return back.id.text;
  }

  // m-bootloader (github.com/jvitkauskas/m-bootloader, MIT) after an install: the running firmware (product)
  // stages the package's installer (no flash.bin: any well-made writer, felucca-bsp chip/ota.c) and resets; the bootloader
  // runs it, it rewrites the boot area [0, 0x4000) only and restarts into the firmware. It never talks to us:
  // we wait for the firmware to come back. A firmware that does not know it refuses it, and nothing is written.
  async installBoot(image, product, onStep) {
    const step = onStep || (() => {});
    const dev = await this.waitFor((id) => !IS_OTA(id) && id.text === product, 40000);
    if (!dev) throw fail("noreturn", "the device did not come back: power-cycle it");
    step("bootstart");
    dev.link.send(UPGRADE);
    await sleep(2000);
    const s = await this.serve(dev.link, image, 0xE0000000, 8000);
    await dev.link.close();
    if (!s.finished) throw fail(s.lost ? "lost" : "bootrefused", `m-bootloader: the device stopped after ${s.served} requests: nothing was written`);
    step("bootwrite");
    await sleep(3000);
    const back = await this.waitFor((id) => !IS_OTA(id), 40000);
    if (!back) throw fail("noreturn", "the device did not come back: power-cycle it");
    await back.link.close();
    step("bootdone", back.id.text);
    return back.id.text;
  }

  // M-VAVE's Open firmware: allow the update of M-VAVE's own firmware (sha256: the package file's, hex)
  async arm(dev, image, product, sha256) {
    if (!IS_STOCK(product) || !/^[0-9a-f]{64}$/.test(sha256 || "")) {
      await dev.link.close();
      throw fail("open", `This FM-1 runs M-VAVE's ${dev.id.text} firmware. Put the stock firmware back first ("Back to the stock firmware"), then install this.`, dev.id.text);
    }
    const cap = await query(dev.link, 49);
    if (!cap || cap.length !== 47 || String.fromCharCode(...cap.slice(0, 4)) !== "FMC1" || cap[6] !== 0) {
      await dev.link.close();
      throw fail("stopped", `${dev.id.text} did not accept the update: nothing was written`);
    }
    const n = image.length;
    const tag = sha256.match(/../g).map((h) => parseInt(h, 16));
    const ack = await query(dev.link, 50, [1, cap[5], n & 0xFF, (n >>> 8) & 0xFF, (n >>> 16) & 0xFF, (n >>> 24) & 0xFF, ...tag]);
    if (!ack || ack.length !== 1 || ack[0] !== 0) {
      await dev.link.close();
      throw fail("stopped", `${dev.id.text} refused the package: nothing was written`);
    }
  }

  // step 2 with the loader found by find()/waitFor()
  async write(ota, image, step) {
    ota.link.send(UPGRADE);
    await sleep(2000);
    const s2 = await this.serve(ota.link, image, 0xF0000000, 180000, (n, b) => step("write", n, b));
    await ota.link.close();
    return s2;
  }

  // resume: the device is already in update mode (loader) -> true when the write finished
  async resume(image, onStep) {
    const ota = await this.find(IS_OTA);
    if (!ota) return false;
    return (await this.write(ota, image, onStep || (() => {}))).finished;
  }
}
