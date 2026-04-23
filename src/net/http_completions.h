#pragma once

#include "core/call_completions.h"
#include "net/http_call_state.h"

#include <engine/net/http_request.h>

namespace engine {

// Finished transfers waiting for the main thread.
using HttpCompletions = CallCompletions<HttpCallState, HttpResult>;

}
