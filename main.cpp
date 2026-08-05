#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

using namespace std;

// Fast I/O
char outbuf[1 << 20];
int out_pos = 0;
inline void flush_out() {
    fwrite(outbuf, 1, out_pos, stdout);
    out_pos = 0;
}
inline void write_char(char c) {
    if (out_pos == sizeof(outbuf)) flush_out();
    outbuf[out_pos++] = c;
}
inline void write_int(int x) {
    if (x < 0) { write_char('-'); x = -x; }
    char tmp[12];
    int len = 0;
    if (x == 0) tmp[len++] = '0';
    while (x) { tmp[len++] = '0' + x % 10; x /= 10; }
    while (len) write_char(tmp[--len]);
}

inline int readChar() {
    return getchar_unlocked();
}
inline bool readStr(char* s, int& len) {
    int c = readChar();
    while (c == ' ' || c == '\n' || c == '\r') c = readChar();
    if (c == EOF) return false;
    len = 0;
    while (c != ' ' && c != '\n' && c != '\r' && c != EOF) {
        s[len++] = c;
        c = readChar();
    }
    s[len] = 0;
    return true;
}
inline bool readInt(int& x) {
    int c = readChar();
    while (c == ' ' || c == '\n' || c == '\r') c = readChar();
    if (c == EOF) return false;
    bool neg = false;
    if (c == '-') { neg = true; c = readChar(); }
    x = 0;
    while (c >= '0' && c <= '9') {
        x = x * 10 + (c - '0');
        c = readChar();
    }
    if (neg) x = -x;
    return true;
}

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

const uint64_t NODE_BASE = HASH_SIZE * sizeof(HashSlot);
const int MAX_NODES = 100005;
ValueNode mem_nodes[MAX_NODES];
int mem_node_count = 0;
int32_t find_values[MAX_NODES];

int fd = -1;

uint64_t hash_str(const char* s, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void init_db() {
    fd = open(DB_FILE, O_RDWR);
    if (fd != -1) {
        struct stat st;
        fstat(fd, &st);
        if (st.st_size < (off_t)(HASH_SIZE * sizeof(HashSlot))) {
            close(fd);
            fd = -1;
        } else {
            (void) posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM);
            mem_node_count = (st.st_size - NODE_BASE) / sizeof(ValueNode);
            if (mem_node_count > 0) {
                (void) pread(fd, mem_nodes, mem_node_count * sizeof(ValueNode), NODE_BASE);
            }
            return;
        }
    }
    
    fd = open(DB_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Failed to create database file");
        exit(1);
    }
    
    (void) ftruncate(fd, HASH_SIZE * sizeof(HashSlot));
    (void) posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM);
    mem_node_count = 0;
}

void close_db() {
    if (fd != -1) {
        close(fd);
    }
}

int find_slot(const char* key, int key_len, bool create) {
    uint64_t h = hash_str(key, key_len);
    int idx = h & HASH_MASK;
    for (int i = 0; i < HASH_SIZE; ++i) {
        int curr = (idx + i) & HASH_MASK;
        HashSlot slot;
        (void) pread(fd, &slot, sizeof(HashSlot), (off_t)curr * sizeof(HashSlot));
        
        if (slot.key_len == 0) {
            if (create) {
                slot.key_len = key_len;
                slot.value_list_offset = 0;
                memcpy(slot.key, key, key_len);
                (void) pwrite(fd, &slot, sizeof(HashSlot), (off_t)curr * sizeof(HashSlot));
                return curr;
            } else {
                return -1;
            }
        } else if (slot.key_len == key_len && memcmp(slot.key, key, key_len) == 0) {
            return curr;
        }
    }
    return -1;
}

void insert_record(const char* key, int key_len, int32_t value) {
    int idx = find_slot(key, key_len, true);
    if (idx == -1) return;
    
    HashSlot slot;
    (void) pread(fd, &slot, sizeof(HashSlot), (off_t)idx * sizeof(HashSlot));
    
    ValueNode& node = mem_nodes[mem_node_count];
    node.next = slot.value_list_offset;
    node.value = value;
    node.deleted = 0;
    
    off_t offset = NODE_BASE + mem_node_count * sizeof(ValueNode);
    (void) pwrite(fd, &node, sizeof(ValueNode), offset);
    
    slot.value_list_offset = offset;
    (void) pwrite(fd, &slot, sizeof(HashSlot), (off_t)idx * sizeof(HashSlot));
    
    mem_node_count++;
}

void delete_record(const char* key, int key_len, int32_t value) {
    int idx = find_slot(key, key_len, false);
    if (idx == -1) return;
    
    HashSlot slot;
    (void) pread(fd, &slot, sizeof(HashSlot), (off_t)idx * sizeof(HashSlot));
    
    uint64_t offset = slot.value_list_offset;
    uint64_t prev_offset = 0;
    while (offset != 0) {
        int node_idx = (offset - NODE_BASE) / sizeof(ValueNode);
        ValueNode& node = mem_nodes[node_idx];
        
        if (node.value == value) {
            if (prev_offset == 0) {
                slot.value_list_offset = node.next;
                (void) pwrite(fd, &slot, sizeof(HashSlot), (off_t)idx * sizeof(HashSlot));
            } else {
                int prev_node_idx = (prev_offset - NODE_BASE) / sizeof(ValueNode);
                ValueNode& prev_node = mem_nodes[prev_node_idx];
                prev_node.next = node.next;
                (void) pwrite(fd, &prev_node, sizeof(ValueNode), prev_offset);
            }
            break;
        }
        prev_offset = offset;
        offset = node.next;
    }
}

void find_record(const char* key, int key_len) {
    int idx = find_slot(key, key_len, false);
    if (idx == -1) {
        write_char('n'); write_char('u'); write_char('l'); write_char('l'); write_char('\n');
        return;
    }
    
    HashSlot slot;
    (void) pread(fd, &slot, sizeof(HashSlot), (off_t)idx * sizeof(HashSlot));
    
    int values_cnt = 0;
    uint64_t offset = slot.value_list_offset;
    while (offset != 0) {
        int node_idx = (offset - NODE_BASE) / sizeof(ValueNode);
        ValueNode& node = mem_nodes[node_idx];
        find_values[values_cnt++] = node.value;
        offset = node.next;
    }
    
    if (values_cnt == 0) {
        write_char('n'); write_char('u'); write_char('l'); write_char('l'); write_char('\n');
    } else {
        sort(find_values, find_values + values_cnt);
        for (int i = 0; i < values_cnt; ++i) {
            if (i > 0) write_char(' ');
            write_int(find_values[i]);
        }
        write_char('\n');
    }
}

int main() {
    init_db();
    
    int n;
    if (!readInt(n)) {
        close_db();
        return 0;
    }
    
    char op[16];
    char key[128];
    int op_len;
    int key_len;
    int value;
    
    for (int i = 0; i < n; ++i) {
        readStr(op, op_len);
        if (strcmp(op, "insert") == 0) {
            readStr(key, key_len);
            readInt(value);
            insert_record(key, key_len, value);
        } else if (strcmp(op, "delete") == 0) {
            readStr(key, key_len);
            readInt(value);
            delete_record(key, key_len, value);
        } else if (strcmp(op, "find") == 0) {
            readStr(key, key_len);
            find_record(key, key_len);
        }
    }
    
    close_db();
    flush_out();
    return 0;
}
