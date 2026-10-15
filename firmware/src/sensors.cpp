#include "sensors.h"
#include <Wire.h>
#include <PDM.h>
#include <Adafruit_APDS9960.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_LSM6DS33.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

static Adafruit_APDS9960 apds;
static Adafruit_BMP280 bmp(&Wire);
static Adafruit_SHT31 sht;
static Adafruit_LSM6DS33 imu;
static Adafruit_LIS3MDL mag;
static bool haveApds, haveBmp, haveSht, haveImu, haveMag;

static Readings R;
static CalState CS;
static Page curPage = PAGE_COUNT;   // none

// Device frame: x = right of screen, y = top of screen, z = out of screen.
// Sensor axes depend on how the board sits in the case: adjust these two only.
static void remapImu(float& x, float& y, float& z) { (void)x; (void)y; (void)z; }
static void remapMag(float& x, float& y, float& z) { (void)x; (void)y; (void)z; }

static const float SEA_LEVEL_HPA = 1013.25f;

// ---- magnetometer calibration -------------------------------------------
struct MagCal { uint32_t magic; float mn[3]; float mx[3]; };
static const uint32_t CAL_MAGIC = 0x4d414731;
static MagCal cal = {CAL_MAGIC, {-1, -1, -1}, {1, 1, 1}};
static MagCal calWork;

static void calLoad() {
  File f(InternalFS);
  if (f.open("/magcal.bin", FILE_O_READ)) {
    MagCal tmp;
    if (f.read((uint8_t*)&tmp, sizeof tmp) == sizeof tmp && tmp.magic == CAL_MAGIC) {
      cal = tmp;
      CS.valid = true;
    }
    f.close();
  }
}

static void calSave() {
  InternalFS.remove("/magcal.bin");
  File f(InternalFS);
  if (f.open("/magcal.bin", FILE_O_WRITE)) {
    f.write((uint8_t*)&cal, sizeof cal);
    f.close();
  }
}

void calStart() {
  for (int i = 0; i < 3; i++) { calWork.mn[i] = 1e9f; calWork.mx[i] = -1e9f; }
  CS.active = true;
  CS.spanX = CS.spanY = CS.spanZ = 0;
}

void calFinish(bool save) {
  CS.active = false;
  if (!save) return;
  float span = 0;
  for (int i = 0; i < 3; i++) span += calWork.mx[i] - calWork.mn[i];
  if (span < 30) return;   // barely moved: keep old calibration
  cal = calWork;
  cal.magic = CAL_MAGIC;
  CS.valid = true;
  calSave();
}

