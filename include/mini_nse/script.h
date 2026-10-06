#pragma once

#include <istream>
#include <string>
#include <vector>

#include "mini_nse/events.h"

namespace mini_nse {

// A morning read from a text file. One command per line; '#' starts a comment. Prices are in rupees.
//
//   PREV_CLOSE 100.00             required, before any event
//   TICK 0.05                     optional: prices must be multiples of this
//   BAND 90.00 110.00             optional: allowed price range for limit orders
//   PHASE PREOPEN | LIMIT_ONLY | AUCTION | CONTINUOUS
//   ADD <id> BUY|SELL LIMIT <price> <qty>
//   ADD <id> BUY|SELL MARKET <qty>
//   CANCEL <id>
//   MODIFY <id> <price> <qty>     (price is ignored for market orders)
struct Script {
    SessionConfig config;
    std::vector<Event> events;
    std::vector<int> lines;  // the file line each event came from, for messages
};

struct ParseError {
    int line = 0;
    std::string message;
};

struct ParsedScript {
    Script script;
    std::vector<ParseError> errors;  // empty means the whole file was understood
};

ParsedScript parse_script(std::istream& in);

}  // namespace mini_nse
