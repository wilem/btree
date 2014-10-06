#include <sys/types.h>

#define container_of(ptr, type, member) ({                          \
            const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
            (type *)( (char *)__mptr - offsetof(type,member) );})

#define SZ_1K 0x400UL
#define SZ_4K 0x1000UL
#define SZ_8K 0x2000UL
#define SZ_4G 0x100000000UL

typedef u_int32_t u32;
typedef u_int64_t u64;

/*
  (1st 4K-byte region)
  header: 4-byte, 0xd0d0baba
  version: 4-byte, version of index file.
  length: 4-byte, length of index file.
  check_sum: 4-byte, CRC32 for bitmap.

  node count: 4-byte
  max node count: 4-byte (1M, ~10^6 nodes)
  file count: 4-byte 
  max file count: 4-byte (100M-200M, ~10^8 files)

  total file size: 8-byte
  max total file size: 8-byte (4T, 2^42 bytes)

  root node index num: 4-byte (defaul=0).
  
  (2nd 4K-byte region)
  node bitmap: (offset=4K) 1024-bytes, 1-bit per node, up to 8*10^3 nodes.

  (3rd region: 4G-byte)
  node array: (offset=8K) max node array size: 4G bytes.
  see "layout of node"
  offset is 1M-byte, align to 4K-byte boundry.
*/
struct index_header {       // aligned to 8-byte
    u32 header;             // 0xd0d0baba
    u32 version;            // 1

    u64 length;             // 4K+128K+4G bytes, length of index file.
    u64 check_sum;          // of index header.

    u32 node_count;         // new node count.
    u32 max_node_count;     // 1 * 1000 * 1000 nodes.

    u32 file_count;         // file saved.
    u32 max_file_count;     // 1 * 10000 * 10000 files.

    u64 total_file_size;    // all files size.
    u64 max_total_file_size;// 4*1024*1024*1024*1024, 4T.

    u32 root_node_index;    // default = 0.
    u32 __padding;

    index_header()
    {
        header = 0xd0d0baba;
        version = 1;
        length = SZ_4K + 128*SZ_1K + SZ_4G;
        check_sum = 0;
        node_count = 0;
        max_node_count = 1000*1000; // 1024 * 1024
        file_count = 0;
        max_file_count = 10000*10000;
        total_file_size = 0;
        max_total_file_size = SZ_4G*SZ_1K; // 4T
        root_node_index = 0;
    }
};

//TODO u32 overflow???
class disk_map {
public:
    struct inode {
        u32 length;
        u32 index; // in the node array.
        char payload[0];
    };

    // index_header offset: 0
    static const char *index_header_file_name;
    static const char *index_inode_file_name;
    static const u32 index_bitmap_offset;
    static const u32 index_inode_array_offset;
    static const u32 map_len_hdr;
    static const u64 map_len_ino;
    
    //int fd; // index file fd for header, bitmap and inodes.
    int fd_hdr, fd_idx;
    struct index_header *hdr;
    void *mem_hdr; // mapping memory addr for head.
    void *mem_map; // mapping memory addr for bitmap.
    inode *ino_arr[2];

    disk_map();

    inode *payload2inode(void *pay);
    // get node index of payload.
    u32 payload2index(void *pay);

    void *read_root_node();
    
    // alloc 4K page.
    inode *allocate_inode();
    void  *allocate();

    int dealloc_inode(inode *addr);
    int dealloc(void *x);

    // relative addr to real address.
    inode *get_inode(u32 idx);
    void  *read(u32 idx);

    int save_inode(inode *ino);
    int save(void *x);

    ~disk_map();
};

