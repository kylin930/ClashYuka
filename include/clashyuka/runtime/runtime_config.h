#pragma once

#include <string>

#include "clashyuka/core/result.h"

namespace clashyuka::runtime {

// Minimal, P2.2-only runtime configuration. It is written to the application
// data directory and always binds the controller and proxy to loopback.
struct RuntimeConfigDefinition {
    std::string host = "127.0.0.1";
    int port = 9090;
    int mixedPort = 7897;
    std::string secret;
    std::string mode = "rule";
    std::string logLevel = "info";
};

// Values actually used by the kernel after loading/creating the file.
struct RuntimeConfigInfo {
    std::string host = "127.0.0.1";
    int port = 9090;
    int mixedPort = 7897;
    std::string secret;
    bool generated = false;
};

core::Result<void> ValidateRuntimeConfigDefinition(const RuntimeConfigDefinition& definition);

// Renders a complete YAML document with the ClashYuka marker header.
std::string RenderRuntimeConfig(const RuntimeConfigDefinition& definition);

// Ensures a usable runtime config exists at path.
// - Missing file: generates the app-owned file.
// - App-owned file (marker present) with different settings: regenerated.
// - User-provided file (no marker): validated and adopted, never overwritten.
core::Result<RuntimeConfigInfo> EnsureRuntimeConfig(const std::string& path,
                                                    const RuntimeConfigDefinition& definition);

}  // namespace clashyuka::runtime
