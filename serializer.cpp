#include <fstream>
#include <cstdint>
#include <iostream>
#include <cstdint>
#include "Hashmap/CustomHashMap.h"

using namespace std;


void serializeToFile(const Hashmap<int>&map, const string& path) {
    
    ofstream out(path, ios::binary);
    if (!out.is_open()) {
        throw runtime_error("file creation was not successful with this path " + path);
    };

    // Header: total entry count, uint32_t per the locked format.
    uint32_t num_entries = static_cast<uint32_t>(map.size());
    out.write(
        reinterpret_cast<const char*>(&num_entries),
        sizeof(uint32_t)
    );

    map.forEach([&out](const string& key, const int& val){
        // key_len prefix
        uint32_t key_len = static_cast<uint32_t>(key.size());
        out.write(
            reinterpret_cast<const char*>(&key_len),
            sizeof(key_len)
        );

        // raw key bytes — .data() gives the actual buffer, no iterator involved
        out.write(
            reinterpret_cast<const char*>(key.data()),
            sizeof(key.size())
        );

        // fixed-size value, no length prefix needed
        out.write(
            reinterpret_cast<const char*>(&val),
            sizeof(val)
        );
    });


    if (!out.good()) {
        throw runtime_error("serializeToFile: write failed for " + path);
    }

}

int main() {

    Hashmap<int> map;
    map.put("chris", 34);
    map.put("randy", 524);

    string path = "DBfile.txt";
    serializeToFile(map, path);



    return 0;
}