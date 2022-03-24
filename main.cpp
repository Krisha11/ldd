#include <ldd_worker.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("usage: %s <elf-binary>\n", argv[0]);
    return 1;
  }

  LddWorker worker = LddWorker();
  worker.Execute(argv[1]);

  return 0;
}
