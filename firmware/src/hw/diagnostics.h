// Serial status line (st / dev / raw / cmp). Benchmarks only with -DDIAGNOSTICS.
#pragma once
#include <stdint.h>

class App;
class SharpPanel;
class CompassLis;

void diagnosticsBegin(App& app, SharpPanel& panel, CompassLis& compass);
// Prints every 2 s when a terminal is attached; never blocks. worstTickUs is reset after each line.
void diagnosticsTick(uint32_t now, uint32_t& worstTickUs);
