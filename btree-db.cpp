/* *
 * On-disk/persistent B-Tree implementation.
 */
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <algorithm>
#include <ctime>
#include <cassert>

#include "bench.hpp"
#include "disk.hpp"

using namespace std;

#define DEBUG 1
#define PROFILE

template <class K, class V>
struct btree {
	// Key-Value pair.
	struct key_val {
		K k; // key
		V v; // value
	};

	struct node;

	/* node items:
	 * +----+---+----+----+----+------+---+-+
	 * |c[0]|...|c[i]|k[i]|v[i]|c[i+1]|INF|-|
	 * +----+---+----+----+----+------+---+-+
	 */
	struct item { // node entry.
        //TODO: limit ptr size to 32-bit, even in a 64-bit env.
        u32 i; // ptr(node index) to child node.
		union /*ANON*/ {
			key_val kv;
			struct /*ANON*/ {
				K k; // key
				V v; // value
			} /*ANON*/;
		} /*ANON*/;
		item() :i(0xDEADBEEFul) {}
	} __attribute__((packed, aligned(4))); // align to 4-byte.

#define MAX_NODE_SIZE SZ_4K
	struct node {
		bool leaf;     // is leaf node?
		int n;         // keys in node.
		item items[1]; // one more as last node ptr.
		node() :n(0) {}
	};

#define NODE_ITEM(x, i)	((x)->items[i])
#define NODE_PTR(x, i)	((x)->items[i].i)
#define NODE_KEY(x, i)	((x)->items[i].k)
#define NODE_VAL(x, i)	((x)->items[i].v)
#define NODE_KVP(x, i)	((x)->items[i].kv)

#define NODE_FIRST_ITEM(x) ((x)->items[0])
#define NODE_LAST_ITEM(x)  ((x)->items[(x)->n-1])
#define NODE_FIRST_PTR(x)  ((x)->items[0].i)
#define NODE_LAST_PTR(x)   ((x)->items[(x)->n].i)
#define NODE_FIRST_KVP(x)  ((x)->items[0].kv)
#define NODE_LAST_KVP(x)   ((x)->items[(x)->n-1].kv)

#define MIN_ITEMS (t - 1)
#define MAX_ITEMS (2*t - 1)

// get ptr/index of node
#define NODE2IDX(x) (disk->payload2index(x))

	// items:  [t-1, 2*t-1]
	// height: <= log(t,(n+1)/2)
	int t;
	node *root, *root_bak;
	int node_count;

    // disk map.
    struct disk_map *disk;
    int max_items_count; // calculated by page size and item size.

	// b-tree-create(T)
	// require O(1) disk operations and O(1) CPU time.
	btree() :
        root(NULL),
		last_error(0),
		node_count(0),
		split_cnt(0),
		erase_cnt(0),
		search_miss_cnt(0),
		fixup_cnt(0),
		concate_cnt(0),
		concate_leaf_cnt(0),
		concate_inter_cnt(0),
		rebalance_cnt(0),
		rebalance_leaf_cnt(0),
		rebalance_inter_cnt(0)
        {
            // disk file map.
            disk = new disk_map();
            uint n_item = (MAX_NODE_SIZE - sizeof(node)) / sizeof(item);
            cout << "key_val size: " << std::dec << sizeof(key_val) << endl;
            cout << "item size: " << std::dec << sizeof(item) << endl;
            cout << "max items in node: " << std::dec << n_item << endl;
            max_items_count = n_item;
            t = max_items_count / 2;
            cout << "       t = " << t << endl;
            cout << "max items: " << MAX_ITEMS << endl;
            cout << "min items: " << MIN_ITEMS << endl;

            init_root_node();
        }

    void init_root_node()
        {
            root = (node *)disk->read_root_node();
            //root_bak = (node *)disk->read(0);

            cout << "init_root_node(): root:" << root << endl;
            // new tree.
            if (root == NULL) {
                root = allocate_node();
                root->leaf = true;
                root->n = 0;
                disk->hdr->root_node_index = disk->payload2index(root);
                cerr << "NEW root node: " << root
                    << ", leaf:" << root->leaf
                    << ", n:" << root->n
                    << ", idx:" << disk->hdr->root_node_index << endl;
            }
            // check root node.
            assert(root != NULL);
            u32 idx = disk->hdr->root_node_index;
            if (idx == 0) {
                cerr << "init_root_node(): " << idx << endl;
                throw -2;
            } 
            cerr << "new root node index: " << idx << endl
                << "root node addr       : " << root << endl
                << "backup root node addr: " << root_bak << endl;
            //XXX chekc if root == root_backup.
        }
    