// ---- battery -------------------------------------------------------------
static int battPercent(float v) {
  static const float tv[] = {3.30f, 3.60f, 3.70f, 3.75f, 3.80f, 3.90f, 4.00f, 4.10f, 4.20f};
  static const int tp[] = {0, 5, 20, 35, 50, 65, 80, 92, 100};
  if (v <= tv[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (v <= tv[i]) return tp[i - 1] + (int)((v - tv[i - 1]) / (tv[i] - tv[i - 1]) * (tp[i] - tp[i - 1]));
  return 100;
}

static void readBattery() {
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++) raw += analogRead(PIN_VBAT);
  float mv = (raw / 8.0f) * 0.73242188f * 2.0f;   // 3.0 V / 4096, 2:1 divider
  R.battV = mv / 1000.0f;
  R.battPct = battPercent(R.battV);
}

// ---- microphone ----------------------------------------------------------
static volatile uint32_t pdmSumSq = 0;
static volatile uint32_t pdmCount = 0;
static volatile int16_t pdmPeak = 0;
static bool pdmOn = false;
static int16_t pdmBuf[256];

static void onPdm() {
  int bytes = PDM.available();
  if (bytes > (int)sizeof pdmBuf) bytes = sizeof pdmBuf;
  PDM.read(pdmBuf, bytes);
  int n = bytes / 2;
  uint32_t s = 0;
  int16_t pk = 0;
  for (int i = 0; i < n; i++) {
    int16_t v = pdmBuf[i];
    s += (int32_t)v * v / 16;      // scaled to avoid overflow
    int16_t a = v < 0 ? -v : v;
    if (a > pk) pk = a;
  }
  pdmSumSq += s;
  pdmCount += n;
  if (pk > pdmPeak) pdmPeak = pk;
}

static void micOn() {
  if (pdmOn) return;
  PDM.onReceive(onPdm);
  PDM.setGain(30);
  pdmOn = PDM.begin(1, 16000);
}

static void micOff() {
  if (!pdmOn) return;
  PDM.end();
  pdmOn = false;
}

// ---- sensor power per page ------------------------------------------------
static void imuPower(bool on) {
  if (!haveImu) return;
  imu.setAccelDataRate(on ? LSM6DS_RATE_26_HZ : LSM6DS_RATE_SHUTDOWN);
  imu.setGyroDataRate(LSM6DS_RATE_SHUTDOWN);
}

static void gyroPower(bool on) {
  if (haveImu) imu.setGyroDataRate(on ? LSM6DS_RATE_26_HZ : LSM6DS_RATE_SHUTDOWN);
}

static void magPower(bool on) {
  if (!haveMag) return;
  mag.setOperationMode(on ? LIS3MDL_CONTINUOUSMODE : LIS3MDL_POWERDOWNMODE);
}

static void apdsPower(bool color, bool gesture) {
  if (!haveApds) return;
  apds.enableColor(color);
  apds.enableProximity(color || gesture);
  apds.enableGesture(gesture);
}

void sensorsSleep() {
  curPage = PAGE_COUNT;
  imuPower(false);
  gyroPower(false);
  magPower(false);
  apdsPower(false, false);
  micOff();
  CS.active = false;
}

void sensorsSetPage(Page p) {
  if (p == curPage) return;
  if (curPage == PAGE_COMPASS && CS.active) calFinish(false);
  sensorsSleep();
  curPage = p;
  switch (p) {
    case PAGE_ENV:     apdsPower(true, false); break;
    case PAGE_COMPASS: imuPower(true); magPower(true); break;
    case PAGE_MOTION:  imuPower(true); gyroPower(true); break;
    case PAGE_LIGHT:   apdsPower(true, true); break;
    case PAGE_SOUND:   micOn(); break;
    default: break;
  }
}

void sensorsBegin() {
  Wire.begin();
  InternalFS.begin();
  calLoad();
  haveApds = apds.begin();
  haveBmp = bmp.begin(0x77, 0x58);
  haveSht = sht.begin(0x44);
  haveImu = imu.begin_I2C();
  haveMag = mag.begin_I2C();
  if (haveBmp)
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED, Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_OFF,
                    Adafruit_BMP280::STANDBY_MS_1000);
  if (haveMag) {
    mag.setDataRate(LIS3MDL_DATARATE_10_HZ);
    mag.setPerformanceMode(LIS3MDL_MEDIUMMODE);
    mag.setRange(LIS3MDL_RANGE_4_GAUSS);
  }
  Serial.printf("sensors: apds=%d bmp=%d sht=%d imu=%d mag=%d cal=%d\n",
                haveApds, haveBmp, haveSht, haveImu, haveMag, CS.valid);
  sensorsSleep();
}

// ---- readings -------------------------------------------------------------
static const float DEG = 57.2957795f;

static void readEnv() {
  if (haveSht) {
    R.tempC = sht.readTemperature();
    R.hum = sht.readHumidity();
  }
  if (haveBmp && bmp.takeForcedMeasurement()) {
    R.pressHpa = bmp.readPressure() / 100.0f;
    R.altM = bmp.readAltitude(SEA_LEVEL_HPA);
  }
  if (haveApds && apds.colorDataReady()) {
    apds.getColorData(&R.r, &R.g, &R.b, &R.c);
    R.lux = apds.calculateLux(R.r, R.g, R.b);
  }
  readBattery();
}

static void readLight() {
  if (!haveApds) return;
  if (apds.colorDataReady()) {
    apds.getColorData(&R.r, &R.g, &R.b, &R.c);
    R.lux = apds.calculateLux(R.r, R.g, R.b);
  }
  R.prox = apds.readProximity();
}

static void pollGesture() {
  if (!haveApds) return;
  uint8_t g = apds.readGesture();
  if (g) { R.gesture = g; R.gestureMs = millis(); }
}

