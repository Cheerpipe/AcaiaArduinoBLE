#pragma once

// Single include for Arduino Preferences vs host stubs. Not an NVS ORM.

#if defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include "tests/persistence_host_stubs.h"
#elif !defined(SHOT_STOPPER_HOST_TEST)
#include <Preferences.h>
#endif
