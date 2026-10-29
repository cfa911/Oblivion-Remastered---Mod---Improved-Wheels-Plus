#pragma once
#include "engine/SessionEpoch.h"

namespace sessioncontext
{
	// Observational guard: compare at enqueue AND immediately before TES execution.
	// Same-character loads require observing the engine's busy/unavailable state.
	sessionepoch::Token Current();
}