static void readImu(bool gyro) {
  if (!haveImu) return;
  sensors_event_t a, g, t;
  imu.getEvent(&a, &g, &t);
  float x = a.acceleration.x, y = a.acceleration.y, z = a.acceleration.z;
  remapImu(x, y, z);
  const float k = 0.5f;   // light smoothing
  R.ax += k * (x - R.ax); R.ay += k * (y - R.ay); R.az += k * (z - R.az);
  if (gyro) {
    float gx = g.gyro.x * DEG, gy = g.gyro.y * DEG, gz = g.gyro.z * DEG;
    remapImu(gx, gy, gz);
    R.gx = gx; R.gy = gy; R.gz = gz;
  }
  R.pitch = atan2f(-R.ay, sqrtf(R.ax * R.ax + R.az * R.az)) * DEG;
  R.roll = atan2f(R.ax, R.az) * DEG;
}

static void readMag() {
  if (!haveMag) return;
  sensors_event_t m;
  mag.getEvent(&m);
  float x = m.magnetic.x, y = m.magnetic.y, z = m.magnetic.z;
  remapMag(x, y, z);
  if (CS.active) {
    float v[3] = {x, y, z};
    for (int i = 0; i < 3; i++) {
      if (v[i] < calWork.mn[i]) calWork.mn[i] = v[i];
      if (v[i] > calWork.mx[i]) calWork.mx[i] = v[i];
    }
    CS.spanX = calWork.mx[0] - calWork.mn[0];
    CS.spanY = calWork.mx[1] - calWork.mn[1];
    CS.spanZ = calWork.mx[2] - calWork.mn[2];
  }
  // hard iron offset + per-axis scale to the mean span
  float v[3] = {x, y, z}, c[3];
  float span[3], avg = 0;
  for (int i = 0; i < 3; i++) { span[i] = cal.mx[i] - cal.mn[i]; avg += span[i] / 3; }
  for (int i = 0; i < 3; i++) {
    float s = span[i] > 1 ? avg / span[i] : 1;
    c[i] = (v[i] - (cal.mx[i] + cal.mn[i]) / 2) * s;
  }
  const float k = 0.4f;
  R.mx += k * (c[0] - R.mx); R.my += k * (c[1] - R.my); R.mz += k * (c[2] - R.mz);
  R.fieldUt = sqrtf(R.mx * R.mx + R.my * R.my + R.mz * R.mz);

  // tilt-compensated heading of the top of the screen:
  // east = m x up, north = up x east, heading = atan2(east_y, north_y)
  float ux = R.ax, uy = R.ay, uz = R.az;
  float ex = R.my * uz - R.mz * uy;
  float ey = R.mz * ux - R.mx * uz;
  float ez = R.mx * uy - R.my * ux;
  float ny = uz * ex - ux * ez;
  float h = atan2f(ey, ny) * DEG;
  if (h < 0) h += 360;
  R.heading = h;
}

static void updateMic() {
  uint32_t n = pdmCount;
  if (n < 1600) return;    // 100 ms of audio
  noInterrupts();
  uint32_t s = pdmSumSq; n = pdmCount; int16_t pk = pdmPeak;
  pdmSumSq = 0; pdmCount = 0; pdmPeak = 0;
  interrupts();
  float rms = sqrtf((float)s * 16.0f / n);
  R.micDb = 20.0f * log10f(max(rms, 1.0f) / 32768.0f);
  float pdb = 20.0f * log10f(max((float)pk, 1.0f) / 32768.0f);
  R.micPeakDb = max(pdb, R.micPeakDb - 1.5f);   // decaying peak hold
}

void sensorsUpdate() {
  static uint32_t tSlow = 0, tFast = 0, tGesture = 0;
  uint32_t now = millis();
  switch (curPage) {
    case PAGE_ENV:
      if (now - tSlow >= 1000 || tSlow == 0) { tSlow = now; readEnv(); }
      break;
    case PAGE_COMPASS:
      if (now - tFast >= 100) { tFast = now; readImu(false); readMag(); }
      break;
    case PAGE_MOTION:
      if (now - tFast >= 100) { tFast = now; readImu(true); }
      break;
    case PAGE_LIGHT:
      if (now - tGesture >= 30) { tGesture = now; pollGesture(); }
      if (now - tFast >= 200) { tFast = now; readLight(); }
      break;
    case PAGE_SOUND:
      updateMic();
      break;
    default: break;
  }
  if (curPage != PAGE_ENV) {
    static uint32_t tBatt = 0;
    if (now - tBatt >= 5000 || tBatt == 0) { tBatt = now; readBattery(); }
  }
}

const Readings& readings() { return R; }
const CalState& calState() { return CS; }
