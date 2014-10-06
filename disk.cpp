#include <iostream>
#include <iomanip>
#include <sys/mman.h> // mmap
#include <sys/types.h>
#include <fcntl.h>
#include <assert.h>
#include <cstring>

// on-disk index header, node
#include "disk.hpp"

using std::cin;
using std::cout;
using std::cerr;
using std::endl;

const char* disk_map::index_header_file_name = (char *)"hdr.bin";
const char* disk_map::index_inode_file_name = (char *)"idx.bin";
const u32   disk_map::index_bitmap_offset = SZ_4K;
const u32   disk_map::index_inode_array_offset = SZ_4K + 128*SZ_1K;
const u32   disk_map::map_len_hdr = SZ_4K + 128*SZ_1K;
const u64   disk_map::map_len_ino = SZ_4G;

disk_map::disk_map()
{
    cout << "size of u32: " << std::dec << sizeof(u32) << endl;
    cout << "size of u64: " << sizeof(u64) << endl;
        
    cout << "idx bitmap     : " << std::hex << index_bitmap_offset << endl
         << "idx node array : " << index_inode_array_offset << endl
         << "mmap length hdr: " << map_len_hdr << endl
         << "mmap length ino: " << map_len_ino << endl;

    fd_hdr = open(index_header_file_name, O_RDWR);
    if (fd_hdr == -1) {
        cerr << "fail to open: " << index_header_file_name << endl;
        throw -1;
    }
    cout << "fd_hdr: " << fd_hdr << endl;

    fd_idx = open(index_inode_file_name, O_RDWR);
    if (fd_idx == -1) {
        cerr << "fail to open: " << index_inode_file_name << endl;
        throw -2;
    }
    cout << "fd_idx: " << fd_idx << endl;

    cout << "map_len for headr: " << map_len_hdr << endl;
    cout << "map_len for inode: " << map_len_ino << endl;
    // MAP_PRIVATE: not across process, will not write to file.
    // MAP_SHARED : will write to file.
    mem_hdr = mmap(NULL, map_len_hdr, PROT_READ | PROT_WRITE,
            MAP_SHARED, fd_hdr, 0/*offset*/);
    mem_map = (void *)((char *)mem_hdr + index_bitmap_offset);

    u32 map_len_ino_1 = map_len_ino >> 1;
    cout << "map_len_ino_1: 0x" << std::hex << map_len_ino_1 << endl;
    ino_arr[0] = (inode *)mmap(NULL, map_len_ino_1, PROT_READ | PROT_WRITE,
            MAP_SHARED, fd_idx, 0);
    ino_arr[1] = (inode *)mmap(NULL, map_len_ino_1, PROT_READ | PROT_WRITE,
            MAP_SHARED, fd_idx, map_len_ino_1);

    if (mem_hdr == MAP_FAILED ||
            ino_arr[0] == MAP_FAILED ||
            ino_arr[1] == MAP_FAILED) {
        cerr << "mmap failed! " << endl
             << "hdr : " << mem_hdr << endl
             << "ino1: " << ino_arr[0] << endl
             << "ino2: " << ino_arr[1] << endl;
    } else {
        // index header init.
        uint *magic = (uint *)mem_hdr;
        if (*magic != 0xd0d0baba) { // new header.
            // new an object on pre-allocated memory.
            hdr = new (mem_hdr) index_header();
            assert(hdr->max_node_count > 0);
			cerr << "disk_map(): " << hdr->length << endl;
            //XXX allocate 1st page for root node.
            u32 *map = (u32 *) mem_map; 
            *map = 0x1; // 0 inodes.
            cout << "NEW disk map created!" << endl;
        } else {
            // already init mem region.
            hdr = (struct index_header *) mem_hdr;
            cout << "OLD disk map loaded!" << endl;
        }
        cout << "mmap       @ " << std::hex << hdr << endl
             << "hdr_len:     " << hdr->length << endl
             << "map_len_hdr: " << map_len_hdr << endl
             << "map_len_ino: " << map_len_ino << endl
             << "header:      " << hdr->header << endl;
    }

    cout << "max node count: " << hdr->max_node_count << endl;
}

