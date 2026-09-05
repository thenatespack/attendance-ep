#pragma once

#include <string>

// Minimal .env loader. Real process environment variables always take
// precedence over values loaded from the .env file.
namespace dotenv
{
    // Loads KEY=VALUE pairs from the given file into an in-memory table.
    // Safe to call if the file doesn't exist (no-op). Lines starting with
    // '#' and blank lines are ignored. Values may be wrapped in matching
    // single or double quotes.
    void Load(const std::string& path = ".env");

    // Returns getenv(key) if set, otherwise the value loaded from .env,
    // otherwise defaultValue.
    std::string Get(const std::string& key, const std::string& defaultValue = "");
}
