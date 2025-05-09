#include "zython.h"
#include "scanner.h"
#include "stdio.h"
int main(int argc, char *argv[]) {
  ZyScanner scanner =
      zy_initScanner("global \"hello\" +asdf== \"hellf\" + 1.4");
  ZyToken t = {};
  while (1) {
    t = zy_scanToken(&scanner);
    printToken(t);
    if (t.type == TOKEN_EOF) {
      break;
    }
  }
  return 0;
}