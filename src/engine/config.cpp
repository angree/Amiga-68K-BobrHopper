#include "engine/config.h"
#include "engine/strings.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cr {

#ifdef CR_PLATFORM_SF2000
Config::SaveStrategy Config::strategy = Config::SaveStrategy::Direct;
#else
Config::SaveStrategy Config::strategy = Config::SaveStrategy::Rename;
#endif
void (*Config::syncFile)(const char *path) = nullptr;

namespace {

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void makeParentDir(const std::string &path)
{
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos || slash == 0) return;
    std::string dir = path.substr(0, slash);
#ifdef _WIN32
    _mkdir(dir.c_str());
#else
    mkdir(dir.c_str(), 0755);
#endif
}

} // namespace

bool Config::loadFile(const std::string &path)
{
    values_.clear();
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[512];
    while (std::fgets(buf, sizeof buf, f)) {
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        if (!key.empty()) values_[key] = trim(line.substr(eq + 1));
    }
    std::fclose(f);
    return true;
}

bool Config::load(const std::string &path)
{
    const bool opened = loadFile(path);
    if (opened && !values_.empty()) return true;
    // the backup copy: always present with Direct, left behind by an interrupted save with Rename
    Config backup;
    if (backup.loadFile(path + ".tmp") && !backup.values_.empty()) {
        values_ = backup.values_;
        return true;
    }
    values_.clear();
    return opened; // an existing file without settings (only comments) is still a loaded file
}

bool Config::writeFile(const std::string &path) const
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = true;
    for (const auto &kv : values_)
        if (std::fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str()) < 0) ok = false;
    if (std::fflush(f) != 0) ok = false;
#if defined(_WIN32)
    if (_commit(_fileno(f)) != 0) ok = false;
#elif !defined(CR_PLATFORM_SF2000)
    if (fsync(fileno(f)) != 0) ok = false;
#endif
    if (std::fclose(f) != 0) ok = false;
    if (ok && syncFile) syncFile(path.c_str());
    return ok;
}

bool Config::save(const std::string &path) const
{
    makeParentDir(path);
    const std::string tmp = path + ".tmp";
    if (strategy == SaveStrategy::Direct) {
        // no rename on the SF2000: a complete backup first, then the file itself
        if (!writeFile(tmp)) return false;
        return writeFile(path);
    }
    if (!writeFile(tmp)) {
        std::remove(tmp.c_str());
        return false;
    }
#ifdef _WIN32
    std::remove(path.c_str()); // rename does not replace an existing file on Windows
#endif
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

int Config::getInt(const std::string &key, int fallback) const
{
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return fallback;
    char *end = nullptr;
    long v = std::strtol(it->second.c_str(), &end, 10);
    return (end && *end == '\0') ? int(v) : fallback;
}

std::string Config::get(const std::string &key, const std::string &fallback) const
{
    auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}

void Config::setInt(const std::string &key, int value) { values_[key] = toString(value); }

void Config::set(const std::string &key, const std::string &value) { values_[key] = trim(value); }

} // namespace cr
