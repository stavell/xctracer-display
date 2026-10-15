#include "hw/compass_lis.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_LIS3MDL.h>
#include <Adafruit_LSM6DS33.h>

static Adafruit_LSM6DS33 imu;
static Adafruit_LIS3MDL mag;

// Sensor axes -> device frame. Identity until measured on the real unit (see NOTES.md).
static Vec3 toDeviceAccel(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }
static Vec3 toDeviceMag(float x, float y, float z) { Vec3 v; v.x = x; v.y = y; v.z = z; return v; }

bool CompassLis::begin() {
  Wire.begin();
  haveImu_ = imu.begin_I2C();
  haveMag_ = mag.begin_I2C();
  if (haveMag_) {
    mag.setDataRate(LIS3MDL_DATARATE_10_HZ);
    mag.setPerformanceMode(LIS3MDL_MEDIUMMODE);
    mag.setRange(LIS3MDL_RANGE_4_GAUSS);
  }
  enable(false);
  return haveImu_ && haveMag_;
}

void CompassLis::enable(bool on) {
  on_ = on;
  if (haveImu_) {
    imu.setAccelDataRate(on ? LSM6DS_RATE_26_HZ : LSM6DS_RATE_SHUTDOWN);
    imu.setGyroDataRate(LSM6DS_RATE_SHUTDOWN);
  }
  if (haveMag_) mag.setOperationMode(on ? LIS3MDL_CONTINUOUSMODE : LIS3MDL_POWERDOWNMODE);
  last_ = 0;
}

bool CompassLis::read(Vec3& a, Vec3& m) {
  if (!on_ || !haveImu_ || !haveMag_) return false;
  uint32_t now = millis();
  if (last_ && now - last_ < 100) return false;
  last_ = now;
  sensors_event_t ea, eg, et, em;
  imu.getEvent(&ea, &eg, &et);
  mag.getEvent(&em);
  a_ = toDeviceAccel(ea.acceleration.x, ea.acceleration.y, ea.acceleration.z);
  m_ = toDeviceMag(em.magnetic.x, em.magnetic.y, em.magnetic.z);
  a = a_;
  m = m_;
  return true;
}