	// height of tree.
	// include root node.
	int height()
        {
            return 1 + height(root);
        }

	// height of subtree from node x.
	int height(node *x)
        {
            node *y = disk_read(NODE_FIRST_PTR(x));
            if (y->leaf)
                return 1;
            else
                return (1 + height(y));
        }

	size_t node_size;

	node *allocate_node()
        {
            node_count++;
            return (node *)disk->allocate();
        }

	void free_node(node *x)
        {
            node_count--;
            disk->dealloc(x);
        }

	void disk_write(node *x)
        {
            int res = disk->save(x);
            //cout << "disk write: node=" << x << endl;
            if (res)
                cout << "disk_write() failed: res = "
                     << res << endl;
        }
    
	// interact with fs.
	// mmap ?
	// disk to main memory.
	// x: addr in disk
	node *disk_read(u32 idx)
        {
            // relative addr x to real addr y.
            return (node *)disk->read(idx);
        }
    
    // set y as child node of x at ptr i.
    void set_child_node(node *x, int i, node *y)
        {
            NODE_PTR(x, i) = NODE2IDX(y);
        }

    void set_last_child(node *x, node *y)
        {
            assert(y != NULL);
            if (y)
                set_child_node(x, x->n, y);
        }

    node *get_child_node(node *x, int i)
        {
            u32 ptr = NODE_PTR(x, i);
            return disk->read(ptr);
        }

    node *get_last_child_node(node *x)
        {
            u32 ptr = NODE_LAST_PTR(x);
            return (node *)disk->read(ptr);
        }

#define BTREE_NO_ERROR          0
#define BTREE_OUT_OF_STORAGE    0x1
    int last_error;

	int split_cnt;
	// ITEM(i) >> ITEM(i+1)
    // return new node z
	node *split_child(node *x, const int i, node *y)
	{
		// split on full node y.
        //assert(x->ptr[i] == y);
		assert(y->n == MAX_ITEMS);
        assert(NODE_PTR(x, i) == disk->payload2index(y));

		split_cnt++;

		node *z = allocate_node();
        if (z == NULL) {
            cerr << __func__ << "(): allocate node failed." << endl;
            last_error = BTREE_OUT_OF_STORAGE;
            return NULL;
        }
		z->leaf = y->leaf;
		z->n = MIN_ITEMS;
		// move right half (t-1 nodes) to z.
		// k, v and child ptr.
		// [0,t-2],[t-1],[t,2t-2],{2t-1}
		// t-1,1,t-1
        // [0,t-1] <= [t,2t-1] 
		for (int j = 0; j < t; j++) // include the last ptr.
			NODE_ITEM(z, j) = NODE_ITEM(y, t + j);
		// shrink node y.
		y->n = MIN_ITEMS;
		// make room for median item(from last item of y) of y and z.
		for (int j = x->n+1; j > i; j--) // include last ptr of x.
			NODE_ITEM(x, j) = NODE_ITEM(x, j-1);
		// insert last item of y into x at index i.
		// [n,i+1],[i,n+1]
		// move y.key[t-1] up.
        // set kv of node i.
		NODE_KVP(x, i) = NODE_KVP(y, t-1);
        // set ptr of node i, i+1.
        set_child_node(x, i,   y);
        set_child_node(x, i+1, z);
		x->n++;
		disk_write(x);
		disk_write(y);
		disk_write(z);

        return z;
	}

#define ROOT_NODE_INDEX (disk->hdr->root_node_index)

	// insert new key into leaf node
	void insert(key_val kv)
	{
        // insert into full root node;
        // produce a new root node.
		if (root->n >= MAX_ITEMS) {
			cout << endl << "Insert into full root node #" << root << endl;
            cout << endl << "Node count: " << node_count << endl;
			node *new_root = allocate_node();
            if (new_root == NULL) {
                cerr << __func__ << "(): allocate node failed." << endl;
                last_error = BTREE_OUT_OF_STORAGE;
                return;
            }
			new_root->leaf = false;
			new_root->n    = 0;
            // root is left child of new root.
            set_child_node(new_root, 0, root); //NODE_FIRST_PTR(s) = root;
			split_child(new_root, 0, root); // split on node r.
			root = new_root;
            // update new root
            ROOT_NODE_INDEX = NODE2IDX(new_root);
		}
        insert_nonfull(root, kv);
	}

