const connect = document.querySelector('#connect');
const disconnect = document.querySelector('#disconnect');
const log = document.querySelector('#log');
const result = document.querySelector('#result');
let port, reader, reading;
let stopping = false;
let identified = false;
let tail = '';

function append(text) {
  log.textContent = (log.textContent + text).slice(-24000);
  log.scrollTop = log.scrollHeight;
  tail = (tail + text).slice(-2000);
  if (tail.includes('VOICE_DSP_V2_USB_RECOVERY_0.1.0')) identified = true;
  if (tail.includes('VOICE_DSP_V2_FACTORY_RAM_LOADER')) {
    result.textContent = 'Factory tester still installed. Install USB Recovery on this ZeroCore first.';
  } else if (identified && /handed_off=1/.test(tail)) {
    result.textContent = 'SPI transfer completed. Now verify XMOS detection and install with the USB updater; QSPI is not written yet.';
  } else if (identified && /outcome=(BOOT_ABORT|AUTO_SKIPPED|SPI_RAM_ABORT)/.test(tail)) {
    result.textContent = 'Recovery did not finish. Disconnect all power for 10 seconds, reconnect, then read the log again.';
  } else if (!identified && tail.includes('READY')) {
    result.textContent = 'Serial device responds, but USB Recovery identity is not confirmed.';
  }
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
  connect.disabled = true; identified = false; tail = ''; log.textContent = '';
  try {
    if (!navigator.serial) throw new Error('Use desktop Chrome or Edge over HTTPS.');
    port = await navigator.serial.requestPort({filters:[{usbVendorId:0x303a}]});
    await port.open({baudRate:115200});
    stopping = false; reader = port.readable.getReader(); reading = readLoop();
    disconnect.disabled = false;
    result.textContent = 'Reading ZeroCore identity and last boot result...';
    // Read-only commands: never reset or request another SPI transfer here.
    const writer = port.writable.getWriter();
    try { await writer.write(new TextEncoder().encode('help\ncold-status\n')); }
    finally { writer.releaseLock(); }
  } catch (error) {
    result.textContent = error.message;
    try { await closeMonitor(); } catch (_) { connect.disabled = false; }
  }
});
disconnect.addEventListener('click', async () => {
  try { await closeMonitor(); } catch (error) { append(`\n${error.message}\n`); }
});
