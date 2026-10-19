#include "platform/hw/game_log.h"
#include <Arduino.h>

void GameLog::tick(uint32_t nowMs, uint32_t tickUs) {
  const bool game = host_.gameMode();
  // Nobody listening: do not even time (the printf is what costs, the spans are cheap, but stay out of the way of a flight).
  const bool listening = Serial && Serial.availableForWrite() >= 128;
  enable(game && listening, nowMs);
  if (!enabled()) return;
  noteTick(tickUs);
  if (!due(nowMs)) return;
  char* buf = buf_;                         // two writes from one buffer: the line, then the app's part
  report(buf, cap_, nowMs, host_.active()->name(), host_.stats(), nullptr);
  Serial.print(buf);
  buf[0] = ' ';
  if (host_.active()->gameInfo(buf + 1, cap_ - 1) <= 0) buf[1] = 0;
  Serial.println(buf);
}
