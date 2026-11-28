#pragma once

#include <stdexcept>
#include <string>

namespace Rpc {

// Thrown by generated/Models.h's fromJson() methods (see
// protocol/codegen/cpprender.py) when a message doesn't match
// protocol/schema/*.yaml — a required field is missing, or a field's
// runtime JSON type/enum value doesn't match. Carries a field path (e.g.
// "Track.durationMs") and the mismatch in its message. Hand-written, not
// generated: every generated struct just throws this one shared type.
class ProtocolParseError : public std::runtime_error {
public:
    explicit ProtocolParseError(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

} // namespace Rpc
