// Serial status line (st / dev / raw / cmp). Benchmarks only with -DDIAGNOSTICS.
#pragma once
#include <stddef.h>
#include <stdint.h>

class App;
class SharpPanel;
class CompassLis;
class PackService;

void diagnosticsBegin(App* app, SharpPanel& panel, CompassLis& compass);
// The map pack service whose "pack ..." line joins the status output (optional).
void diagnosticsSetPack(PackService* pack);
// Prints every 2 s when a terminal is attached; never blocks. worstTickUs is reset after each line.
void diagnosticsSetApp(App* app);                  // box image: the vario's App comes and goes; nullptr while it does not exist
void diagnosticsTick(uint32_t now, uint32_t& worstTickUs);
// One-shot drawing micro-benchmark on the real panel canvas (rotation 1); DIAGNOSTICS builds only, a no-op
// otherwise. Call after setup, before the first frame. Also installs the draw-time profiler clock.
void diagnosticsBench();
// One text buffer shared by everything that formats a status line (the loop task is the only user, one at a time): static
// RAM that a second private buffer would take from the heap budget.
char* diagnosticsScratch(size_t* cap);
