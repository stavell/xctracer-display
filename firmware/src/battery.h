#pragma once
// Local LiPo battery from the Feather's VBAT divider.
void batteryUpdate();      // call often; samples every few seconds
int batteryPct();
float batteryVolts();
