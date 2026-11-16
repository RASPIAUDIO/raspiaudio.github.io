// Pure log interpretation: no device commands or inferred boot success.
export function recoveryState(text, persistent = false) {
  const identity = text.match(/VOICE_DSP_V2_USB_RECOVERY_[^\s]+/g)?.at(-1);
  persistent ||= !!identity?.includes('_PERSISTENT');
  let message = persistent ? 'Waiting for the persistent writer status...' : 'Waiting for USB Recovery identity...';
  for (const line of text.split(/\r?\n/)) {
    if (line.includes('VOICE_DSP_V2_FACTORY_RAM_LOADER'))
      message = 'Factory tester installed. Install USB Recovery on this ZeroCore first.';
    else if (/BOOT_ABORT|AUTO_SKIPPED|SPI_RAM_ABORT/.test(line))
      message = 'Recovery did not start. Disconnect all power for 10 seconds, reconnect, then check this log.';
    else if (/SPI_RAM_SENT_NOT_VERIFIED|handed_off=1/.test(line))
      message = persistent ? 'RAM writer transferred; persistent installation is not confirmed yet.' : 'Old RAM-only Recovery. Install the new persistent recovery app.';
    else if (/INSTALL_BEGIN|INSTALL_ERASE/.test(line))
      message = 'Preparing XMOS flash. Keep both boards powered; do not reset or disconnect.';
    else if (line.includes('INSTALL_WRITE'))
      message = 'Writing and reading back XMOS firmware: ' + line.split('INSTALL_WRITE ')[1] + ' bytes. Keep power connected.';
    else if (/INSTALL_ABORT|INSTALL_FAILED|INSTALL_STATUS_TIMEOUT|INSTALL_STATUS_FAILED/.test(line))
      message = 'Installation failed. Keep the log; do not assume XMOS firmware is installed.';
    else if (/INSTALL_QSPI_VERIFIED|RECOVERY attempted=1 verified=1/.test(line))
      message = 'QSPI write and readback verified. Power off both boards, remove ZeroCore, then connect Voice DSP+ USB alone to check standalone startup.';
    else if (line.includes('RECOVERY attempted=1 verified=0'))
      message = 'Persistent installation not confirmed. Check the preceding log for a failure or active operation.';
  }
  return {persistent, message};
}
