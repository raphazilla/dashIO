#pragma once
#include <Arduino.h>

// Process a terminal command from a WebSocket client.
// Returns a JSON string: { "type":"terminal_out", "seq":N, "output":"...", "ok":true, "cwd":"..." }
String terminal_exec(uint32_t client_id, const String& cmd, int seq);

// Called when a WebSocket client disconnects — cleans up its CWD state.
void terminal_client_disconnected(uint32_t client_id);