//XXX
disk_map::inode *
disk_map::allocate_inode()
{
    static u32 last_idx = 0xdeadbeef;
    // scan bitmap for free slot.
    u32 i, j, found = 0, idx, map, *bitmap;
    // max inode count: 1M.
    u32 n_32 = SZ_1K * SZ_1K / 32; //1M-bit/32-bit=32K (128K-byte)

    //assert(n_32 == 32 * SZ_1K);

    assert(hdr->header == 0xd0d0baba);
    if (hdr->node_count >= hdr->max_node_count) {
        cerr << "hdr:     node_count = " << hdr->node_count << endl
             << "hdr: max_node_count = " << hdr->max_node_count << endl
             << "disk_map::allocate_inode(): run out of inode." << endl;
        return NULL;
    }

    bitmap = (u32 *)mem_map;
    for (i = 0; i < n_32 && !found; i++) { //1M inodes, 128K bytes.
        if (bitmap[i] == 0xFFFFFFFFUL)
            continue;
        for (j = 0; j < 32; j++) { // 4-bytes
//cerr << std::hex << "i: 0x" << i << ", j: 0x" << j
//    << ", map: 0x" << bitmap[i] << endl;
            if (bitmap[i] & (1 << j))
                continue;
            found = 1;
            bitmap[i] |= (1 << j); // mark.
            idx = i * 32 + j;
            break;
        }
    }
    if (!found) {
        cerr << std::dec;
        cerr << "allocate_inode(): not found." << endl
            << "n_32: " << n_32
            << ", i:  " << i << endl
            << "hdr->node_count:     " << hdr->node_count << endl
            << "hdr->max_node_count: " << hdr->max_node_count << endl;
        return NULL;
    }
    // offset of node array.
    inode *ino = get_inode(idx);
//cerr << "HDR: " << hdr << endl
//     << "BIT: " << mem_map << endl
//     << "INO: " << ino << endl;
    hdr->node_count++;
    //XXX clear inode.
    memset(ino, 0, SZ_4K);
    ino->length = SZ_4K; //???
    ino->index = idx;

    //XXX sync to hdr.bin file
    //msync(mem_hdr, map_len_hdr, MS_ASYNC);

#if 0
cerr << "allocate_inode(): idx=" << std::setw(10) << idx
     << ", ino=" << std::setw(10) << ino
     << ", node_count=" << std::setw(10) << hdr->node_count
     << " "; // << endl;
#endif

    //XXX not linkely.
    if (last_idx == idx) {
        cerr << "disk_map::allocate_inode(): Duplicate inode index: "
             << idx << endl;
        return NULL;
    }
    last_idx = idx;

    return ino;
}

void *
disk_map::allocate()
{
    inode *ino = allocate_inode();
    if (ino)
        return ino->payload;
    return NULL;
}

int
disk_map::dealloc_inode(inode *ino)
{
    if ((size_t)ino % SZ_4K)
        return -1; // unaligned page addr, invalid.

    if (ino->index > SZ_1K * SZ_1K)
        return -2;

    inode *ino2 = get_inode(ino->index);
    if (ino2 != ino)
        return -3;

    u32 i, j, idx;
    idx = ino->index;
    i = idx / 32;
    j = idx % 32;
    u32 *bitmap = (u32 *)mem_map;
    bitmap[i] &= ~(1 << j);
    hdr->node_count--;

    ino->index = 0;
    ino->length = 0;

    msync(mem_hdr, map_len_hdr, MS_ASYNC);

    return 0;
}

disk_map::inode *
disk_map::payload2inode(void *pay)
{
     inode *ino = (inode *)((char *)pay - sizeof(inode));
//cerr << "payload2inode(): payload: " << (void *)pay << endl
//     << "inode:   " << ino << endl;
     if (ino->length != SZ_4K) {
         cerr << "payload2inode(): invalid inode." << endl;
         return NULL;
     }
     return ino;
}

//TODO define as disk_map::node_index_t
u32
disk_map::payload2index(void *pay)
{
    inode *ino = payload2inode(pay);
    if (ino)
        return ino->index;
    cerr << "payload2index(): invalid inode." << endl;
    return 0;
}

int
disk_map::dealloc(void *x)
{
     inode *ino = (inode *)((char *)x - sizeof(struct inode)); 
     if (ino->length != SZ_4K) {
         cout << "dealloc(): invalid inode." << endl;
         return -1;
     }
     return dealloc_inode(ino);
}

void *
disk_map::read_root_node()
{
    u32 idx = hdr->root_node_index;
    cout << __func__ << "(): idx:" << idx << endl;
    if (idx == 0)
        return NULL;
    inode *ino = get_inode(idx);
    if (ino == NULL)
        return NULL;
    return ino->payload;
}

// relative addr to real address.
// 1M: 2^20 inodes.
// [19..0]
disk_map::inode *
disk_map::get_inode(u32 idx)
{
    if (idx == 0) //XXX reserved.
        return NULL;
    if (idx & ~0xFFFFF) {
        cerr << "disk_map::get_inode(): index out of range." << endl;
        return NULL;
    }
    // idx: 0 .. 2^20,1M
    // 19  | 18 .. 0
    // seg | ofs
    u32 seg = (idx & (1 << 19)) >> 19; // mod 1/2M; 0/1
    u32 ofs = idx & 0xFFFFF; // ~(1 << 19)
    inode *ino = ino_arr[seg];
    ino = (inode *)((char *)ino + ofs * SZ_4K);
    return ino;
}

void *
disk_map::read(u32 idx)
{
    inode *ino = get_inode(idx);
    if (ino == NULL) {
        cerr << "disk_map::read(): invalid index: " << idx; // << endl;
        return NULL;
    }
    return ino->payload;
}

//XXX do nothing.
int
disk_map::save_inode(inode *addr)
{
    if (((size_t)addr) % SZ_4K)
        return -1; // unaligned addr.
    //XXX save all pages.
    //msync(mem_ino, map_len_ino, MS_ASYNC); // or MS_SYNC.
    //XXX may hang here.
    return 0;
}

int
disk_map::save(void *x)
{
//cerr << "+disk_map::save(): payload=" << x << endl;
    inode *ino = (inode *)((char *)x - sizeof(inode)) ;
//cerr << "ino=" << ino << endl;
    if (ino->length != SZ_4K) {
        cerr << "disk_map::save(): invalid inode." << endl;
        return -1;
    }
//cerr << "-disk_map::save():" << endl;
    return save_inode(ino);
} 

disk_map::~disk_map()
{
    // flush mem pages to disk file.
    munmap(mem_hdr, map_len_hdr);
    munmap(ino_arr[0], map_len_ino >> 1);
    munmap(ino_arr[1], map_len_ino >> 1);
}

