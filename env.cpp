#include "env.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace
{
    std::unordered_map<std::string, std::string> g_values;

    std::string Trim(const std::string& s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    std::string StripQuotes(const std::string& s)
    {
        if (s.size() >= 2)
        {
            char first = s.front();
            char last = s.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
                return s.substr(1, s.size() - 2);
        }
        return s;
    }
}

namespace dotenv
{
    void Load(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return;

        std::string line;
        while (std::getline(file, line))
        {
            std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == '#')
                continue;

            size_t eq = trimmed.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = Trim(trimmed.substr(0, eq));
            std::string value = StripQuotes(Trim(trimmed.substr(eq + 1)));

            if (!key.empty())
                g_values[key] = value;
        }
    }

    std::string Get(const std::string& key, const std::string& defaultValue)
    {
        if (const char* env = std::getenv(key.c_str()))
            return std::string(env);

        auto it = g_values.find(key);
        if (it != g_values.end())
            return it->second;

        return defaultValue;
    }
}
