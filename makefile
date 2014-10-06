
all: test

d:
	@make erase
	@make db
	./db


dump:
	echo #### disk mapping ### 
	#./db
	echo
	echo #### disk dump: off: 20K ### 
	#dd if=idx.bin of=dump.bin bs=4096 count=1 skip=1024
	dd if=hdr.bin of=dump_hdr.bin bs=4096 count=1
	echo
	echo #### show disk dump ### 
	xxd -l 64 dump_hdr.bin
	#od -txa -w16 -Ax dump0.bin
	#hexdump -C dump.bin
	echo

db: disk.o db.o
	g++ $^ -o $@

disk.o: disk.cpp
	g++ -c $^ -o $@ 
db.o: btree-db.cpp
	g++ -c $^ -o $@

clean:
	rm -f a.exe db.exe* *.o

# calculator by call (bash) shell command.
calc=$(shell echo $$\(\($(1)\)\))
SZ_1K:=$(call calc,32*32)
SZ_4K:=$(call calc,$(SZ_1K)*4)
SZ_4M:=$(call calc,$(SZ_4K)*$(SZ_1K))
SZ_4G:=$(call calc,$(SZ_4M)*$(SZ_1K))

hdr_sz:=$(call calc,$(SZ_4K)*1)
# bitmap: 128K
bit_sz:=$(call calc,$(SZ_4K)*32)
# inode array: 4G
ino_sz:=$(call calc,$(SZ_4K)*$(SZ_1K)*$(SZ_1K))
idx_file_sz:=$(call calc,$(hdr_sz)+$(bit_sz)+$(ino_sz))
blk_sz:=$(SZ_4K)
blk_cnt:=$(call calc,$(idx_file_sz)/$(blk_sz))

math:
	@echo "SZ_1K is:" $(SZ_1K)
	@echo "SZ_4K is:" $(SZ_4K)
	@echo "SZ_4M is:" $(SZ_4M)
	@echo "SZ_4G is:" $(SZ_4G)
	@echo
	@echo "idx hdr :" $(hdr_sz)
	@echo "idx bit :" $(bit_sz)
	@echo "idx ino :" $(ino_sz)
	@echo "idx file:" $(idx_file_sz)
	@echo "blk cnt :" $(blk_cnt)

index:
	dd if=/dev/zero of=hdr.bin bs=$(blk_sz) count=33
	dd if=/dev/zero of=idx.bin bs=$(blk_sz) count=$(blk_cnt)

erase:
	dd if=/dev/zero of=hdr.bin bs=$(blk_sz) count=33

distclean: clean
	rm -f *~

.PHONY: all test clean distclean

