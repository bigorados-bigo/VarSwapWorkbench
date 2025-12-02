#include "varswap/settings_state.h"

// Standalone VarSwap workbench only needs the default gSettings instance
// so we provide a lightweight definition without pulling the full ini.cpp
// dependency chain (CG loading, Parts, etc.).
Settings gSettings;
