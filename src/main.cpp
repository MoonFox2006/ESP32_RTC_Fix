#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

constexpr uint8_t LED_PIN = 23;
constexpr bool LED_LEVEL = LOW;
constexpr uint32_t LED_PULSE = 25; // 25 ms.

const char WIFI_SSID[] = "YOUR_SSID";
const char WIFI_PSWD[] = "YOUR_PSWD";

const char NTP_SERVER[] = "pool.ntp.org";
constexpr int8_t NTP_TZ = 3; // Your time zone (GMT*)

constexpr uint32_t SLEEP_TIME = 1800; // 30 min.
constexpr uint32_t REPEAT_TIME = 300; // 5 min.

uint32_t RTC_DATA_ATTR lastTime = 0;
uint32_t RTC_DATA_ATTR usInSec = 1000000;

static bool wifiConnect(uint32_t timeout = 30000) {
  Serial.printf("Connecting to \"%s\"", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  timeout += millis();
  while ((! WiFi.isConnected()) && ((int32_t)millis() < (int32_t)timeout)) {
    digitalWrite(LED_PIN, LED_LEVEL);
    Serial.print('.');
    delay(LED_PULSE);
    digitalWrite(LED_PIN, ! LED_LEVEL);
    delay(500 - LED_PULSE);
  }
  if (WiFi.isConnected()) {
    Serial.print(" OK (IP: ");
    Serial.print(WiFi.localIP());
    Serial.println(')');
    return true;
  } else {
    WiFi.disconnect();
    Serial.println(" FAIL!");
    return false;
  }
}

static uint32_t ntpUpdate(uint32_t timeout = 1000, uint8_t repeat = 1) {
  constexpr uint16_t LOCAL_PORT = 55123;

  WiFiUDP udp;

  if (udp.begin(LOCAL_PORT)) {
    do {
      uint8_t buffer[48];

      memset(buffer, 0, sizeof(buffer));
      // Initialize values needed to form NTP request
      buffer[0] = 0B11100011; // LI, Version, Mode
      buffer[1] = 0; // Stratum, or type of clock
      buffer[2] = 6; // Polling Interval
      buffer[3] = 0xEC; // Peer Clock Precision
      // 8 bytes of zero for Root Delay & Root Dispersion
      buffer[12] = 49;
      buffer[13] = 0x4E;
      buffer[14] = 49;
      buffer[15] = 52;
      // all NTP fields have been given values, now
      // you can send a packet requesting a timestamp
      if (udp.beginPacket(NTP_SERVER, 123) && (udp.write(buffer, sizeof(buffer)) == sizeof(buffer)) && udp.endPacket()) {
        uint32_t time = millis();
        int cb;

        while ((! (cb = udp.parsePacket())) && (millis() - time < timeout)) {
          delay(1);
        }
        if (cb) {
          // We've received a packet, read the data from it
          if (udp.read(buffer, sizeof(buffer)) == sizeof(buffer)) { // read the packet into the buffer
            // the timestamp starts at byte 40 of the received packet and is four bytes,
            // or two words, long. First, esxtract the two words:
            time = (((uint32_t)buffer[40] << 24) | ((uint32_t)buffer[41] << 16) | ((uint32_t)buffer[42] << 8) | buffer[43]) - 2208988800UL;
            time += NTP_TZ * 3600;
            return time;
          }
        }
      }
      if (repeat)
        delay(timeout / 2);
    } while (repeat--);
  }
  return 0;
}

static void printTime(uint32_t time) {
  Serial.printf("%u (%02u:%02u:%02u)", time, (time % 86400) / 3600, (time % 3600) / 60, time % 60);
}

static void fixTime(uint32_t rtc, uint32_t ntp) {
  usInSec = (uint64_t)(rtc - lastTime) * 1000000 / (ntp - lastTime);
}

void setup() {
  timeval tm;
  uint32_t sleepTime;

  Serial.begin(115200);
  Serial.println();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, ! LED_LEVEL);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  if (wifiConnect()) {
    uint32_t time;

    time = ntpUpdate();
    if (time) {
      gettimeofday(&tm, nullptr);
      if (lastTime)
        fixTime((uint32_t)tm.tv_sec, time);
      Serial.print("RTC time: ");
      printTime((uint32_t)tm.tv_sec);
      Serial.print(", NTP time: ");
      printTime(time);
      Serial.println();
      tm.tv_sec = time;
      tm.tv_usec = 0;
      settimeofday(&tm, nullptr);
      sleepTime = SLEEP_TIME - time % SLEEP_TIME;
      if (lastTime && (sleepTime <= SLEEP_TIME / 5)) // 20%
        sleepTime += SLEEP_TIME;
      lastTime = time;
    } else {
      sleepTime = REPEAT_TIME;
      Serial.println("Error updating time!");
    }
    WiFi.disconnect();
  } else
    sleepTime = REPEAT_TIME;

  Serial.printf("Going to sleep for %u sec. (1 sec. = %u us.)...\n", sleepTime, usInSec);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)sleepTime * usInSec);
  esp_deep_sleep_disable_rom_logging();
  esp_deep_sleep_start();
}

void loop() {}
