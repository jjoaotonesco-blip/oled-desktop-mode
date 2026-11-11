#include "state.h"
#include <iostream>
#include <vector>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while(0)
struct Fake : oled::Backend {
    std::map<std::string, oled::Color> values;
    std::vector<std::string> writes;
    bool fail = false;
    oled::Color read(const std::string& id, unsigned) override { return values.at(id); }
    bool write(const std::string& id, const oled::Color& v, unsigned mask) override {
        writes.push_back(id);
        if (fail) { values[id][0] = v[0]; return false; }
        for (unsigned i=0;i<5;++i) if(mask&(1u<<i)) values[id][i]=v[i];
        return true;
    }
};
int main() {
    try {
        Fake f;
        const oled::Color original{4, 109, 96, 3, 7100};
        f.values["A"]=original; f.values["B"]={2, 101, 103, 1, 6500};
        oled::Records disk;
        bool failSave=false;
        oled::Controller c(f,[&](const auto& records){
            if(failSave) throw std::runtime_error("disk full");
            disk=records;
        });
        c.capture("A",31); c.capture("B",3);
        CHECK(c.enter("A")); CHECK(disk.at("A").pending);
        c.observeNormal("A"); CHECK(c.records.at("A").normal==original);
        auto count=f.writes.size(); c.enter("A"); CHECK(f.writes.size()==count);
        CHECK(c.restore("A")); CHECK(f.values["A"]==original);
        f.values["A"][1]=112; c.observeNormal("A");
        CHECK(disk.at("A").normal[1]==112);
        f.values["A"]=oled::preset; c.observeNormal("A");
        CHECK(disk.at("A").normal[1]==112);
        c.enter("B"); CHECK(f.values["B"][2]==103); CHECK(c.restore("B"));
        failSave=true; count=f.writes.size();
        try { c.enter("A"); } catch(const std::exception&) {}
        CHECK(f.writes.size()==count); CHECK(!c.records.at("A").pending);
        failSave=false; f.fail=true; CHECK(!c.enter("A"));
        CHECK(disk.at("A").pending); CHECK(c.records.at("A").normal[1]==112);
        // Restart from journal while hardware contains a partially written preset.
        oled::Controller restarted(f,[&](const auto& r){disk=r;});
        restarted.records=disk; restarted.capture("A",31);
        CHECK(restarted.records.at("A").normal[1]==112);
        f.fail=false; CHECK(restarted.restore("A")); CHECK(f.values["A"][1]==112);
        CHECK(!disk.at("A").pending);
        Fake custom;
        custom.values["one"]=original; custom.values["two"]={1,102,97,2,6800};
        oled::Controller profiles(custom,[](const auto&){});
        profiles.capture("one",31); profiles.capture("two",31);
        profiles.profiles["one"]={-10,120,130,0,6300};
        profiles.profiles["two"]={-22,110,116,4,6700};
        CHECK(profiles.enter("one")); CHECK(profiles.enter("two"));
        CHECK(custom.values["one"]==profiles.target("one"));
        CHECK(custom.values["two"]==profiles.target("two"));
        profiles.observeNormal("one"); CHECK(profiles.records.at("one").normal==original);
        CHECK(profiles.restore("one")); CHECK(custom.values["one"]==original);
        custom.values["one"]=profiles.target("one"); profiles.observeNormal("one");
        CHECK(profiles.records.at("one").normal==original);
        profiles.profiles["one"]={-14,117,124,0,6200};
        profiles.observeNormal("one"); CHECK(profiles.records.at("one").normal==original);
        CHECK(profiles.restore("two"));
        std::cout << "PASS: capture, transition, duplicate, manual edit, OLED exclusion, partial support, disk failure, rollback, restart recovery\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
