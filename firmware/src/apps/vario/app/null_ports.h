#pragma once
// Ports for an App with nothing behind them: the compass & weather tool is the vario App without a tracer
// and without a laptop.
#include "apps/vario/app/ports.h"

class NullTracer : public ITracer {
 public:
  void update(uint32_t) override {}
  ViewModel::Link link() const override { return ViewModel::LINK_NONE; }
  bool hasSaved() const override { return false; }
  const char* deviceName() const override { return ""; }
  const char* filter() const override { return "*"; }
  void setFilter(const char*) override {}
  void listOpen() override {}
  void listClose() override {}
  int listCount() override { return 0; }
  bool listItem(int, ListItem&) override { return false; }
  bool listConnect(int) override { return false; }
  bool popLine(char*, size_t) override { return false; }
  void describe(char* buf, size_t n) const override { if (n) buf[0] = 0; }
  void pause() override {}
  void resume() override {}
};

class NullLaptop : public ILaptop {
 public:
  bool pop(Command&) override { return false; }
  bool hostDrawing(uint32_t) override { return false; }
  void service(uint32_t) override {}
};
