#include "hw/compass_lis.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_LSM6DS33.h>

static Adafruit_LSM6DS33 imu;
static Adafruit_LIS3MDL mag;

// Chip axes -> board frame: how each chip is soldered relative to the others on the Feather Sense. These are
// fixed by the PCB, not by the case. Identity until measured: neither the Adafruit libraries nor the
// board docs in the library folders say, and host/orient_analyze.py reports a mismatch between the two
// chips if there is one (it prints the replacement for toBoardMag). The gyro is in the LSM6DS33, so it
// shares toBoardAccel. The board -> case rotation is a separate setting (core/mount.h, `ctl.py orient`).
static Vec3 toBoardAccel(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }
static Vec3 toBoardMag(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }

bool CompassLis::begin() {
  Wire.begin();
  haveImu_ = imu.begin_I2C();
  haveMag_ = mag.begin_I2C();
  if (haveImu_) imu.setGyroRange(LSM6DS_GYRO_RANGE_500_DPS);       // hand rotations; 17.5 mdps per count
  if (haveMag_) {
    // 80 Hz needs medium performance (datasheet: UHP max 10 Hz, HP 40 Hz, MP 80 Hz). The library's
    // setDataRate() leaves the mode alone for these rates, so set both.
    mag.setDataRate(LIS3MDL_DATARATE_80_HZ);
    mag.setPerformanceMode(LIS3MDL_MEDIUMMODE);
    mag.setRange(LIS3MDL_RANGE_4_GAUSS);
  }
  enable(false);
  return haveImu_ && haveMag_;
}

void CompassLis::enable(bool on) {
  on_ = on;
  if (haveImu_) {
    imu.setAccelDataRate(on ? LSM6DS_RATE_104_HZ : LSM6DS_RATE_SHUTDOWN);
    imu.setGyroDataRate(on ? LSM6DS_RATE_104_HZ : LSM6DS_RATE_SHUTDOWN);
  }
  gyroFrom_ = millis() + 100;                          // the gyro needs ~80 ms to start; ignore it until then
  if (haveMag_) mag.setOperationMode(on ? LIS3MDL_CONTINUOUSMODE : LIS3MDL_POWERDOWNMODE);
}

// LIS3MDL STATUS register: the library has no data-ready call, so read it directly (one byte).
static const uint8_t MAG_ADDR = 0x1C, MAG_STATUS = 0x27;
static bool magReady() {
  Wire.beginTransmission(MAG_ADDR);
  Wire.write(MAG_STATUS);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MAG_ADDR, (uint8_t)1) != 1) return false;
  return (Wire.read() & 0x08) != 0;                   // ZYXDA: a new sample on all three axes
}

// The magnetometer paces the set (80 Hz); the accelerometer and gyro (104 Hz) are read right after in one burst, so it is never
// older than 10 ms. Nothing is read (and no time is spent) when there is no new sample.
bool CompassLis::read(Vec3& a, Vec3& m, Vec3& g, bool& haveGyro) {
  if (!on_ || !haveImu_ || !haveMag_) return false;
  if (!magReady()) return false;
  mag.read();
  m_ = toBoardMag(mag.x_gauss * 100.0f, mag.y_gauss * 100.0f, mag.z_gauss * 100.0f);   // gauss -> microtesla
  sensors_event_t ea, eg, et;
  imu.getEvent(&ea, &eg, &et);                        // one burst; at 104 Hz it is always newer than the last pair
  a_ = toBoardAccel(ea.acceleration.x, ea.acceleration.y, ea.acceleration.z);
  haveGyro = (int32_t)(millis() - gyroFrom_) >= 0;
  g_ = toBoardAccel(eg.gyro.x, eg.gyro.y, eg.gyro.z);   // rad/s; same burst as the accelerometer, no extra bus time
  g = g_;
  a = a_;
  m = m_;
  return true;
}
