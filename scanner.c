#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "scanner.h"
#include "zython.h"

typedef struct {
  const char *start;
  const char *current;
  size_t line;
  int start_of_line;
} ZyScanner;

ZyScanner scanner;

void zy_initScanner(const char *src) {
  scanner.start = src;
  scanner.current = src;
  scanner.line = 1;
  scanner.start_of_line = 1;
}

static int isAtEnd() { return *scanner.current == '\0'; }

static ZyToken makeToken(ZyTokenType type) {
  ZyToken t = {};
  t.type = type;
  t.start = scanner.start;
  t.length = (size_t)(scanner.current - scanner.start);
  t.line = scanner.line;
  return t;
}

static ZyToken errorToken(const char *errorString) {
  ZyToken t = {};
  t.type = TOKEN_ERROR;
  t.start = errorString;
  t.length = strlen(errorString);
  t.line = scanner.line;
  return t;
}

static char advance() { return *(scanner.current++); }

static int match(char expected) {
  if (isAtEnd())
    return 0;
  if (*scanner.current != expected)
    return 0;
  scanner.current++;
  return 1;
}

static char peek() { return *scanner.current; }

static char peekNext() {
  if (isAtEnd())
    return '\0';
  return scanner.current[1];
}

static void skipWhitespace() {
  while (1) {
    char c = peek();
    switch (c) {
    case ' ':
    case '\t':
      advance();
      break;
    case '\n':
      scanner.line++;
      scanner.start_of_line = 1;
    default:
      return;
    }
  }
}

static ZyToken makeIndentation() {
  while (!isAtEnd() && peek() == ' ')
    advance();
  if (peek() == '\n') {
    return errorToken("Empty indentation line is invalid.");
  }

  return makeToken(TOKEN_INDENTATION);
}

static ZyToken string() {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\\')
      advance();
    if (peek() == '\n')
      scanner.line++;
  }

  if (isAtEnd())
    return errorToken("Unterminated string.");

  assert(peek() == '"');
  advance();

  return makeToken(TOKEN_STRING);
}

static ZyToken codepoint() {
  while (peek() != '\'' && !isAtEnd()) {
    if (peek() == '\\')
      advance();
    if (peek() == '\n')
      return makeToken(TOKEN_RETRY);
    advance();
  }

  if (isAtEnd())
    return errorToken("Unterminated codepoint literal.");

  assert(peek() == '\'');
  advance();

  return makeToken(TOKEN_CODEPOINT);
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

static int checkKeyword(size_t start, const char *rest, ZyTokenType type) {
  size_t length = strlen(rest);
  if (scanner.current - scanner.start == start + length &&
      memcmp(scanner.start, rest, length) == 0)
    return type;
  return TOKEN_IDENTIFIER;
}

static ZyTokenType identifierType() {
  switch (*scanner.start) {
  case 'a':
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