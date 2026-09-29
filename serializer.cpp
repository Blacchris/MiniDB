#include <fstream>
#include <cstdint>
#include <iostream>
#include <cstdint>
#include "Hashmap/CustomHashMap.h"

using namespace std;


void serializeToFile(const Hashmap<int>&map, const string& path) {
    

    //ofstream is an output file stream object used to write data to a file on disk
    // path tells which file to open to write/read
    // ios::binary: the file is open in binary mode, meanining your bytes are written/read as binary data
    // rather than text-mode processing
    ofstream out(path, ios::binary);

            
    if (!out.is_open()) {
        throw runtime_error("file creation was not successful with this path " + path);
    };

    // Header: total entry count, uint32_t per the locked format.
    uint32_t num_entries = static_cast<uint32_t>(map.size());
    out.write(
        reinterpret_cast<const char*>(&num_entries),
        sizeof(num_entries)
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
            key_len
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


Hashmap<int> deserializeFromFile(const string& path) {
   
    ifstream in(path, ios::binary);
    if (!in.is_open()) {
        throw runtime_error("failed to open: " + path);
    }
    uint32_t num_of_entries;

    in.read(
        reinterpret_cast<char*>(&num_of_entries), 
        sizeof(num_of_entries)
    );

    Hashmap<int> map;
    for (uint32_t i = 0; i < num_of_entries; ++i) {
        uint32_t key_len = 0;
        
        in.read(
            reinterpret_cast<char*>(&key_len),
            sizeof(key_len)
        );

        string key(key_len, '\0');
        in.read(key.data(), key_len);

        int val = 0;
        in.read(reinterpret_cast<char*>(&val), sizeof(val));

        map.put(key, val);
    }

    return map;

}

int main() {

    Hashmap<int> map;
    map.put("chris", 34);
    map.put("randy", 524);
    map.put("Michael", 24);
    map.put("Theo", 29);
    map.put("Randy", 31);


    const string path = "/home/grand_marshal/CPP/hexfile.txt";
    serializeToFile(map, path);


    if (deserializeFromFile(path).containsKey("Randy")) {
        cout << "true" << endl;
    }

    cout << "Value: " << deserializeFromFile(path).get("Theo") << endl;

    // std::ifstream in("DBfile.txt", std::ios::binary);

    // unsigned char buffer[4];

    // in.read(reinterpret_cast<char*>(buffer), 4);

    // std::cout << "bytes read: " << in.gcount() << '\n';

    // for (int i = 0; i < 4; ++i) {
    //     std::cout << std::hex
    //             << static_cast<int>(buffer[i]) << ' ';
    // }
    

    return 0;
}