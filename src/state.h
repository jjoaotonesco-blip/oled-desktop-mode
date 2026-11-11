#pragma once
#include <array>
#include <map>
#include <string>
#include <stdexcept>
#include <functional>

namespace oled {
// Order is part of the journal format: brightness, contrast, saturation, hue, kelvin.
using Color = std::array<int, 5>;
inline constexpr Color preset{-17, 115, 122, 0, 6400};
struct Record {
    Color normal{};
    unsigned mask = 0;
    bool pending = false;
    Color lastOled = preset;
};
using Records = std::map<std::string, Record>;
inline bool same(const Color& a, const Color& b, unsigned mask) {
    for (unsigned i = 0; i < 5; ++i)
        if ((mask & (1u << i)) && a[i] != b[i]) return false;
    return true;
}
struct Backend {
    virtual ~Backend() = default;
    virtual Color read(const std::string&, unsigned) = 0;
    // Must skip unchanged properties and report failure on any setter/readback failure.
    virtual bool write(const std::string&, const Color&, unsigned) = 0;
};
class Controller {
    Backend& backend;
    std::function<void(const Records&)> persist;
public:
    Records records;
    std::map<std::string, Color> profiles;
    Color target(const std::string& id) const {
        auto it=profiles.find(id); return it==profiles.end()?preset:it->second;
    }
    Controller(Backend& b, std::function<void(const Records&)> save)
        : backend(b), persist(std::move(save)) {}
    void capture(const std::string& id, unsigned mask) {
        if (records.count(id)) return; // Never capture over an unresolved recovery.
        auto current = backend.read(id, mask);
        records.emplace(id, Record{current, mask, false});
        persist(records);
    }
    void observeNormal(const std::string& id) {
        auto& r = records.at(id);
        if (r.pending) return;
        auto current = backend.read(id, r.mask);
        if (same(current, preset, r.mask) || same(current, target(id), r.mask) ||
            same(current, r.lastOled, r.mask) || same(current, r.normal, r.mask)) return;
        auto previous = r.normal;
        r.normal = current;
        try { persist(records); } catch (...) { r.normal = previous; throw; }
    }
    bool restore(const std::string& id) {
        auto& r = records.at(id);
        if (!r.pending) return true;
        if (!backend.write(id, r.normal, r.mask)) return false;
        r.pending = false;
        try { persist(records); } catch (...) { r.pending = true; throw; }
        return true;
    }
    bool enter(const std::string& id) {
        auto& r = records.at(id);
        if (r.pending) return true;
        observeNormal(id);
        r.lastOled=target(id);
        r.pending = true;
        // Write-ahead: no AMD write is allowed before the durable backup.
        try { persist(records); } catch (...) { r.pending = false; throw; }
        if (backend.write(id, r.lastOled, r.mask)) return true;
        restore(id); // Roll back partially applied setters; retain journal on failure.
        return false;
    }
};
}
