#include <iostream>
#include <cstdlib>
#include <algorithm>
#include <cassert>
#include <iomanip>

//#include "timer.hpp"
#include "bench.hpp"

using namespace std;

#define DEBUG 1
#define PROFILE

// an in-memory B-Tree implementation.
//TODO a disk-based B-Tree.
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
	struct item {
		node *c; // to child node.
		union /*anon*/ {
			key_val kv;
			struct /*anon*/ {
				K k; // key
				V v; // value
			}/*anon*/;
		}/*anon*/;
		item() :c((node *) 0xDEADBEEF) {}
	};

	struct node {
		bool leaf; // is leaf node?
		int n; // keys in node
		item items[2]; // allocate more latter.
		//one more for last node ptr.
		node() :n(0) {}
	};

	// location of item in node x.
	struct item_info {
		node *ptr;
		int idx; // key(i) in node *x.
		item_info() :ptr(NULL), idx(0) {}
		item_info(node *x_, int i_) :ptr(x_), idx(i_) {}
	};
#define NI_ITEM(x, i)	((x)->items[i])
#define NI_PTR(x, i)	((x)->items[i].c)
#define NI_KEY(x, i)	((x)->items[i].k)
#define NI_VAL(x, i)	((x)->items[i].v)
#define NI_KVP(x, i)	((x)->items[i].kv)

#define NI_FIRST_ITEM(x) ((x)->items[0])
#define NI_LAST_ITEM(x) ((x)->items[(x)->n - 1])
#define NI_FIRST_PTR(x) ((x)->items[0].c)
#define NI_LAST_PTR(x)  ((x)->items[(x)->n].c)
#define NI_FIRST_KVP(x) ((x)->items[0].kv)
#define NI_LAST_KVP(x)  ((x)->items[(x)->n - 1].kv)

#define MIN_ITEMS (t - 1)
#define MAX_ITEMS (2 * t - 1)

	// items:  [t-1, 2t-1]
	// height: <= log(t,(n+1)/2)
	const int t;
	node *root;
	int node_count;

	// b-tree-create(T)
	// require O(1) disk operations and O(1) CPU time.
	btree(int _t)
		: t(_t),
		node_size(sizeof(node) +2 * _t * sizeof(item))