    //TODO handle duplicate key.
	void insert_nonfull(node *x, key_val kv)
	{
        assert(x->n < MAX_ITEMS);
//cerr << "+" << __func__ << "(): x:" << x << endl;
		//XXX 处理x->n为0的情况.
		int i = x->n - 1;
//cerr << "x:       " << x << endl
//     << "x->n:    " << x->n << endl
//     << "x->leaf: " << (u32)x->leaf << endl;
        // simple case: insert into non-full, leaf node.
		if (x->leaf) {
			//node *lc = get_last_child_node(x);
			//[1,...,n] <= [0,...,n - 1]
			//XXX preserve the last ptr of x.
            u32 last_ptr = NODE_LAST_PTR(x);
			for (; i >= 0 && kv.k < NODE_KEY(x, i); i--)
				NODE_ITEM(x, i+1) = NODE_ITEM(x, i);
			// insert key-val-pair kv into x.
			NODE_KVP(x, i+1) = kv;
			x->n++;
			//XXX restore the last ptr of x.
            NODE_LAST_PTR(x) = last_ptr;
			disk_write(x);
            return;
		}

        // search thru the non-leaf node.
        for (; i >= 0 && kv.k < NODE_KEY(x, i); i--)
            ;
        //FIX: -1 => 0; i' => i'+1; n-1 => n
        i++;
        node *y = disk_read(NODE_PTR(x, i));

        // split full node y down the road.
        if (y->n >= MAX_ITEMS) {
            node *z = split_child(x, i, y);
            if (kv.k > NODE_KEY(x, i))
                y = z; // search right half
        }
        // now we can insert into non-full node y.
        insert_nonfull(y, kv);
	}

    // kvp count
    u64 item_count(node *x)
    {
        u64 cnt = x->n;
        if (x->leaf)
            return cnt;

        for (int i = 0; i <= x->n; ++i) {
            u32 ptr = NODE_PTR(x, i);
            node *y = disk_read(ptr);
            cnt += item_count(y);
        }
        return cnt;
    }

    u64 item_count()
    {
        return item_count(root);
    }

    node *get_max_node()
    {
        cerr << __func__ << "(): node count:"
            << disk->hdr->node_count << endl;
        if (disk->hdr->node_count == 0)
            return NULL;
        return search_max(root);
    }

    item *get_max_item()
    {
        if (!root) {
            cerr << __func__ << "(): empty tree." << endl;
            return NULL;
        }
        cerr << __func__ << "(): root:" << root
             << "(): root->n:" << root->n << endl;
        node *x = search_max(root);
        if (!x) {
            cerr << __func__ << "(): no max node." << endl;
            return NULL;
        }
        item *it = &(NODE_LAST_ITEM(x));
        return it;
    }

    item *get_min_item()
    {
        if (!root) {
            cerr << __func__ << "(): empty tree." << endl;
            return NULL;
        }
        cerr << __func__ << "(): root:" << root
             << "(): root->n:" << root->n << endl;
        node *x = search_min(root);
        if (!x) {
            cerr << __func__ << "(): no max node." << endl;
            return NULL;
        }
        item *it = &(NODE_FIRST_ITEM(x));
        return it;
    }

	// find the max item in node x or its subtree.
	node *search_max(node *x)
	{
        if (x == NULL || x->n == 0)
            return NULL;
		if (x->leaf)
			return x;
		node *lc = disk_read(NODE_LAST_PTR(x));
        if (lc == x) {
            cerr << "search_max(): invalid node: " << x << endl;
            last_error = 2;
            return NULL;
        }
		return search_max(lc);
	}

	// find the min item in node x or its subtree.
	node *search_min(node *x)
	{
        cerr << __func__ << "(): x:" << x
             << ", leaf:" << x->leaf
             << ", n:" << x->n << endl;
		if (!x || !x->n)
			return NULL;
		if (x->leaf)
			return x;
		node *s = disk_read(NODE_FIRST_PTR(x));
		return search_min(s);
	}

	void dump_item(item it, int i)
	{
		cout << "[" << i << "](" << it.i << ", " << it.k << ")" << endl;
	}

