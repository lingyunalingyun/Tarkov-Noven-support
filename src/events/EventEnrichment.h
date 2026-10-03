#pragma once
#include "events/EventService.h"
#include "events/EventHttp.h"

namespace noven::events {
EventEnrichment MakeEventEnrichment(std::filesystem::path assetDirectory,IEventHttp& http);
}
