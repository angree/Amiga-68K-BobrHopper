// conf/crossy.cfg: plain "key=value" lines (the port's AsyncStorage). Unknown keys are kept on save so an
// older binary never drops settings written by a newer one.
#pragma once

#include <map>
#include <string>

namespace cr {

class Config {
public:
    // How save() reaches the card. Rename (PC, R36S): <path>.tmp is written, flushed and renamed over <path>.
    // Direct (SF2000, whose firmware has no rename/unlink): <path>.tmp is written first as a full backup copy, then
    // <path> itself; both are flushed through syncFile. load() falls back to the backup when <path> is missing or
    // holds no setting (power lost while it was being rewritten).
    enum class SaveStrategy { Rename, Direct };
    static SaveStrategy strategy;
    // called after each file is written and closed (SF2000: fs_sync); may be null
    static void (*syncFile)(const char *path);

    // missing or unreadable file (and no usable backup) = empty config (first start)
    bool load(const std::string &path);
    // creates the directory when needed
    bool save(const std::string &path) const;

    int getInt(const std::string &key, int fallback) const;
    std::string get(const std::string &key, const std::string &fallback) const;
    void setInt(const std::string &key, int value);
    void set(const std::string &key, const std::string &value);

private:
    bool loadFile(const std::string &path);
    bool writeFile(const std::string &path) const;

    std::map<std::string, std::string> values_;
};

} // namespace cr
