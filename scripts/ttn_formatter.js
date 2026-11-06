// Uplink payload formatter for The Things Stack (Applications > Payload formatters > Uplink > Custom JavaScript).
// Port 1, 10 bytes, big endian; a missing value is all ones and left out of the result.
function decodeUplink(input) {
  var b = input.bytes;
  if (input.fPort !== 1 || b.length < 10) {
    return { errors: ["unknown payload (port " + input.fPort + ", " + b.length + " bytes)"] };
  }
  var u16 = function (i) { return (b[i] << 8) | b[i + 1]; };
  var data = {};
  if (u16(0) !== 0xFFFF) { data.co2 = u16(0); }
  var t = u16(2);
  if (t !== 0x7FFF) { data.temperature = ((t & 0x8000) ? t - 0x10000 : t) / 10; }
  if (b[4] !== 0xFF) { data.humidity = b[4] / 2; }
  if (u16(5) !== 0xFFFF) { data.pressure = u16(5) / 10; }
  if (u16(7) !== 0xFFFF) { data.light = u16(7); }
  data.quiet = (b[9] & 0x01) !== 0;
  data.error = (b[9] & 0x02) !== 0;
  data.calibrating = (b[9] & 0x04) !== 0;
  data.buzzer = (b[9] & 0x10) !== 0;
  return { data: data };
}
