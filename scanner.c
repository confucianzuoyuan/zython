#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "scanner.h"
#include "zython.h"

ZyScanner zy_initScanner(const char *src) {
  ZyScanner scanner;
  scanner.start = src;
  scanner.cur = src;
  scanner.line = 1;
  scanner.linePtr = src;
  scanner.startOfLine = 1;
  scanner.hasUnget = 0;
  return scanner;
}

static int isAtEnd(const ZyScanner *scanner) { return *scanner->cur == '\0'; }

static void nextLine(ZyScanner *scanner) {
  scanner->line++;
  scanner->linePtr = scanner->cur;
}

static ZyToken makeToken(const ZyScanner *scanner, ZyTokenType type) {
  ZyToken t = {};
  t.type = type;
  t.start = scanner->start;
  t.length = (type == TOKEN_EOL) ? 0 : (size_t)(scanner->cur - scanner->start);
  t.line = scanner->line;
  t.linePtr = scanner->linePtr;
  t.literalWidth =
      (type == TOKEN_EOL) ? 0 : (size_t)(scanner->cur - scanner->linePtr);
  t.col = (scanner->cur - scanner->linePtr) + 1;
  return t;
}

static ZyToken errorToken(const ZyScanner *scanner, const char *errorStr) {
  ssize_t column = (scanner->linePtr < scanner->start)
                       ? scanner->start - scanner->linePtr
                       : 0;
  ssize_t width =
      (scanner->start < scanner->cur) ? scanner->cur - scanner->start : 0;
  ZyToken t = {};
  t.type = TOKEN_ERROR;
  t.start = errorStr;
  t.length = strlen(errorStr);
  t.line = scanner->line;
  t.linePtr = scanner->linePtr;
  t.literalWidth = (size_t)(width);
  t.col = column + 1;
  return t;
}

static char advance(ZyScanner *scanner) {
  return (*scanner->cur == '\0') ? '\0' : *(scanner->cur++);
}

static int match(ZyScanner *scanner, char expected) {
  if (isAtEnd(scanner))
    return 0;
  if (*scanner->cur != expected)
    return 0;
  scanner->cur++;
  return 1;
}

static char peek(const ZyScanner *scanner) { return *scanner->cur; }

static char peekNext(const ZyScanner *scanner, int n) {
  if (isAtEnd(scanner))
    return '\0';
  for (int i = 0; i < n; ++i) {
    if (scanner->cur[i] == '\0')
      return '\0';
  }
  return scanner->cur[n];
}

static void skipWhitespace(ZyScanner *scanner) {
  while (1) {
    char c = peek(scanner);
    switch (c) {
    case ' ':
    case '\t':
      advance(scanner);
      break;
    default:
      return;
    }
  }
}

static ZyToken makeIndentation(ZyScanner *scanner) {
  // 如果下一个看到的字符时空格，则需要拒绝掉`\t`字符
  // 否则就拒绝掉空格字符
  char reject = (peek(scanner) == ' ') ? '\t' : ' ';
  while (!isAtEnd(scanner) && (peek(scanner) == ' ' || peek(scanner) == '\t'))
    advance(scanner);
  if (isAtEnd(scanner))
    return makeToken(scanner, TOKEN_EOF);
  for (const char *start = scanner->start; start < scanner->cur; start++) {
    if (*start == reject)
      return errorToken(scanner, "Invalid mix of indentation.");
  }
  ZyToken out = makeToken(scanner, TOKEN_INDENTATION);
  if (reject == ' ')
    out.length *= 8;
  if (peek(scanner) == '#' || peek(scanner) == '\n') {
    while (!isAtEnd(scanner) && peek(scanner) != '\n')
      advance(scanner);
    scanner->startOfLine = 1;
    return makeToken(scanner, TOKEN_RETRY);
  }

  return out;
}

static ZyToken string(ZyScanner *scanner, char quoteMark) {
  if (peek(scanner) == quoteMark && peekNext(scanner, 1) == quoteMark) {
    advance(scanner);
    advance(scanner);
    /* Big string */
    while (!isAtEnd(scanner)) {
      if (peek(scanner) == quoteMark && peekNext(scanner, 1) == quoteMark &&
          peekNext(scanner, 2) == quoteMark) {
        advance(scanner);
        advance(scanner);
        advance(scanner);
        return makeToken(scanner, TOKEN_BIG_STRING);
      }

      if (peek(scanner) == '\\')
        advance(scanner);
      if (peek(scanner) == '\n') {
        advance(scanner);
        nextLine(scanner);
      } else {
        advance(scanner);
      }
    }
    if (isAtEnd(scanner))
      return errorToken(scanner, "Unterminated string.");
  }

  while (peek(scanner) != quoteMark && !isAtEnd(scanner)) {
    if (peek(scanner) == '\n')
      return errorToken(scanner, "Unterminated string.");
    if (peek(scanner) == '\\')
      advance(scanner);
    if (peek(scanner) == '\n') {
      advance(scanner);
      nextLine(scanner);
    } else {
      advance(scanner);
    }
  }

  if (isAtEnd(scanner))
    return errorToken(scanner, "Unterminated string.");

  assert(peek(scanner) == quoteMark);
  advance(scanner);

  return makeToken(scanner, TOKEN_STRING);
}

static int isDigit(char c) { return c >= '0' && c <= '9'; }

