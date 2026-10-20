#pragma once
// MotionService (platform/sensors/motion.h): sensors only between start() and stop(), the heading of the compass alone and of the
// gyro fusion, the stored calibration and mounting applied, tilt for games. Included by tests.cpp (after mocks.h, box_tests.h).
#include "platform/sensors/motion.h"

namespace mot {

static float angDist(float a, float b) {
  float d = fmodf(fabsf(a - b), 360.0f);
  return d > 180.0f ? 360.0f - d : d;
}

static void service() {
  puts("motion service: lifecycle, heading (compass alone and fused), calibration, mounting, tilt");
  MockKeyStore keys;
  BoxSettings settings(keys);
  MockCompass hw;
  MotionService m(hw, settings);

  // Nobody asked: the sensors are off and nothing is read.
  CHECK(!m.running() && !hw.enabled);
  CHECK(!m.poll(0) && !m.valid());

  // Compass alone (no gyro): tilt-compensated magnetic heading.
  hw.psi = 120; hw.pitch = 15; hw.roll = -8;
  m.start();
  CHECK(m.running() && hw.enabled && hw.enables == 1);
  m.start();
  CHECK(hw.enables == 1);                                       // start() twice does nothing
  for (uint32_t t = 0; t < 2000; t += 12) m.poll(t);
  CHECK(m.valid() && !m.fused() && !m.quaternionValid());
  NEAR(angDist(m.heading(), 120.0f), 0, 1.0);
  float roll, pitch;
  CHECK(m.tilt(&roll, &pitch));
  NEAR(pitch, 15.0, 1.5);
  NEAR(roll, -8.0, 1.5);                                       // sim convention: right side down = positive roll
  CHECK(m.fieldUt() > 10.0f && !m.calibrated());
  CHECK(m.fieldQuality() == 0.0f);                              // no calibration radius: no verdict

  // Stop: sensors off, filters reset, nothing valid, a poll reads nothing.
  m.stop();
  CHECK(!m.running() && !hw.enabled && !m.valid() && !m.fused());
  CHECK(!m.poll(5000));

  // With a gyro the heading is the fusion's and the attitude is available.
  hw.gyro = true;
  hw.gyroFromMotion = true;
  hw.psi = 250; hw.pitch = 0; hw.roll = 0;
  m.start();
  uint32_t t = 10000;
  for (; t < 16000; t += 12) { hw.nowMs = t; m.poll(t); }
  CHECK(m.fused() && m.valid() && m.quaternionValid() && !m.gyroFailed() && m.gyroSample());
  NEAR(angDist(m.heading(), 250.0f), 0, 2.0);
  float q[4];
  m.quaternion(q);
  NEAR(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1.0, 1e-3);
  CHECK(m.trust() > 0.5f && m.fieldQuality() > 0.5f);

  // The gyro goes quiet: after 0.5 s the compass alone again.
  hw.gyro = false;
  for (uint32_t e = t + 800; t < e; t += 12) { hw.nowMs = t; m.poll(t); }
  CHECK(!m.fused() && m.valid());
  m.stop();

  // Calibration from the settings: the hard iron offset is removed (a magnet of 20 uT would turn the compass alone otherwise).
  hw.gyro = false;
  hw.hardIron[0] = 20; hw.hardIron[1] = -15; hw.hardIron[2] = 5;
  hw.psi = 90; hw.pitch = 0; hw.roll = 0;
  MagCal cal;
  cal.valid = true; cal.off[0] = 20; cal.off[1] = -15; cal.off[2] = 5; cal.radius = 45;
  settings.saveMagCal(cal);
  m.start();
  for (t = 0; t < 2000; t += 12) m.poll(t);
  CHECK(m.calibrated());
  NEAR(angDist(m.heading(), 90.0f), 0, 1.5);
  CHECK(m.fieldQuality() > 0.5f);
  // New offsets stored while running (a calibration finished): recalibrated() takes them.
  hw.hardIron[0] = hw.hardIron[1] = hw.hardIron[2] = 0;
  cal.off[0] = cal.off[1] = cal.off[2] = 0;
  settings.saveMagCal(cal);
  m.recalibrated();
  for (t = 3000; t < 5000; t += 12) m.poll(t);
  NEAR(angDist(m.heading(), 90.0f), 0, 1.5);
  m.stop();

  // Mounting: the board is mounted rotated; the index from the settings brings the device frame back.
  hw.boardMount = 7;
  hw.hardIron[0] = hw.hardIron[1] = hw.hardIron[2] = 0;
  hw.psi = 200; hw.pitch = 10; hw.roll = 0;
  CHECK(settings.setMountIndex(7));
  m.start();
  CHECK(m.mountIndex() == 7);
  for (t = 0; t < 2000; t += 12) m.poll(t);
  NEAR(angDist(m.heading(), 200.0f), 0, 1.5);
  CHECK(m.tilt(&roll, &pitch));
  NEAR(pitch, 10.0, 1.5);
  CHECK(settings.setMountIndex(0));
  m.remount();                                                  // changed under a running service
  CHECK(m.mountIndex() == 0);
  m.stop();
  hw.boardMount = 0;
}

static void run() { service(); }

}  // namespace mot
