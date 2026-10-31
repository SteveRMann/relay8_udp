/*
  ESP12F_Relay_X8_V1.1 - UDP relay controller
  Board: "Generic ESP8266 Module" (or NodeMCU 1.0), Flash 4MB

  UDP port 8888, plain-text commands (case-insensitive):
    1 ON          relay 1 on        (relays 1-8, "R1 ON" also works)
    1 OFF         relay 1 off
    1 TOGGLE      flip relay 1
    1 PULSE 500   relay 1 on for 500 ms, then off (default 500)
    ALL ON | ALL OFF | ALL TOGGLE      (staggered, see DELAY)
    MASK 0xA5     set all relays from a bitmask (bit0 = relay 1, staggered)
    DELAY 150     set stagger delay in ms between relays (0 = all at once)
    MARVIN        Marvin show: relay 1 ON, relay 2 OFF; after MARVIN_MS
                  relay 1 OFF, relay 2 ON. Re-sending restarts the timer.
    MARVIN STOP   end the show now (relay 1 OFF, relay 2 ON)
    MARVINTIME 15000   set the Relay 1 length in ms (until next reboot)
    STATUS        just report

  ALL / MASK switch one relay at a time, STAGGER_MS apart, to spread out
  inrush current. Relays already in the wanted state are skipped (no wait).
  A single-relay command cancels any queued change for that relay.
  A manual command on relay 1 or 2 (or ALL / MASK) cancels a running MARVIN.

  Every command gets a reply to the sender:
    R1=1 R2=0 R3=0 R4=0 R5=0 R6=0 R7=0 R8=0 MASK=0x01 DELAY=100 MARVIN_MS=15000
  with " PENDING=n" appended while a staggered sequence is still running,
  and " MARVIN_LEFT=n" (ms) while the Marvin show is running.

  Test from Linux:
    echo -n "1 ON" | nc -u -w1 relay8.local 8888
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

const char*    WIFI_SSID = "Kaywinnet";
const char*    WIFI_PASS = "806194edb8";
const char*    HOSTNAME  = "relay8";
const uint16_t UDP_PORT  = 8888;

const uint8_t RELAY_PINS[8] = {16, 14, 12, 13, 15, 0, 4, 5};
const bool    ACTIVE_HIGH   = true;   // NPN drivers on this board: HIGH = relay on

unsigned long STAGGER_MS = 100;       // default delay between relays for ALL/MASK
unsigned long MARVIN_MS  = 10000;     // Marvin show length (relay 1 on time)

WiFiUDP udp;
bool state[8];
unsigned long pulseEnd[8];            // 0 = no pulse running
unsigned long marvinEnd = 0;          // 0 = Marvin show not running

// Staggered sequencer
bool pending[8];                      // relay i has a queued change
bool target[8];                       // ...to this state
unsigned long lastStep = 0;

void setRelay(int i, bool on) {
  state[i] = on;
  pulseEnd[i] = 0;
  pending[i] = false;
  digitalWrite(RELAY_PINS[i], (on == ACTIVE_HIGH) ? HIGH : LOW);
}

// Marvin show: start = relay 1 on, relay 2 off; end = relay 1 off, relay 2 on
void marvinStart() {
  setRelay(0, true);
  setRelay(1, false);
  marvinEnd = millis() + MARVIN_MS;
  if (marvinEnd == 0) marvinEnd = 1;
}

void marvinFinish() {
  marvinEnd = 0;
  setRelay(0, false);
  setRelay(1, true);
}

// State the relay is heading to (queued target, or current state)
bool effectiveState(int i) {
  return pending[i] ? target[i] : state[i];
}

// Queue a change; only relays that actually need to change are queued
void queueRelay(int i, bool on) {
  if (on == state[i] && !pulseEnd[i]) {
    pending[i] = false;               // already there
    return;
  }
  target[i] = on;
  pending[i] = true;
}

int pendingCount() {
  int c = 0;
  for (int i = 0; i < 8; i++) if (pending[i]) c++;
  return c;
}

// Apply the next queued relay change when the stagger delay has elapsed
void runSequencer() {
  if (!pendingCount()) return;
  unsigned long now = millis();
  if (lastStep && now - lastStep < STAGGER_MS) return;
  for (int i = 0; i < 8; i++) {
    if (pending[i]) {
      setRelay(i, target[i]);
      lastStep = now;
      if (STAGGER_MS > 0) return;     // one per step; 0 = do them all now
    }
  }
}

String statusString() {
  String s;
  uint8_t mask = 0;
  for (int i = 0; i < 8; i++) {
    s += "R" + String(i + 1) + "=" + (state[i] ? "1 " : "0 ");
    if (state[i]) mask |= (1 << i);
  }
  char m[48];
  snprintf(m, sizeof(m), "MASK=0x%02X DELAY=%lu", mask, STAGGER_MS);
  s += m;
  s += " MARVIN_MS=" + String(MARVIN_MS);
  int p = pendingCount();
  if (p) s += " PENDING=" + String(p);
  if (marvinEnd) {
    long left = (long)(marvinEnd - millis());
    s += " MARVIN_LEFT=" + String(left > 0 ? left : 0);
  }
  return s;
}

String handleCommand(char* buf) {
  for (char* p = buf; *p; p++) *p = toupper(*p);

  char a[16] = "", b[16] = "";
  long n = 500;
  int cnt = sscanf(buf, "%15s %15s %ld", a, b, &n);
  if (cnt < 1) return "ERR empty";

  if (!strcmp(a, "STATUS")) return statusString();

  if (!strcmp(a, "MARVIN")) {
    if (!strcmp(b, "STOP")) marvinFinish();
    else                    marvinStart();
    return statusString();
  }

  if (!strcmp(a, "MARVINTIME")) {
    if (cnt < 2) return statusString();
    long t = strtol(b, NULL, 0);
    if (t < 1) t = 1;
    if (t > 3600000) t = 3600000;     // max 1 hour
    MARVIN_MS = t;
    return statusString();
  }

  if (!strcmp(a, "DELAY")) {
    if (cnt < 2) return statusString();
    long d = strtol(b, NULL, 0);
    if (d < 0) d = 0;
    if (d > 10000) d = 10000;
    STAGGER_MS = d;
    return statusString();
  }

  if (!strcmp(a, "ALL")) {
    if (strcmp(b, "ON") && strcmp(b, "OFF") && strcmp(b, "TOGGLE"))
      return "ERR use ALL ON|OFF|TOGGLE";
    marvinEnd = 0;                    // manual override cancels Marvin show
    for (int i = 0; i < 8; i++) {
      if      (!strcmp(b, "ON"))  queueRelay(i, true);
      else if (!strcmp(b, "OFF")) queueRelay(i, false);
      else                        queueRelay(i, !effectiveState(i));
    }
    runSequencer();                   // first relay switches immediately
    return statusString();
  }

  if (!strcmp(a, "MASK")) {
    if (cnt < 2) return "ERR use MASK <value>";
    marvinEnd = 0;                    // manual override cancels Marvin show
    long mask = strtol(b, NULL, 0);   // accepts 165, 0xA5, 0245
    for (int i = 0; i < 8; i++) queueRelay(i, mask & (1 << i));
    runSequencer();
    return statusString();
  }

  // Relay number, optionally prefixed with R
  const char* num = (a[0] == 'R') ? a + 1 : a;
  int r = atoi(num);
  if (r < 1 || r > 8) return "ERR unknown command";
  int i = r - 1;

  if (strcmp(b, "ON") && strcmp(b, "OFF") && strcmp(b, "TOGGLE") && strcmp(b, "PULSE"))
    return "ERR use <1-8> ON|OFF|TOGGLE|PULSE [ms]";
  if (i <= 1) marvinEnd = 0;          // manual relay 1/2 cancels Marvin show

  if      (!strcmp(b, "ON"))     setRelay(i, true);
  else if (!strcmp(b, "OFF"))    setRelay(i, false);
  else if (!strcmp(b, "TOGGLE")) setRelay(i, !effectiveState(i));
  else {                              // PULSE
    if (cnt < 3) n = 500;
    if (n < 1) n = 1;
    setRelay(i, true);
    pulseEnd[i] = millis() + n;
    if (pulseEnd[i] == 0) pulseEnd[i] = 1;
  }

  return statusString();
}

void setup() {
  // Relays off as early as possible
  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    setRelay(i, false);
  }

  Serial.begin(115200);
  Serial.println();

  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to Kaywinnet");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

  ArduinoOTA.setHostname(HOSTNAME);   // also starts mDNS: relay8.local
  ArduinoOTA.begin();

  udp.begin(UDP_PORT);
  Serial.printf("Listening for UDP on port %u\n", UDP_PORT);
}

void loop() {
  ArduinoOTA.handle();

  int len = udp.parsePacket();
  if (len > 0) {
    char buf[64];
    int n = udp.read(buf, sizeof(buf) - 1);
    buf[n > 0 ? n : 0] = '\0';
    // strip trailing CR/LF
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';

    String reply = handleCommand(buf);
    Serial.printf("%s:%u  '%s' -> %s\n", udp.remoteIP().toString().c_str(),
                  udp.remotePort(), buf, reply.c_str());

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print(reply);
    udp.endPacket();
  }

  runSequencer();

  // Finish any running pulses
  unsigned long now = millis();
  for (int i = 0; i < 8; i++) {
    if (pulseEnd[i] && (long)(now - pulseEnd[i]) >= 0) setRelay(i, false);
  }

  // End the Marvin show when its time is up
  if (marvinEnd && (long)(now - marvinEnd) >= 0) marvinFinish();
}