	void dump_node(node *x, int more = 1)
	{
		cout << setfill('>') << setw(40) << ":" << endl;
		if (x == NULL) {
			cout << "EMPTY NODE:" << endl;
			return;
		}
		cout << endl << (x == root ? "ROOT " : "INTERN ") << "NODE #" << x << ", N=" << x->n << endl;
		if (x->n == 0)
			return;
		item li, ri;
		li = NODE_FIRST_ITEM(x);
		ri = NODE_LAST_ITEM(x);
		dump_item(li, 0);
		dump_item(ri, x->n - 1);
		cout << "[" << x->n << "](" << NODE_LAST_PTR(x) << ", *)" << endl;
		if (!x->leaf) {
			node *l = disk_read(NODE_FIRST_PTR(x));
			node *r = disk_read(NODE_LAST_PTR(x));
			cout << endl << "Dump item #0: k=" << NODE_KEY(x, 0) << endl;
			dump_node(l);
			for (int j = 1; j < more; j++) {
				cout << endl << "Dump item #" << j << ": k=" << NODE_KEY(x, j) << endl;
				node *m = disk_read(NODE_PTR(x, j));
				dump_node(m);
			}
			cout << endl << "Dump item #" << x->n << ": *" << endl;
			dump_node(r);
		}
		cout << setfill('<') << setw(40) << ":" << endl;
	}

	V *search(K k)
	{
		return search(root, k);
	}

	//XXX perform concate on the path of search??
	// search item with key k in node x and its subtree.
	V *search(node *x, K k)
	{
		int i = 0;
		// search
		while (i < x->n && k > NODE_KEY(x, i))
			i++;
		// hit!
		if (i < x->n && k == NODE_KEY(x, i)) {
			//cout << "hit on x=" << inf->ptr << ", i=" << inf->idx << endl;
			return &NODE_VAL(x, i);
		}
		// not found!
		if (x->leaf) {
			return NULL;
		}
		// continue to subtree.
//        cerr << "search(): x:" << x
//            << ", x.ptr[i]:" << NODE_PTR(x,i) << endl;
		node *y = disk_read(NODE_PTR(x, i));
		return search(y, k);
	}

	// erase the max item in node x.
	// return the (leaf)node that hold the max item.
	// the max item be deleted after this call.
	node *erase_max(node *x)
	{
		if (x->leaf) {
			if (x->n == 0)
				return NULL;
			x->n--;
			return x;
		}
		node *y = disk_read(NODE_LAST_PTR(x));
		fixup(x, x->n - 1); // on last item.
		return erase_max(y);
	}

	void erase(K k)
	{
		erase(root, k);
		// strip empty root node.
		// tree_height--
		if (root->n == 0) {
			node *r = NODE_FIRST_PTR(root);
			free_node(root);
			root = r;
		}
	}

#ifdef PROFILE
	int search_miss_cnt;

	int erase_cnt;
#endif

	// recursive version.
	// analog to search.
	// search & delete from the root node.
	void erase(node *x, K k)
	{
#ifdef PROFILE
		erase_cnt++;
#endif // PROFILE

		//cerr << "erase k=" << k << " on node#" << x << endl;
		//if (k == 1981) {
		//	cerr << endl << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>" << endl;
		//	cerr << "erase k=" << k << " on node#" << x << endl;
		//	dump_node(x);
		//	cerr << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" << endl << endl;
		//}

		int i = 0;
		// 1 search for key k.
		while (i < x->n && k > NODE_KEY(x, i))
			i++;
		// hit.
		if (k == NODE_KEY(x, i)) {
			if (x->leaf) {
				// 1 Erase item on leaf node.
				for (int j = i; j <= x->n - 1; j++) // last ptr of x included.
					NODE_ITEM(x, j) = NODE_ITEM(x, j + 1);
				x->n--;
				// FIXUP: node x may underflow.
				return;
			}
			else {
				// 2.a Erase item on internal node.
				// Find predecessor/successor of item i, then apply cut&paste.
				node *y, *p;
				y = disk_read(NODE_PTR(x, i));
				p = erase_max(y); // copy, Find predecessor
				assert(p != NULL);
				assert(p->leaf); // erase on leaf.
				NODE_KVP(x, i) = NODE_KVP(p, p->n); // paste, the latest deleted kvp in p. 
			}
		}
		else { // if (k <> NODE_KEY(x, i)) {
			K k2 = NODE_KEY(x, i);
			if (x->leaf) {
				cout << "Key=" << k << " not found on:" << endl;
				dump_node(x);
				search_miss_cnt++;
				return;
			}
			// continue to subtree.
			node *z = disk_read(NODE_PTR(x, i)); // last ptr of x.
			assert(z != NULL);
			erase(z, k);
		}

		fixup(x, i);
	}

#ifdef PROFILE
	int fixup_cnt;
#endif // PROFILE

