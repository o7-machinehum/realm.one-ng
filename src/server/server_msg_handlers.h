// Message dispatch: routes decoded envelopes to per-type handler functions.
#pragma once

#include "server_state.h"
#include "envelope.h"

#include <enet/enet.h>

// Routes an incoming Envelope to the correct per-type handler.
void dispatchMessage(ServerState& state, ENetPeer* peer, const Envelope& env);
