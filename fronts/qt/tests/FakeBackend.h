#pragma once

#include <QStringList>

namespace Tests {

// A backend for tests to talk to: this same test binary, started with
// kFakeBackendArgument, answering JSON-RPC over NDJSON on stdin/stdout
// (docs/protocol.md §2) — so RpcClient and SourceManager are exercised
// through their real transport and a real process, with no test-only
// seams in production code. What it answers is in FakeBackend.cpp.
inline constexpr char kFakeBackendArgument[] = "--fake-backend";

// The argv for a Rpc::BackendManifest that starts the fake backend.
QStringList fakeBackendArgv();

// Installs the fake backend as the only backend Rpc::discoverManifests()
// finds — into the test-mode config location (QStandardPaths test mode is
// on for the whole run, see main.cpp), with dev-mode discovery off.
void installFakeBackendManifest();

// The fake backend's main loop: reads requests until stdin closes or a
// `shutdown` request, and returns the process's exit code.
int runFakeBackend();

} // namespace Tests
