#pragma once

#include "quantum.h"

#include <stdint.h>

// Retrieves persisted default layer from EEPROM configuration.
// Global variables: (none - reads from EEPROM only)
layer_state_t persistent_default_layer_get(void);

// Persists default layer to EEPROM and activates it immediately.
// Global variables: (none - updates EEPROM and layer state)
void persistent_default_layer_set(uint16_t default_layer);
