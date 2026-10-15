#pragma once
#include "core/types.h"

// Parse one "$XCTRC,...*CS" line (no CR/LF). Verifies the checksum.
// Fields: Y,M,D,h,m,s,cs,lat,lon,alt,speed,course,vario,,,,pressure,battery
bool xctrcParse(const char* line, Sample& out);
