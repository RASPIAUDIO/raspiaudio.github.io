import {recoveryState} from './status-state.mjs';
const connect = document.querySelector('#connect');
const disconnect = document.querySelector('#disconnect');
const log = document.querySelector('#log');
const result = document.querySelector('#result');
let port, reader, reading;
let stopping = false;
let persistent = false;
let pendingLine = '';

function append(text) {
  log.textContent = (log.textContent + text).slice(-24000);
  log.scrollTop = log.scrollHeight;
  pendingLine += text;
  const end = pendingLine.lastIndexOf('\n');
  if (end < 0) return;
  const complete = pendingLine.slice(0, end + 1);
  pendingLine = pendingLine.slice(end + 1);
  const state = recoveryState(complete, persistent);
  persistent = state.persistent;
  if (!state.message.startsWith('Waiting')) result.textContent = state.message;
}

async function readLoop() {
  const decoder = new TextDecoder();
  try {
    while (!stopping) {
      const {value, done} = await reader.read();
      if (done) break;
      if (value) append(decoder.decode(value, {stream:true}));
    }
  } catch (error) {
    if (!stopping) append(`\nSerial connection ended: ${error.message}\n`);
  } finally {
    reader.releaseLock(); reader = null;
    if (!stopping) {
      try { await port.close(); } catch (_) { /* USB may already be gone. */ }
      port = null; connect.disabled = false; disconnect.disabled = true;
    }
  }
}

async function closeMonitor() {
  stopping = true;
  if (reader) await reader.cancel();
  if (reading) await reading;
  if (port) { try { await port.close(); } finally { port = null; } }
  connect.disabled = false; disconnect.disabled = true;
}

connect.addEventListener('click', async () => {
  connect.disabled = true; persistent = false; pendingLine = ''; log.textContent = '';
  try {
    if (!navigator.serial) throw new Error('Use desktop Chrome or Edge over HTTPS.');
    port = await navigator.serial.requestPort({filters:[{usbVendorId:0x303a}]});
    await port.open({baudRate:115200});
    stopping = false; reader = port.readable.getReader(); reading = readLoop();
    disconnect.disabled = false;
    result.textContent = 'Reading ZeroCore identity and last boot result...';
    // Read-only commands: never reset or request another SPI transfer here.
    const writer = port.writable.getWriter();
    try { await writer.write(new TextEncoder().encode('help\ncold-status\nrecovery-status\n')); }
    finally { writer.releaseLock(); }
  } catch (error) {
    result.textContent = error.message;
    try { await closeMonitor(); } catch (_) { connect.disabled = false; }
  }
});
disconnect.addEventListener('click', async () => {
  try { await closeMonitor(); } catch (error) { append(`\n${error.message}\n`); }
});
