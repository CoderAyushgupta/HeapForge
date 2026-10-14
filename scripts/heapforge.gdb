# GDB helpers for HeapForge. Load with:  gdb -x scripts/heapforge.gdb --args ./bin/heapforge-debug ...
set pagination off
set print pretty on

# hfblock <payload-pointer> : print the header that precedes a payload
define hfblock
  print *(hf_block_t *)((char *)$arg0 - sizeof(hf_block_t))
end
document hfblock
Show the HeapForge block header for a payload pointer.
end

# hfheap : print the allocator's global state (lists, counters, config)
define hfheap
  print *hf_heap()
end

# hffree : walk the free list and print each node
define hffree
  set $n = hf_heap()->free_head
  while $n != 0
    printf "free block %p size %lu\n", $n, $n->size
    set $n = $n->free_next
  end
end

break hf_heap_grow
commands
  printf "heap is growing: sbrk about to be called\n"
  bt 3
  continue
end