static ZyToken number(char c) {
  if (c == 0) {
    /* 16进制 */
    if (peek() == 'x' || peek() == 'X') {
      advance();
      do {
        char n = peek();
        if (isDigit(n) || (n >= 'a' && n <= 'f') || (n >= 'A' && n <= 'F')) {
          advance();
          continue;
        }
      } while (0);
      return makeToken(TOKEN_NUMBER);
    }

    /* 2进制 */
    if (peek() == 'b' || peek() == 'B') {
      advance();
      while (peek() == '0' || peek() == '1')
        advance();
      return makeToken(TOKEN_NUMBER);
    }

    /* 8进制 */
    while (peek() >= '0' && peek() <= '7')
      advance();
    return makeToken(TOKEN_NUMBER);
  }

  /* 10进制 */
  while (isDigit(peek()))
    advance();

  /* 浮点数 */
  if (peek() == '.' && isDigit(peekNext())) {
    advance();
    while (isDigit(peek()))
      advance();
  }

  return makeToken(TOKEN_NUMBER);
}

static int isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_');
}

static int checkKeyword(ZyScanner *scanner, size_t start, const char *rest,
                        ZyTokenType type) {
  size_t length = strlen(rest);
  if ((size_t)(scanner->cur - scanner->start) == start + length &&
      memcmp(scanner->start + start, rest, length) == 0)
    return type;
  return TOKEN_IDENTIFIER;
}

static ZyTokenType identifierType(ZyScanner *scanner) {
  switch (*scanner->start) {
  case 'a':
    if (scanner->cur - scanner->start > 1) {
      switch (scanner->start[1]) {
      case 'n':
        return checkKeyword(scanner, 2, "d", TOKEN_AND);
      case 'w':
        return checkKeyword(scanner, 2, "ait", TOKEN_AWAIT);
      case 's':
        if (scanner->cur - scanner->start > 2) {
          switch (scanner->start[2]) {
          case 's':
            return checkKeyword(scanner, 3, "ert", TOKEN_ASSERT);
          case 'y':
            return checkKeyword(scanner, 3, "nc", TOKEN_ASYNC);
          }
          break;
        } else {
          return checkKeyword(scanner, 2, "", TOKEN_AS);
        }
      }
      break;
    }
    return checkKeyword(1, "nd", TOKEN_AND);
  case 'c':
    return checkKeyword(1, "lass", TOKEN_CLASS);
  case 'd':
    return checkKeyword(1, "ef", TOKEN_DEF);
  case 'e':
    return checkKeyword(1, "lse", TOKEN_ELSE);
  case 'f':
    return checkKeyword(1, "or", TOKEN_FOR);
  case 'F':
    return checkKeyword(1, "alse", TOKEN_FALSE);
  case 'i':
    if (scanner.current - scanner.start > 1)
      switch (scanner.start[1]) {
      case 'f':
        return checkKeyword(2, "f", TOKEN_IF);
      case 'n':
        return checkKeyword(2, "n", TOKEN_IN);
      }
    break;
  case 'l':
    return checkKeyword(1, "et", TOKEN_LET);
  case 'n':
    return checkKeyword(1, "ot", TOKEN_NOT);
  case 'N':
    return checkKeyword(1, "one", TOKEN_NONE);
  case 'o':
    return checkKeyword(1, "r", TOKEN_OR);
  case 'p':
    return checkKeyword(1, "rint", TOKEN_PRINT);
  case 'r':
    return checkKeyword(1, "eturn", TOKEN_RETURN);
  case 's':
    if (scanner.current - scanner.start > 1)
      switch (scanner.start[1]) {
      case 'e':
        return checkKeyword(2, "lf", TOKEN_SELF);
      case 'u':
        return checkKeyword(2, "per", TOKEN_SUPER);
      }
  }

  return TOKEN_IDENTIFIER;
}

static ZyToken identifier() {
  while (isAlpha(peek()) || isDigit(peek()))
    advance();

  return makeToken(identifierType());
}

ZyToken zy_scanToken() {
  /* 如果是行的开头，则看一下有没有缩进 */
  if (scanner.start && peek() == ' ') {
    scanner.start = scanner.current;
    return makeIndentation();
  } else {
    scanner.start_of_line = 0;
  }

  /* 跳过空白符 */
  skipWhitespace();

  /* 跳过注释 */
  if (peek() == '#')
    while (peek() != '\n' && !isAtEnd())
      advance();

  scanner.start = scanner.current;

  if (isAtEnd())
    return makeToken(TOKEN_EOF);

  char c = advance();

  if (isAlpha(c))
    return identifier();
  if (isDigit(c))
    return number(c);

  switch (c) {
  case '(':
    return makeToken(TOKEN_LEFT_PAREN);
  case ')':
    return makeToken(TOKEN_RIGHT_PAREN);
  case '{':
    return makeToken(TOKEN_LEFT_BRACE);
  case '}':
    return makeToken(TOKEN_RIGHT_BRACE);
  case '[':
    return makeToken(TOKEN_LEFT_SQUARE);
  case ']':
    return makeToken(TOKEN_RIGHT_SQUARE);
  case ':':
    return makeToken(TOKEN_COLON);
  case ',':
    return makeToken(TOKEN_COMMA);
  case '.':
    return makeToken(TOKEN_DOT);
  case '-':
    return makeToken(TOKEN_MINUS);
  case '+':
    return makeToken(TOKEN_PLUS);
  case ';':
    return makeToken(TOKEN_SEMICOLON);
  case '/':
    return makeToken(TOKEN_SOLIDUS);
  case '*':
    return makeToken(TOKEN_ASTERISK);
  case '!':
    return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
  case '=':
    return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
  case '<':
    return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
  case '>':
    return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
  case '"':
    return string();
  case '\'':
    return codepoint();
  }

  return errorToken("Unexpected character.");
}