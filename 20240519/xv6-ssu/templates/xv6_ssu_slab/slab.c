#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "slab.h"

struct {
	struct spinlock lock;
	struct slab slab[NSLAB];
} stable;

void setBit(char *bitmap, int idx, int val) {
	int byte_idx = idx / 8;
	int bit_offset = idx % 8;

	bitmap[byte_idx] = val ? bitmap[byte_idx] | (1 << bit_offset) : bitmap[byte_idx] & ~(1 << bit_offset);
}

int getBit(char *bitmap, int idx) {
	int byte_idx = idx / 8;
	int bit_offset = idx % 8;
	
	return (bitmap[byte_idx] >> bit_offset) & 1;
}

void slabinit(){
	struct slab *s;

	acquire(&stable.lock);

	for (int i = 0; i < NSLAB; i++) {
		s = &stable.slab[i];
		s->size = 1 << (i + 4);
		int obj_per_pg = PGSIZE / s->size;
		s->bitmap = kalloc();

		memset(s->bitmap, 0, PGSIZE);
		memset(s->page, 0, MAX_PAGES_PER_SLAB);

		s->page[0] = kalloc();
		s->num_objects_per_page = obj_per_pg;
		s->num_used_objects = 0;
		s-> num_free_objects = obj_per_pg;
		s->num_pages++;
	}

	release(&stable.lock);
}

char *kmalloc(int size){
	struct slab *s;
	int obj_per_pg, pg_idx, offset;
	char *bitmap;

	acquire(&stable.lock);

	for (s = stable.slab; s < &stable.slab[NSLAB]; s++) {
		if (size <= s->size) {
			break;
		}
	}

	bitmap = s->bitmap;
	obj_per_pg = s->num_objects_per_page;

	for (int i = 0; i < obj_per_pg * MAX_PAGES_PER_SLAB; i++) {
		if (!getBit(bitmap, i)) {
			pg_idx = i / obj_per_pg;
			offset = i % obj_per_pg;

			if (s->page[pg_idx]) {
				s->num_free_objects--;
				s->num_used_objects++;

				setBit(bitmap, i, 1);
				release(&stable.lock);

				return s->page[pg_idx] + (offset * s->size);
			}
		}
	}

	for (int i = 0; i < obj_per_pg * MAX_PAGES_PER_SLAB; i++) {
		if (!getBit(bitmap, i)) {
			pg_idx = i / obj_per_pg;

			s->page[pg_idx] = kalloc();
			s->num_pages++;
			s->num_used_objects++;
			s->num_free_objects += s->num_objects_per_page - 1;

			setBit(bitmap, i, 1);
			release(&stable.lock);

			return s->page[pg_idx];
		}
	}
	
	release(&stable.lock);

	return 0x00;
}

void kmfree(char *addr, int size){
	struct slab *s;
	int idx, i, offset = 0, isfree = 1;
	char *bitmap;

	if (!addr || 0 >= size) {
		return;
	}

	acquire(&stable.lock);

	for (s = stable.slab; s < &stable.slab[NSLAB]; s++) {
		if (size <= s->size) {
			break;
		}
	}

	for (i = 0; i < MAX_PAGES_PER_SLAB; i++) {
		if (!s->page[i]) {
			continue;
		}
		
		offset = addr - s->page[i];
		
		if (offset >= 0 && offset < PGSIZE) {
			break;
		}
	}

	bitmap = s->bitmap;
	idx = i * s->num_objects_per_page + offset / s->size;

	if (!getBit(bitmap, idx)) {
		release(&stable.lock);

		return;
	}

	s->num_used_objects--;
	s->num_free_objects++;
	setBit(bitmap, idx, 0);

	for (int j = i * s->num_objects_per_page; j < (i + 1) * s->num_objects_per_page; j++) {
		if (getBit(bitmap, j)) {
			isfree = 0;
			break;
		}
	}

	if (isfree) {
		kfree(s->page[i]);
		s->page[i] = 0;
		s->num_pages--;
		s->num_free_objects -= s->num_objects_per_page;
	}

	release(&stable.lock);
}

/* Helper functions */
void slabdump(){
	cprintf("__slabdump__\n");

	struct slab *s;

	cprintf("size\tnum_pages\tused_objects\tfree_objects\n");

	for(s = stable.slab; s < &stable.slab[NSLAB]; s++){
		cprintf("%d\t%d\t\t%d\t\t%d\n", 
			s->size, s->num_pages, s->num_used_objects, s->num_free_objects);
	}
}

int numobj_slab(int slabid)
{
	return stable.slab[slabid].num_used_objects;
}

int numpage_slab(int slabid)
{
	return stable.slab[slabid].num_pages;
}
