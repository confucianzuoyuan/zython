#include "zython.h"
#include "scanner.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

int main(int argc, char *argv[]) {
  // 检查命令行参数的数量
  if (argc < 3) {
    printf("用法: %s --verbose-lex <filename>\n", argv[0]);
    printf("      %s --verbose-ast <filename>\n", argv[0]);
    return 1;
  }

  char *filename = NULL;
  // 检查参数
  if (strcmp(argv[1], "--verbose-lex") == 0) {
    // 获取文件名
    filename = argv[2];
    printf("Verbose lex mode enabled. Filename: %s\n", filename);
  } else {
    printf("未知参数: %s\n", argv[1]);
  }

  FILE *file;
  char *buffer;
  long file_size;

  // Open the file in binary read mode
  file = fopen(filename, "rb");
  if (file == NULL) {
    perror("Error opening file");
    return 1;
  }

  // Get the file size
  fseek(file, 0, SEEK_END);
  file_size = ftell(file);
  rewind(file);

  // Allocate memory for the buffer
  buffer = (char *)malloc(file_size * sizeof(char));
  if (buffer == NULL) {
    perror("Error allocating memory");
    fclose(file);
    return 1;
  }

  // Read the file into the buffer
  size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
  if (bytes_read != file_size) {
    if (feof(file)) {
      printf("End of file reached, read %zu bytes\n", bytes_read);
    } else if (ferror(file)) {
      perror("Error reading file");
    }
    free(buffer);
    fclose(file);
    return 1;
  }

  // Close the file
  fclose(file);

  ZyScanner scanner = zy_initScanner(buffer);
  ZyToken t = {};
  while (1) {
    t = zy_scanToken(&scanner);
    printToken(t);
    if (t.type == TOKEN_EOF) {
      break;
    }
  }

  free(buffer);
  return 0;
}