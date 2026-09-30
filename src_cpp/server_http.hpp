#pragma once

#include "config.hpp"
#include "db.hpp"

#include "httplib.h"

namespace kos {

void register_http_routes(httplib::Server& svr, Store& store, Config& cfg);

}  // namespace kos