	// fix subtree at item i.
	void fixup(node *x, int i)
	{
		assert(!x->leaf);
		assert(i <= x->n);

		// fixup:
		if (i == x->n)
			i--; // at last item.

#ifdef PROFILE
		fixup_cnt++;
#endif // PROFILE

		node *y, *z;
		y = disk_read(NODE_PTR(x, i));
		z = disk_read(NODE_PTR(x, i+1));
		// rebalance: make items equally spread among y and z.
		//XXX a stricter rule: n <= t.
		if (y->n < t-1 || z->n < t-1) { // < t - 1 ?
			int tn = y->n + z->n;
			if (tn < 2 * t-1) { // one more for median.
				concate(x, i); // y += median + z.
			}
			else {
				rebalance(x, i);
			}
		}
	}

#ifdef PROFILE
	int rebalance_cnt;
	int rebalance_leaf_cnt;
	int rebalance_inter_cnt;
#endif

	// rebalance of node y, z:
	// rebalancing of left and right subtree of item i,
	// equally distribute items among node y and z.
	void rebalance(node *x, int i)
	{
#ifdef PROFILE
		rebalance_cnt++;
#endif // PROFILE

		node *y = disk_read(NODE_PTR(x, i));
		node *z = disk_read(NODE_PTR(x, i+1));
		int tn, an, nz, ny;
		tn = y->n + z->n; // total
		an = tn / 2;      // average
		nz = an;          // new size of z
		ny = tn - nz;     // new size of y

#ifdef PROFILE
		if (y->leaf)
			rebalance_leaf_cnt++;
		else
			rebalance_inter_cnt++;
#endif // PROFILE

		//cerr << "before concate: ny=" << y->n << ", nz=" << z->n << endl;

		// rebalance of node items.
		if (y->n < ny) { // y << z;
			int n = ny - y->n;
			// move top (n-1) items from z to y.
			NODE_KVP(y, y->n) = NODE_KVP(x, i); // 1: [n], median in x.
			// n-1: [0,n-2] [n + 1, n + n2y]
			for (int j = 0; j <= n - 1; j++) { // one more for last ptr.
				NODE_ITEM(y, (y->n + 1) + j) = NODE_ITEM(z, j);
			}
			NODE_KVP(x, i) = NODE_KVP(z, n - 1); // for new median
			// remove top n items from z.
			for (int j = n; j <= z->n; j++) { // include last ptr.
				NODE_ITEM(z, j - n) = NODE_ITEM(z, j);
			}
		}
		else { // if (y->n > ny) { // move nodes from y to z.
			int n = y->n - ny; // n for z.
			assert(nz == z->n + n);
			// in z: make room for new items from y.
			for (int j = nz; j >= n; j--) { // one more for last ptr of z.
				NODE_ITEM(z, j) = NODE_ITEM(z, j - n);
			}
			// n-1: [0,n-2], move last (n-1) items from y to z.
			for (int j = 0; j <= n - 1; j++) { // one ptr for z from last ptr of y.
				NODE_ITEM(z, j) = NODE_ITEM(y, j + ny + 1);
			}
			// 1: [n-1], median to z.
			NODE_KVP(z, n - 1) = NODE_KVP(x, i);
			// 1: [ny], one from y to median.
			NODE_KVP(x, i) = NODE_KVP(y, ny); // preserve last ptr for y.
		}
		// update node size.
		y->n = ny;
		z->n = nz;

		disk_write(z);
		disk_write(y);
		disk_write(x);

		//cerr << "after concate: ny=" << y->n << ", nz=" << z->n << endl;
	}

#ifdef PROFILE
	int concate_cnt;
	int concate_leaf_cnt;
	int concate_inter_cnt;
#endif // PROFILE

