#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>

using namespace std;

const int HASH_SIZE = 1 << 18; // 262144 slots
const int HASH_MASK = HASH_SIZE - 1;
const char* DB_FILE = "data.db";

struct HashSlot {
    uint8_t key_len;
    uint8_t padding[7];
    uint64_t value_list_offset;
    char key[64];
}; // 80 bytes

struct ValueNode {
    uint64_t next;
    int32_t value;
    uint8_t deleted;
    uint8_t padding[3];
}; // 16 bytes

FILE* db_file = nullptr;

uint64_t hash_str(const char* s, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void init_db() {
    db_file = fopen(DB_FILE, "r+b");
    if (db_file) {
        fseek(db_file, 0, SEEK_END);
        long size = ftell(db_file);
        if (size < (long)(HASH_SIZE * sizeof(HashSlot))) {
            fclose(db_file);
            db_file = nullptr;
        } else {
            fseek(db_file, 0, SEEK_SET);
            setvbuf(db_file, nullptr, _IOFBF, 1 << 20);
            return;
        }
    }
    
    db_file = fopen(DB_FILE, "w+b");
    if (!db_file) {
        perror("Failed to create database file");
        exit(1);
    }
    setvbuf(db_file, nullptr, _IOFBF, 1 << 20);
    
    HashSlot empty_slot;
    memset(&empty_slot, 0, sizeof(HashSlot));
    for (int i = 0; i < HASH_SIZE; ++i) {
        fwrite(&empty_slot, sizeof(HashSlot), 1, db_file);
    }
    fflush(db_file);
}

void close_db() {
    if (db_file) {
        fflush(db_file);
        fclose(db_file);
    }
}

int find_slot(const string& key, bool create) {
    uint64_t h = hash_str(key.c_str(), key.size());
    int idx = h & HASH_MASK;
    for (int i = 0; i < HASH_SIZE; ++i) {
        int curr = (idx + i) & HASH_MASK;
        HashSlot slot;
        fseek(db_file, curr * sizeof(HashSlot), SEEK_SET);
        fread(&slot, sizeof(HashSlot), 1, db_file);
        
        if (slot.key_len == 0) {
            if (create) {
                slot.key_len = key.size();
                slot.value_list_offset = 0;
                memset(slot.key, 0, 64);
                memcpy(slot.key, key.c_str(), key.size());
                fseek(db_file, curr * sizeof(HashSlot), SEEK_SET);
                fwrite(&slot, sizeof(HashSlot), 1, db_file);
                return curr;
            } else {
                return -1;
            }
        } else if (slot.key_len == key.size() && memcmp(slot.key, key.c_str(), key.size()) == 0) {
            return curr;
        }
    }
    return -1;
}

void insert_record(const string& key, int32_t value) {
    int idx = find_slot(key, true);
    if (idx == -1) return;
    
    HashSlot slot;
    fseek(db_file, idx * sizeof(HashSlot), SEEK_SET);
    fread(&slot, sizeof(HashSlot), 1, db_file);
    
    ValueNode node;
    node.next = slot.value_list_offset;
    node.value = value;
    node.deleted = 0;
    memset(node.padding, 0, 3);
    
    fseek(db_file, 0, SEEK_END);
    uint64_t offset = ftell(db_file);
    fwrite(&node, sizeof(ValueNode), 1, db_file);
    
    slot.value_list_offset = offset;
    fseek(db_file, idx * sizeof(HashSlot), SEEK_SET);
    fwrite(&slot, sizeof(HashSlot), 1, db_file);
}

void delete_record(const string& key, int32_t value) {
    int idx = find_slot(key, false);
    if (idx == -1) return;
    
    HashSlot slot;
    fseek(db_file, idx * sizeof(HashSlot), SEEK_SET);
    fread(&slot, sizeof(HashSlot), 1, db_file);
    
    uint64_t offset = slot.value_list_offset;
    while (offset != 0) {
        ValueNode node;
        fseek(db_file, offset, SEEK_SET);
        fread(&node, sizeof(ValueNode), 1, db_file);
        
        if (!node.deleted && node.value == value) {
            node.deleted = 1;
            fseek(db_file, offset, SEEK_SET);
            fwrite(&node, sizeof(ValueNode), 1, db_file);
            break;
        }
        offset = node.next;
    }
}

void find_record(const string& key) {
    int idx = find_slot(key, false);
    if (idx == -1) {
        printf("null\n");
        return;
    }
    
    HashSlot slot;
    fseek(db_file, idx * sizeof(HashSlot), SEEK_SET);
    fread(&slot, sizeof(HashSlot), 1, db_file);
    
    vector<int32_t> values;
    uint64_t offset = slot.value_list_offset;
    while (offset != 0) {
        ValueNode node;
        fseek(db_file, offset, SEEK_SET);
        fread(&node, sizeof(ValueNode), 1, db_file);
        
        if (!node.deleted) {
            values.push_back(node.value);
        }
        offset = node.next;
    }
    
    if (values.empty()) {
        printf("null\n");
    } else {
        sort(values.begin(), values.end());
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) printf(" ");
            printf("%d", values[i]);
        }
        printf("\n");
    }
}

int main() {
    setvbuf(stdout, nullptr, _IOFBF, 1 << 20);
    init_db();
    
    int n;
    if (scanf("%d", &n) != 1) {
        close_db();
        return 0;
    }
    
    char op[16];
    char key[128];
    int value;
    
    for (int i = 0; i < n; ++i) {
        scanf("%s", op);
        if (strcmp(op, "insert") == 0) {
            scanf("%s %d", key, &value);
            insert_record(key, value);
        } else if (strcmp(op, "delete") == 0) {
            scanf("%s %d", key, &value);
            delete_record(key, value);
        } else if (strcmp(op, "find") == 0) {
            scanf("%s", key);
            find_record(key);
        }
    }
    
    close_db();
    fflush(stdout);
    return 0;
}
