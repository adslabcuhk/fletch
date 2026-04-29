
#include <openssl/md5.h>
#include <pthread.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#include "../common/dynamic_array.h"
#include "../common/helper.h"
#include "../common/key.h"
#include "../common/metadata.h"
#include "../common/pkt_ring_buffer.h"
#include "../common/rocksdb_wrapper.h"
#include "../common/snapshot_record.h"
#include "../common/val.h"
#include "common_impl.h"

std::unordered_set<std::string> internal_dirs_set;

std::string get_parent_directory(const std::string& file_path) {
    std::size_t pos = file_path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return "";
    }
    return file_path.substr(0, pos);
}

int main(int argc, char** argv) {
    RocksdbWrapper::prepare_rocksdb();
    parse_ini("config.ini");
    parse_control_ini("control_type.ini");
    if (argc != 3) {
        printf("Usage: ./load_local server_physical_idx path_file.out\n");
        exit(-1);
    }
    int server_physical_idx = atoi(argv[1]);
    INVARIANT(server_physical_idx >= 0);
    INVARIANT(server_physical_idx < server_physical_num);

    std::string file_out_path = std::string(argv[2]);

    RocksdbWrapper db_wrapper(NOCACHE_ID);
    RocksdbWrapper index_db_wrapper(NOCACHE_ID);
    printf("init db %d\n", NOCACHE_ID);
    db_wrapper.open(server_physical_idx);
    index_db_wrapper.open_index(server_physical_idx);
    // put 100 paths into db and read them back
    std::ifstream file_out(file_out_path);
    std::string line;

    // create a 24 bytes val, each byte is 0
    val_t dir_val;
    dir_val.val_length = 24;
    dir_val.val_data = new char[24];
    memset(dir_val.val_data, 0, 24);
    for (int i = 0; i < 24; i++) {
        dir_val.val_data[i] = i;
    }
    val_t file_val;
    file_val.val_length = 40;
    file_val.val_data = new char[40];
    memset(file_val.val_data, 0, 40);
    for (int i = 0; i < 40; i++) {
        file_val.val_data[i] = i;
    }

    std::vector<std::string> paths;
    int count = 0;
    while (std::getline(file_out, line)) {
        if (count++ % 1000000 == 0) {
            // break;
            // print progress
            printf("count: %dK\n", count / 1000);
        }
        paths.push_back(line);
        std::vector<std::string> levels = extractPathLevels(line, 2);
        // only need for dirs
        levels.pop_back();
        for (auto& level : levels) {
            if (internal_dirs_set.find(level) != internal_dirs_set.end()) {
                continue;
            }
            internal_dirs_set.insert(level);
        }
    }
    count = 0;
    // get start time
    // convert set_to_vector
    std::vector<std::string> internal_dirs(internal_dirs_set.begin(), internal_dirs_set.end());
    auto start = std::chrono::high_resolution_clock::now();
    sort(internal_dirs.begin(), internal_dirs.end());
    index_db_wrapper.put_dirs_index(internal_dirs);
    // get end time
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    printf("index Elapsed time: %f seconds\n", elapsed.count());
    std::vector<uint64_t> dir_ids;
    index_db_wrapper.get_dirs_index(internal_dirs, dir_ids);
    // generate a mapping
    std::unordered_map<std::string, uint64_t> dir_id_map;
    for (int i = 0; i < internal_dirs.size(); i++) {
        dir_id_map[internal_dirs[i]] = dir_ids[i];
    }

    start = std::chrono::high_resolution_clock::now();
    for (auto& dir : internal_dirs) {
        auto parent_dir = get_parent_directory(dir);
        auto it = dir_id_map.find(parent_dir);
        if (it == dir_id_map.end()) {
            // it is the root dir
            db_wrapper.put_path_with_id(uint64_to_hex((uint64_t)0), dir, dir_val);
        }else{
            db_wrapper.put_path_with_id(uint64_to_hex(it->second), dir, dir_val);
        }
        // db_wrapper.put_path_with_id(uint64_to_hex(dir_id_map[get_parent_directory(dir)]), dir, dir_val);
    }
    for (auto& path : paths) {
        if (count++ % 1000000 == 0) {
            // break;
            // print progress
            printf("count: %dK\n", count / 1000);
        }
        db_wrapper.put_path_with_id(uint64_to_hex(dir_id_map[get_parent_directory(path)]), path, file_val);
        // printf("put key: %s val %d\n", path.c_str(), file_val.val_length);
    }
    // end = std::chrono::high_resolution_clock::now();
    // elapsed = end - start;
    // printf("db Elapsed time: %f seconds\n", elapsed.count());
}
