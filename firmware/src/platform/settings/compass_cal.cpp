#include "platform/settings/compass_cal.h"

void CompassCal::start() {
  fit_.reset();
  MagCal c;
  if (settings_.loadMagCal(c)) fit_.setCalibration(c);   // so the field strength shown is the corrected one
  fit_.startCalibration();
  active_ = true;
}

bool CompassCal::finish(bool keep) {
  if (!active_) return false;
  active_ = false;
  const bool saved = fit_.finishCalibration(keep);
  if (keep && saved) settings_.saveMagCal(fit_.calibration());
  return keep && saved;
}
