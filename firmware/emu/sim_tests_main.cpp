// Only the flight sim tests, fast (no sanitizers, -O2): build.sh simtests [name]. name = one test function of sim_tests.h
// (for example sky_fx), none = all of them.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define NEAR(a, b, tol) CHECK(fabs((double)(a) - (double)(b)) <= (tol))
#include "mocks.h"
#include "platform/app_host.h"
#include "sim_tests.h"

int main(int argc, char** argv) {
  if (argc > 1 && !strcmp(argv[1], "sky_fx")) sim_tests::sky_fx();
  else sim_tests::run();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
