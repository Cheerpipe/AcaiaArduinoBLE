#include "../ShotStopperDomain.h"

#include <cstdlib>

int main() {
  static_assert(!shotstopper::REMOTE_CN9_CONTROL_ENABLED,
                "Remote CN9 control must remain opt-in by default");
  return shotstopper::REMOTE_CN9_CONTROL_ENABLED ? EXIT_FAILURE
                                                 : EXIT_SUCCESS;
}
