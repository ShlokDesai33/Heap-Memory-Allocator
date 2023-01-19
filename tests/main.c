// first pointer returned is 8-byte aligned
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include "p3Heap.h"
#include "p3Heap.c"

int main() {
  assert(init_heap(4096) == 0);
  disp_heap();

  for (int i = 1; i < 50; i++) {
    if (balloc(i) == NULL) {
      break;
    }
  }

  disp_heap();
  
  exit(0);
}
