#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include <spdlog/sinks/basic_file_sink.h>

#ifdef GetObject
#undef GetObject
#endif

namespace logger = SKSE::log;
using namespace std::literals;