#ifdef PROFILE
		,
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
#endif
	{
		node *x = allocate_node();
		x->leaf = true;
		x->n = 0;
		disk_write(x);
		root = x;
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
		node *y = disk_read(NI_FIRST_PTR(x));
		if (y->leaf)
			return 1;
		else
			return (1 + height(y));
	}

	const size_t node_size;

	// create file/append/truncate
	node *allocate_node()
	{
		//size_t sz = sizeof(node) +2 * t * sizeof(item);
		node *np = (node *) calloc(1, node_size);
		if (np == NULL) {
			cerr << "fail to allocate node." << endl;
			exit(-1);
		}
		//cout << "allocate node: #" << node_count
		//	<< " [" << np << "]" << endl;
		// one more item for the last node ptr.
		node_count++;
		return np;
	}

	void free_node(node *x)
	{
		//cout << "[" << x << "] ";
		//cout << "free node: #" << node_count << endl;
		free(x);
		node_count--;
		//TODO delete node on disk.
	}

	void disk_write(node *x)
	{
		//TODO
		//cout << "disk write: node=" << x << endl;
		;
	}

	// interact with fs.
	// mmap ?
	// disk to main memory.
	// x: addr in disk
	node *disk_read(node *x)
	{
		//TODO
		//cout << "disk read: node=" << x << endl;
		;
		return x;
	}

	int split_cnt;
	// ITEM(i) >> ITEM(i+1)
	void split_child(node *x, int i, node *y)
	{
		// node y is full.
		assert(y->n == MAX_ITEMS);
		assert(y->n > 2);
		split_cnt++;

		node *z = allocate_node();
		z->leaf = y->leaf;
		z->n = MIN_ITEMS;
		// move right half (t-1 nodes) to z.
		// k, v and child ptr.
		// [0,t-2],[t-1],[t,2t-2]
		// t-1, 1, t-1
		for (int j = 0; j < t; j++) // one more for the last ptr.
			NI_ITEM(z, j) = NI_ITEM(y, t + j); // t for z(with ptr)
		// shrink node y.
		y->n = MIN_ITEMS;
		// make room for median item(from last item of y) of y and z.
		// insert last item of y into x at index i.
		// [n,i+1], [i,n + 1]
		for (int j = x->n + 1; j > i; j--) // include last ptr of x.
			NI_ITEM(x, j) = NI_ITEM(x, j - 1);
		// move y.key[t-1] up.
		NI_KVP(x, i) = NI_KVP(y, t - 1);
		NI_PTR(x, i) = y;
		NI_PTR(x, i + 1) = z;
		x->n++;
		disk_write(x);
		disk_write(y);
		disk_write(z);
	}

	// insert new key into leaf node
	void insert(key_val kv)
	{
		node *r = root;
		// root node is full.
		if (r->n == 2 * t - 1) {
			cout << "insert on full root node #" << root << endl;
			node *s = allocate_node();
			root = s;
			s->leaf = false;
			s->n = 0;
			NI_FIRST_PTR(s) = root;
			split_child(s, 0, r); // split on node r.
			insert_nonfull(s, kv); // insert from new root node.
		}
		else {
			insert_nonfull(r, kv);
		}
	}

	void insert_nonfull(node *x, key_val kv)
	{
		//XXX 处理x->n为0的情况.
		int i = x->n - 1;
		if (x->leaf) {
			//XXX preserve the last ptr of x.
			node *lp = NI_LAST_PTR(x);
			//[1,...,n] <= [0,...,n - 1]
			for (; i >= 0 && kv.k < NI_KEY(x, i); i--)
				NI_ITEM(x, i + 1) = NI_ITEM(x, i);
			// insert key-val-pair kv into x.
			NI_KVP(x, i + 1) = kv;
			x->n++;
			//XXX restore the last ptr of x.
			NI_LAST_PTR(x) = lp;
			disk_write(x);
		}
		else {
			for (; i >= 0 && kv.k < NI_KEY(x, i); i--)
				;
			i++; //fix;
			node *y = disk_read(NI_PTR(x, i));
			// split the full node.
			if (y->n == 2 * t - 1) {
				split_child(x, i, y);
				if (kv.k > NI_KEY(x, i))
					i++;
			}
			y = disk_read(NI_PTR(x, i)); //XXX
			insert_nonfull(y, kv);
		}
	}

	// find the max item in node x or its subtree.
	node *search_max(node *x)
	{
		if (x->leaf)
			return x;
		if (x->n == 0)
			return NULL;
		node *s = disk_read(NI_LAST_PTR(x));
		return search_max(s);
	}

	// find the min item in node x or its subtree.
	node *search_min(node *x)
	{
		if (x->leaf)
			return x;
		if (x->n == 0)
			return NULL;
		node *s = disk_read(NI_FIRST_PTR(x));
		return search_min(s);
	}

	void dump_item(item it, int i)
	{
		cout << "[" << i << "](" << it.c << ", " << it.k << ")" << endl;
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
		li = NI_FIRST_ITEM(x);
		ri = NI_LAST_ITEM(x);
		dump_item(li, 0);
		dump_item(ri, x->n - 1);
		cout << "[" << x->n << "](" << NI_LAST_PTR(x) << ", *)" << endl;
		if (!x->leaf) {
			node *l = disk_read(NI_FIRST_PTR(x));
			node *r = disk_read(NI_LAST_PTR(x));
			cout << endl << "Dump item #0: k=" << NI_KEY(x, 0) << endl;
			dump_node(l);
			for (int j = 1; j < more; j++) {
				cout << endl << "Dump item #" << j << ": k=" << NI_KEY(x, j) << endl;
				node *m = disk_read(NI_PTR(x, j));
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
		int i = 0; //XXX
		// search
		while (i < x->n && k > NI_KEY(x, i))
			i++;
		// hit!
		if (i < x->n && k == NI_KEY(x, i)) {
			//cout << "hit on x=" << inf->ptr << ", i=" << inf->idx << endl;
			return &NI_VAL(x, i);
		}
		// not found!
		if (x->leaf) {
			return NULL;
		}
		// continue to subtree.
		node *y = disk_read(NI_PTR(x, i));
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
		node *y = disk_read(NI_LAST_PTR(x));
		fixup(x, x->n - 1); // on last item.
		return erase_max(y);
	}

	void erase(K k)
	{
		erase(root, k);
		// strip empty root node.
		// tree_height--
		if (root->n == 0) {
			node *r = NI_FIRST_PTR(root);
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
		while (i < x->n && k > NI_KEY(x, i))
			i++;
		// hit.
		if (k == NI_KEY(x, i)) {
			if (x->leaf) {
				// 1 Erase item on leaf node.
				for (int j = i; j <= x->n - 1; j++) // last ptr of x included.
					NI_ITEM(x, j) = NI_ITEM(x, j + 1);
				x->n--;
				// FIXUP: node x may underflow.
				return;
			}
			else {
				// 2.a Erase item on internal node.
				// Find predecessor/successor of item i, then apply cut&paste.
				node *y, *p;
				y = disk_read(NI_PTR(x, i));
				p = erase_max(y); // copy, Find predecessor
				assert(p != NULL);
				assert(p->leaf); // erase on leaf.
				NI_KVP(x, i) = NI_KVP(p, p->n); // paste, the latest deleted kvp in p. 
			}
		}
		else { // if (k <> NI_KEY(x, i)) {
			K k2 = NI_KEY(x, i);
			if (x->leaf) {
				cout << "Key=" << k << " not found on:" << endl;
				dump_node(x);
				search_miss_cnt++;
				return;
			}
			// continue to subtree.
			node *z = disk_read(NI_PTR(x, i)); // last ptr of x.
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
		y = disk_read(NI_PTR(x, i));
		z = disk_read(NI_PTR(x, i + 1));
		// rebalance: make items equally spread among y and z.
		//XXX a stricter rule: n <= t.
		if (y->n < t - 1 || z->n < t - 1) { // < t - 1 ?
			int tn = y->n + z->n;
			if (tn < 2 * t - 1) { // one more for median.
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

		node *y = disk_read(NI_PTR(x, i));
		node *z = disk_read(NI_PTR(x, i + 1));
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
			NI_KVP(y, y->n) = NI_KVP(x, i); // 1: [n], median in x.
			// n-1: [0,n-2] [n + 1, n + n2y]
			for (int j = 0; j <= n - 1; j++) { // one more for last ptr.
				NI_ITEM(y, (y->n + 1) + j) = NI_ITEM(z, j);
			}
			NI_KVP(x, i) = NI_KVP(z, n - 1); // for new median
			// remove top n items from z.
			for (int j = n; j <= z->n; j++) { // include last ptr.
				NI_ITEM(z, j - n) = NI_ITEM(z, j);
			}
		}
		else { // if (y->n > ny) { // move nodes from y to z.
			int n = y->n - ny; // n for z.
			assert(nz == z->n + n);
			// in z: make room for new items from y.
			for (int j = nz; j >= n; j--) { // one more for last ptr of z.
				NI_ITEM(z, j) = NI_ITEM(z, j - n);
			}
			// n-1: [0,n-2], move last (n-1) items from y to z.
			for (int j = 0; j <= n - 1; j++) { // one ptr for z from last ptr of y.
				NI_ITEM(z, j) = NI_ITEM(y, j + ny + 1);
			}
			// 1: [n-1], median to z.
			NI_KVP(z, n - 1) = NI_KVP(x, i);
			// 1: [ny], one from y to median.
			NI_KVP(x, i) = NI_KVP(y, ny); // preserve last ptr for y.
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

		node *y = disk_read(NI_PTR(x, i));
		node *z = disk_read(NI_PTR(x, i + 1));

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
		NI_KVP(y, y->n) = NI_KVP(x, i); //item [ny].
		for (int j = 0; j <= z->n; j++) //item [ny+1, ny+1+nz]
			NI_ITEM(y, y->n + 1 + j) = NI_ITEM(z, j); // last ptr of z included.
		// remove item i from x.
		for (int j = i; j < x->n; j++)
			NI_ITEM(x, j) = NI_ITEM(x, j + 1); // last ptr of x included.
		NI_PTR(x, i) = y; // fix
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

Timer timer;

int
main()
{
	long cnt = 65536000; //10000 * 1000;
	int t = 128; //1024;

	cout << "Input parameters:" << endl;
	cout << "cnt (default: " << cnt << " ) ";
	cin >> cnt;
	cout << "  t (default: " << t << " ) ";
	cin >> t;

#if 0
	benchmark bm(cnt);
	bm.test();
#endif

	btree<int, int> tree(t);

	cout << "item count: " << cnt << endl;

	cout << "sizeof(bool): " << sizeof(bool) << endl;
	cout << "sizeof(node): " << sizeof(btree<int, int>::node) << endl;
	cout << "node size:    " << tree.node_size << endl;
	
	cout << "t = " << tree.t << endl;
	cout << "min items per node: " << (tree.t - 1) << endl;
	cout << "max items per node: " << (2 * tree.t - 1) << endl;

	cout << "Preparing data..." << endl;
	timer.Start();
	int *ai = new int[cnt];
	long i_prev = 0;
	for (long i = 0; i < cnt; i++) {
		ai[i] = i + 1;
	}

	cout << "Shuffling data..." << endl;
	random_shuffle(ai, ai + cnt);
	cout << "Finished shuffling data..." << endl;

	double t_prep = timer.Stop();
	cout << "Afer " << t_prep << " seconds." << endl;
	cout << "Finished preparing data..." << endl;

	cout << "Inserting data..." << endl;
	timer.Start();
	btree<int, int>::key_val kv;
	cout << endl;
	cerr << fixed << setw(3) << setprecision(2) << setfill(' ');
	for (int i = 0, i_prev = 0; i < cnt; i++) {
		//cout << "#" << i+1 << " insert item: k=" << ai[i] << endl;
		kv.k = ai[i];
		kv.v = ai[i] * 2;
		tree.insert(kv);
		#if 1
		// progress report:
		if ((i - i_prev) > 0.001 * cnt) {
			cerr << i * 100.0 / cnt << "%" << "\r";
			i_prev = i;
		}
		#endif
	}
	cout << endl << endl;
	double insert_time = timer.Stop();
	cout << "After " << insert_time << " seconds." << endl;
	cout << "Finished inserting data..." << endl;
	cout << "time for every insertion(sec): " << insert_time / cnt << endl;

	cout << "node size:   " << tree.node_size << endl;
	cout << "node count:  " << tree.node_count << endl;
	cout << "average items per node:  " << cnt / tree.node_count << endl;

	long total_items_cnt = tree.node_count * (tree.t * 2);
	cout << "item/node utilization ratio:  "
		<< 100.0 * cnt / total_items_cnt
		<< "%" << endl;

	cout << "tree height: " << tree.height() << endl;

#if 0
	cout << "root node:   " << endl;
	tree.dump_node(tree.root);
#endif

	cout << "searching data..." << endl;
	timer.Start();
	for (int i = 0, i_prev = 0; i < cnt; i++) {
		int k = ai [i];
		int *vp = tree.search(k);
		if (vp == NULL)
			cerr << "search miss!" << endl;
		#if 1
		// progress report:
		if ((i - i_prev) > 0.001 * cnt) {
			i_prev = i;
			cerr << i * 100.0 / cnt << "%" << "\r";
		}
		#endif
	}
	double search_time = timer.Stop();
	cout << "After " << search_time << " seconds." << endl;
	cout << "Finished searching data..." << endl;
	cout << "time for every searching(sec): " << search_time / cnt << endl;

	cout << "Erasing data..." << endl;
	timer.Start();

#if 0
	cout << "sequentialy erasing data..." << endl;
	// sequential erase
	//cerr << fixed << setw(3) << setprecision(2) << setfill(' ');
	for (int i = 0, i_prev = 0; i < cnt; i++) {
		int k = i + 1;
		//cout << endl << "#" << i + 1 << " erase item: k=" << k << endl;
		tree.erase(k);
		#if 1
		// progress report:
		if ((i - i_prev) > 0.001 * cnt) {
			i_prev = i;
			cerr << i * 100.0 / cnt << "%" << "\r";
		}
		#endif
	}
	cout << endl << endl;
#else
	cout << "RANDOM ERASING data..." << endl;
	// random erase
	//random_shuffle(ai, ai + cnt);
	for (int i = 0; i < cnt; i++) {
		int k = ai[i];
		//cout << endl << "#" << i + 1 << " erase item: k=" << k << endl;
		tree.erase(k);
	}
#endif

	double erase_time = timer.Stop();
	cout << "After " << erase_time << " seconds." << endl;
	cout << "Finished erasing data..." << endl;
	cout << "time for every erasing(sec): " << erase_time / cnt << endl;

	cout << "search miss count:  " << tree.search_miss_cnt << endl;
	cout << "Remain nodes count: " << tree.node_count << endl;

#if 0
	cout << "Root node dump:" << endl;
	tree.dump_node(tree.root);
#endif

	cout
		<< "Profile:             " << endl
		<< "split_cnt:           " << tree.split_cnt << endl
		<< "erase_cnt:           " << tree.erase_cnt << endl
		<< "fixup_cnt:           " << tree.fixup_cnt << endl
		<< "rebalance_cnt:       " << tree.rebalance_cnt << endl
		<< "rebalance_leaf_cnt:  " << tree.rebalance_leaf_cnt << endl
		<< "rebalance_inter_cnt: " << tree.rebalance_inter_cnt << endl
		<< "concate_cnt:           " << tree.concate_cnt << endl
		<< "concate_leaf_cnt:      " << tree.concate_leaf_cnt << endl
		<< "concate_inter_cnt:     " << tree.concate_inter_cnt << endl
		<< endl;

	delete [] ai;

	cout << "Press any key to exit." << endl;
#if 10
	cin.get();
	cin.get(); //XXX
#elif 0
	cin.ignore();
	cin.get();
#else
	cin.ignore(2); //XXX 1?
#endif
	return 0;
}

