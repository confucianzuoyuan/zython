#include "zython.h"
#include "scanner.h"
int main(int argc, char *argv[]) {
  zy_initScanner("\"hello\" + \"hellf\" + 1.4");

  return 0;
}