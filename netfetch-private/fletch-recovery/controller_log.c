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

int main(int argc, char** argv) {
    RocksdbWrapper::prepare_rocksdb();
    parse_ini("config.ini");
    parse_control_ini("control_type.ini");
    if (argc != 2) {
        printf("Usage: ./controller_log is_historical\n");
        exit(-1);
    }

    bool is_historical = atoi(argv[1]);
    RocksdbWrapper db_wrapper(NOCACHE_ID);

    std::vector<std::string> paths = {"/a", "/a/b", "/a/b/c"};
    std::vector<uint8_t> tokens = {1, 1, 1};
    std::vector<uint64_t> hashes = {0x1111111111111111ULL,
                                    0x2222222222222222ULL,
                                    0x3333333333333333ULL};

    db_wrapper.open_path_map(is_historical); // 0: active; 1: historical    
    // // ========== put phase ==========
    // printf("=== put phase ===\n");
    // for (size_t i = 0; i < paths.size(); i++) {
    //     db_wrapper.put_path_token(paths[i], tokens[i]);
    //     printf("  put key: %s token: %d\n", paths[i].c_str(), tokens[i]);
    // }

    // // ========== get all phase (after put) ==========
    // printf("=== get all phase (after put) ===\n");
    // {
    //     std::vector<std::pair<std::string, uint8_t>> all;
    //     db_wrapper.get_all_path_tokens(all);
    //     printf("  total entries: %zu\n", all.size());
    //     for (auto& kv : all) {
    //         printf("    %s -> token=%d\n", kv.first.c_str(), (int)kv.second);
    //     }
    // }

    // // ========== delete phase ==========
    // // delete /a/b to simulate cache eviction
    // printf("=== delete phase ===\n");
    // {
    //     if (!is_historical){
    //         std::string victim = "/a/b";
    //         db_wrapper.delete_path_token(victim);
    //         printf("  delete key: %s\n", victim.c_str());
    //     }
    // }

    // ========== get all phase (after delete) ==========
    printf("=== get all phase (after delete) ===\n");
    {
        std::vector<std::pair<std::string, uint8_t>> all;
        db_wrapper.get_all_path_tokens(all);
        printf("  total entries: %zu\n", all.size());
        for (auto& kv : all) {
            printf("    %s -> token=%d\n", kv.first.c_str(), (int)kv.second);
        }
    }

    db_wrapper.open_hash_map(is_historical);  // 0: active; 1: historical
    // // ========== put phase ==========
    // printf("=== put phase (hash-token) ===\n");
    // for (size_t i = 0; i < hashes.size(); i++) {
    //     db_wrapper.put_hash_token(hashes[i], tokens[i]);
    //     printf("  put hash: %016lx token: %d\n", hashes[i], tokens[i]);
    // }

    // // ========== get all phase (after put) ==========
    // printf("=== get all phase (after put) ===\n");
    // {
    //     std::vector<std::pair<uint64_t, uint8_t>> all;
    //     db_wrapper.get_all_hash_tokens(all);
    //     printf("  total entries: %zu\n", all.size());
    //     for (auto& kv : all) {
    //         printf("    hash=%016lx -> token=%d\n", kv.first, (int)kv.second);
    //     }
    // }

    // // ========== delete phase ==========
    // // delete (hashes[1], tokens[1]) to simulate cache eviction for /a/b
    // printf("=== delete phase ===\n");
    // {
    //     if (!is_historical) {
    //         uint64_t victim_hash = hashes[1];
    //         uint8_t victim_token = tokens[1];
    //         db_wrapper.delete_hash_token(victim_hash, victim_token);
    //         printf("  delete hash: %016lx token: %d\n", victim_hash,
    //                (int)victim_token);
    //     }
    // }

    // ========== get all phase (after delete) ==========
    printf("=== get all phase (after delete) ===\n");
    {
        std::vector<std::pair<uint64_t, uint8_t>> all;
        db_wrapper.get_all_hash_tokens(all);
        printf("  total entries: %zu\n", all.size());
        for (auto& kv : all) {
            printf("    hash=%016lx -> token=%d\n", kv.first, (int)kv.second);
        }
    }

    return 0;
}