	// concatenate node y, z with separator in x.
	void concate(node *x, int i)
	{
#ifdef PROFILE
		concate_cnt++;
#endif // PROFILE

		node *y = disk_read(NODE_PTR(x, i));
		node *z = disk_read(NODE_PTR(x, i + 1));

#ifdef PROFILE
		if (y->leaf)
			concate_leaf_cnt++;
		else
			concate_inter_cnt++;
#endif // PROFILE

		//cout << "concate: x:" << x << ", i:" << i << ", y:" << y << ", z:" << z << endl;
		//dump_node(x);

		// cut & paste:
		// append item i at the end of pn node.
		NODE_KVP(y, y->n) = NODE_KVP(x, i); //item [ny].
		for (int j = 0; j <= z->n; j++) //item [ny+1, ny+1+nz]
			NODE_ITEM(y, y->n + 1 + j) = NODE_ITEM(z, j); // last ptr of z included.
		// remove item i from x.
		for (int j = i; j < x->n; j++)
			NODE_ITEM(x, j) = NODE_ITEM(x, j + 1); // last ptr of x included.
		NODE_PTR(x, i) = y; // fix
		x->n--;
		y->n = y->n + 1 + z->n;
		free_node(z);

		// fixup:
		//if (x == root && x->n == 0) {
		//	node *r = root;
		//	root = y;
		//	free_node(r);
		//}

		disk_write(x);
		disk_write(y);
		//disk_write(z);
	}

};

// key_info
// u32 as key.
// save last key in disk->hdr_idx.
struct value_info {
    u64 offset;
    u32 size;
}__attribute__((packed,aligned(4)));

#pragma pack(1)
// size: 12-byte ?
struct value_info1 {
    u64 offset;
    u32 size;
};
#pragma pack()

int
main()
{
    Timer timer;

    cout << "value_info size:" << sizeof(value_info) << endl; 
    assert(sizeof(value_info) == 12);

    typedef btree<u32, value_info> tree;
    tree *t = new tree();
    cout << "tree node item size:" << sizeof(tree::item) << endl; 

    u32 last_key, max_key = 10000 * 1000 * 5 + 10000;//10000;
    value_info last_val = {0, 0};
    // 0, 30*1024

    tree::item *it = t->get_min_item();
    if (it) {
        // get min item in node.
        cout << "FIRST key: " << it->k
             << ", offset: " << it->v.offset
             << ", size:   " << it->v.size << endl;
        last_key = it->k;
        last_val = it->v;
        cout << "Do not add new nodes." << endl;
    } else {
        cout << "Do add new nodes." << endl;
        srand(time(0));
        u32 step = 20*1024;
        u32 size = 30*1024;
        u32 diff = (rand() % step) & ~0x7; // align to 8-byte.

        cout << endl;
        timer.Start();
        for (   last_key=1; !t->last_error && last_key <= max_key;
                last_key++) {
            last_val.offset += last_val.size;
            diff = (rand() % (20*1024)) & ~0x7; // align to 8-byte.
            last_val.size = size + diff; 
            tree::key_val kv = {last_key, last_val};
            cout << "\r  key: " << std::setw(10) << last_key
                 << ", v.ofs: " << std::setw(15) << last_val.offset
                 << ", v.siz: " << std::setw(6)  << last_val.size
                 << " "; // << endl;

            t->insert(kv);
        }
        cout << endl;
        double t_insert = timer.Stop();
        cout << "insertion loop terminated!" << endl;
        cout << "take " << t_insert << " seconds." << endl;
    }

    cout << "root item: " << endl;
    for (int i=0; i < t->root->n; ++i) {
        tree::item it = NODE_ITEM(t->root, i);
        cout << "#" << i+1 << " k:" << it.k
             << " v.ofs:" << it.v.offset
             << endl;
    }

    cout << "node count: 0x" << std::hex << t->node_count << endl;
    cout << "node count: " << std::dec << t->node_count << endl;

    u64 item_cnt = t->item_count();
    cout << "item count: " << item_cnt << endl;

    cout << "items per node: " << item_cnt / t->node_count << endl;

    assert(item_cnt == max_key);

    cout << "begin search..." << endl;
    timer.Start();
    // search all keys.
    u32 search_hit = 0, search_miss = 0;
    for (last_key = 1; last_key <= max_key; last_key++) {
        value_info *vp;
        vp = t->search(last_key);
        if (vp == NULL) {
            search_miss++;
            cout << "\rsearch miss on key=" << last_key << " ";// << endl;
            //continue;
            return -1;
        }
        search_hit++;
        cout << "\r  key: " << std::setw(10) << last_key
             << ", v.ofs: " << std::setw(15) << vp->offset
             << ", v.siz: " << std::setw(6)  << vp->size
             << " ";
    } 
    cout << endl;
    double t_search = timer.Stop();
    cout << "searching loop terminated!" << endl;
    cout << "take " << t_search << " seconds." << endl;
    cout << " hit:  " << search_hit
         << ",miss: " << search_miss<< endl;

    return 0;
}

