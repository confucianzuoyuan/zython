#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

static ZyToken number(ZyScanner *scanner, char c) {
  if (c == '0') {
    /* 16进制 */
    if (peek(scanner) == 'x' || peek(scanner) == 'X') {
      advance(scanner);
      while (isDigit(peek(scanner)) ||
             (peek(scanner) >= 'a' && peek(scanner) <= 'f') ||
             (peek(scanner) >= 'A' && peek(scanner) <= 'F') ||
             (peek(scanner) == '_'))
        advance(scanner);

      return makeToken(scanner, TOKEN_NUMBER);
    }
    /* 2进制 */
    else if (peek(scanner) == 'b' || peek(scanner) == 'B') {
      advance(scanner);
      while (peek(scanner) == '0' || peek(scanner) == '1' ||
             peek(scanner) == '_')
        advance(scanner);
      return makeToken(scanner, TOKEN_NUMBER);
    }

    /* 8进制，必须是0o开头，0123这种不合法 */
    if (peek(scanner) == 'o' || peek(scanner) == 'O') {
      advance(scanner);
      while ((peek(scanner) >= '0' && peek(scanner) <= '7') ||
             (peek(scanner) == '_'))
        advance(scanner);
      return makeToken(scanner, TOKEN_NUMBER);
    }
    /* 否则，要不就是十进制，要么就是0.123这种浮点数 */
  }

  /* 10进制 */
  while (isDigit(peek(scanner)) || peek(scanner) == '_')
    advance(scanner);

  /* 浮点数 */
  if (peek(scanner) == '.' && isDigit(peekNext(scanner, 1))) {
    advance(scanner);
    while (isDigit(peek(scanner)))
      advance(scanner);
  }

  /* 科学记数法 */
  if (peek(scanner) == 'e' || peek(scanner) == 'E') {
    advance(scanner);
    if (peek(scanner) == '+' || peek(scanner) == '-')
      advance(scanner);
    while (isDigit(peek(scanner))) {
      advance(scanner);
    }
  }

  return makeToken(scanner, TOKEN_NUMBER);
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
  case 'b':
    if (scanner->cur - scanner->start > 1) {
      return checkKeyword(scanner, 1, "reak", TOKEN_BREAK);
    } else if (scanner->start[1] == '\'' || scanner->start[1] == '"') {
      return TOKEN_PREFIX_B;
    }
    break;
  case 'c':
    if (scanner->cur - scanner->start > 1) {
      switch (scanner->start[1]) {
      case 'l':
        return checkKeyword(scanner, 2, "ass", TOKEN_CLASS);
      case 'o':
        return checkKeyword(scanner, 2, "ntinue", TOKEN_CONTINUE);
      }
    }
    break;
  case 'd':
    if (scanner->cur - scanner->start > 1) {
      switch (scanner->start[1]) {
      case 'e':
        if (scanner->cur - scanner->start > 2) {
          switch (scanner->start[2]) {
          case 'f':
            return checkKeyword(scanner, 3, "", TOKEN_DEF);
          case 'l':
            return checkKeyword(scanner, 3, "", TOKEN_DEL);
          }
        }
        break;
      }
    }
    break;
  case 'e':
    if (scanner->cur - scanner->start > 1) {
      switch (scanner->start[1]) {
      case 'l':
        if (scanner->cur - scanner->start > 2) {
          switch (scanner->start[2]) {
          case 's':
            return checkKeyword(scanner, 3, "e", TOKEN_ELSE);
          case 'i':
            return checkKeyword(scanner, 3, "f", TOKEN_ELIF);
          }
        }
        break;
      case 'x':
        return checkKeyword(scanner, 2, "cept", TOKEN_EXCEPT);
      }
    }
  case 'f':
    if (scanner->cur - scanner->start > 1) {
      switch (scanner->start[1]) {
      case 'i':
        return checkKeyword(scanner, 2, "nally", TOKEN_FINALLY);
      case 'o':
        return checkKeyword(scanner, 2, "r", TOKEN_FOR);
      case 'r':
        return checkKeyword(scanner, 2, "om", TOKEN_FROM);
      }
    } else if (scanner->start[1] == '\'' || scanner->start[1] == '"') {
      return TOKEN_PREFIX_F;
    }
  case 'F':
    return checkKeyword(scanner, 1, "alse", TOKEN_FALSE);
  case 'i':
    if (scanner->cur - scanner->start > 1) {
      switch (scanner->start[1]) {
      case 'f':
        return checkKeyword(scanner, 2, "", TOKEN_IF);
      case 'n':
        return checkKeyword(scanner, 2, "", TOKEN_IN);
      case 'm':
        return checkKeyword(scanner, 2, "port", TOKEN_IMPORT);
      case 's':
        return checkKeyword(scanner, 2, "", TOKEN_IS);
      }
      break;
    }
  case 'g':
    return checkKeyword(scanner, 1, "lobal", TOKEN_GLOBAL);
  case 'l':
    return checkKeyword(scanner, 1, "ambda", TOKEN_LAMBDA);
  case 'n':
    return checkKeyword(scanner, 1, "ot", TOKEN_NOT);
  case 'N':
    return checkKeyword(scanner, 1, "one", TOKEN_NONE);
  case 'o':
    return checkKeyword(scanner, 1, "r", TOKEN_OR);
  case 'p':
    return checkKeyword(scanner, 1, "ass", TOKEN_PASS);
  case 'r':
    if (scanner->cur - scanner->start > 1) {
      switch (scanner->start[1]) {
      case 'e':
        return checkKeyword(scanner, 2, "turn", TOKEN_RETURN);
      case 'a':
        return checkKeyword(scanner, 2, "ise", TOKEN_RAISE);
      }
    } else if (scanner->start[1] == '\'' || scanner->start[1] == '"') {
      return TOKEN_PREFIX_R;
    }
  case 's':
    return checkKeyword(scanner, 1, "uper", TOKEN_SUPER);
  case 't':
    return checkKeyword(scanner, 1, "ry", TOKEN_TRY);
  case 'T':
    return checkKeyword(scanner, 1, "rue", TOKEN_TRUE);
  case 'w':
    if (scanner->cur - scanner->start > 1) {
      switch (scanner->start[1]) {
      case 'h':
        return checkKeyword(scanner, 2, "ile", TOKEN_WHILE);
      case 'i':
        return checkKeyword(scanner, 2, "th", TOKEN_WITH);
      }
    }
    break;
  case 'y':
    return checkKeyword(scanner, 1, "ield", TOKEN_YIELD);
  }

  return TOKEN_IDENTIFIER;
}

static ZyToken identifier(ZyScanner *scanner) {
  while (isAlpha(peek(scanner)) || isDigit(peek(scanner)) ||
         (unsigned char)peek(scanner) > 0x7F)
    advance(scanner);

  return makeToken(scanner, identifierType(scanner));
}

void zy_ungetToken(ZyScanner *scanner, ZyToken token) {
  if (scanner->hasUnget)
    abort();

  scanner->hasUnget = 1;
  scanner->unget = token;
}

ZyScanner zy_tellScanner(ZyScanner *scanner) { return *scanner; }

void zy_rewindScanner(ZyScanner *scanner, ZyScanner to) { *scanner = to; }

ZyToken zy_scanToken(ZyScanner *scanner) {
  if (scanner->hasUnget) {
    scanner->hasUnget = 0;
    return scanner->unget;
  }

  /* 如果是行的开头，则看一下有没有缩进 */
  if (scanner->startOfLine && peek(scanner) == ' ' || peek(scanner) == '\t') {
    scanner->start = scanner->cur;
    return makeIndentation(scanner);
  }

  /* 跳过空白符 */
  skipWhitespace(scanner);

  /* 跳过注释 */
  if (peek(scanner) == '#')
    while (peek(scanner) != '\n' && !isAtEnd(scanner))
      advance(scanner);

  scanner->start = scanner->cur;

  if (isAtEnd(scanner))
    return makeToken(scanner, TOKEN_EOF);

  char c = advance(scanner);

  if (c == '\n') {
    ZyToken out;
    if (scanner->startOfLine) {
      /* 忽略掉完整的空行 */
      out = makeToken(scanner, TOKEN_RETRY);
    } else {
      scanner->startOfLine = 1;
      out = makeToken(scanner, TOKEN_EOL);
    }
    nextLine(scanner);
    return out;
  }

  if (c == '\\' && peek(scanner) == '\n') {
    advance(scanner);
    nextLine(scanner);
    return makeToken(scanner, TOKEN_RETRY);
  }

  scanner->startOfLine = 0;

  if (isAlpha(c) || (unsigned char)c > 0x7F)
    return identifier(scanner);
  if (isDigit(c))
    return number(scanner, c);

  switch (c) {
  case '(':
    return makeToken(scanner, TOKEN_LEFT_PAREN);
  case ')':
    return makeToken(scanner, TOKEN_RIGHT_PAREN);
  case '{':
    return makeToken(scanner, TOKEN_LEFT_BRACE);
  case '}':
    return makeToken(scanner, TOKEN_RIGHT_BRACE);
  case '[':
    return makeToken(scanner, TOKEN_LEFT_SQUARE);
  case ']':
    return makeToken(scanner, TOKEN_RIGHT_SQUARE);
  case ',':
    return makeToken(scanner, TOKEN_COMMA);
  case ';':
    return makeToken(scanner, TOKEN_SEMICOLON);
  case '~':
    return makeToken(scanner, TOKEN_TILDE);
  case '.':
    if (peek(scanner) == '.') {
      if (peekNext(scanner, 1) == '.') {
        advance(scanner);
        advance(scanner);
        return makeToken(scanner, TOKEN_ELLIPSIS);
      } else {
        return makeToken(scanner, TOKEN_DOT);
      }
    } else {
      return makeToken(scanner, TOKEN_DOT);
    }
  case ':':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_WALRUS);
    } else {
      return makeToken(scanner, TOKEN_COLON);
    }
  case '^':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_CARET_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_CARET);
    }
  case '<':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_LESS_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_LESS);
    }
  case '>':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_GREATER_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_GREATER);
    }
  case '=':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_EQUAL_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_EQUAL);
    }
  case '!':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_BANG_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_BANG);
    }
  case '|':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_PIPE_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_PIPE);
    }
  case '&':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_AMP_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_AMPERSAND);
    }
  case '-':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_MINUS_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_MINUS);
    }
  case '+':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_PLUS_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_PLUS);
    }
  case '/':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_SOLIDUS_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_SOLIDUS);
    }
  case '*':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_ASTERISK_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_ASTERISK);
    }
  case '%':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_MODULO_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_MODULO);
    }
  case '@':
    if (match(scanner, '=')) {
      return makeToken(scanner, TOKEN_AT_EQUAL);
    } else {
      return makeToken(scanner, TOKEN_AT);
    }
  case '"':
    return string(scanner, '"');
  case '\'':
    return string(scanner, '\'');
  }

  return errorToken(scanner, "Unexpected character.");
}

void printToken(ZyToken t) {
  switch (t.type) {
  case TOKEN_YIELD:
  case TOKEN_AND:
  case TOKEN_OR:
  case TOKEN_WITH:
  case TOKEN_FOR:
  case TOKEN_GLOBAL:
  case TOKEN_TRY:
  case TOKEN_EXCEPT:
  case TOKEN_AS:
  case TOKEN_ASYNC:
  case TOKEN_AWAIT:
  case TOKEN_ASSERT:
  case TOKEN_BREAK:
  case TOKEN_CLASS:
  case TOKEN_COMMA:
  case TOKEN_DEF:
  case TOKEN_ELIF:
  case TOKEN_ELSE:
  case TOKEN_FALSE:
  case TOKEN_TRUE:
  case TOKEN_FINALLY:
  case TOKEN_FROM:
  case TOKEN_IMPORT:
  case TOKEN_CONTINUE:
  case TOKEN_DEL:
  case TOKEN_IF:
  case TOKEN_IN:
  case TOKEN_IS:
  case TOKEN_NONE:
  case TOKEN_NOT:
  case TOKEN_PASS:
  case TOKEN_RETURN:
  case TOKEN_SUPER:
  case TOKEN_WHILE:
  case TOKEN_RAISE:
  case TOKEN_LAMBDA:
    printf("\033[38;5;214m关键字\033[0m：");
    break;
  case TOKEN_LEFT_PAREN:
  case TOKEN_RIGHT_PAREN:
  case TOKEN_LEFT_BRACE:
  case TOKEN_RIGHT_BRACE:
  case TOKEN_LEFT_SQUARE:
  case TOKEN_RIGHT_SQUARE:
  case TOKEN_COLON:
  case TOKEN_DOT:
  case TOKEN_MINUS:
  case TOKEN_PLUS:
  case TOKEN_SEMICOLON:
  case TOKEN_SOLIDUS:
  case TOKEN_DOUBLE_SOLIDUS:
  case TOKEN_ASTERISK:
  case TOKEN_POW:
  case TOKEN_MODULO:
  case TOKEN_AT:
  case TOKEN_CARET:
  case TOKEN_AMPERSAND:
  case TOKEN_PIPE:
  case TOKEN_TILDE:
  case TOKEN_LEFT_SHIFT:
  case TOKEN_RIGHT_SHIFT:
  case TOKEN_BANG:
  case TOKEN_GREATER:
  case TOKEN_LESS:
  case TOKEN_ARROW:
  case TOKEN_WALRUS:
  case TOKEN_GREATER_EQUAL:
  case TOKEN_LESS_EQUAL:
  case TOKEN_BANG_EQUAL:
  case TOKEN_EQUAL_EQUAL:
  case TOKEN_EQUAL:
  case TOKEN_LSHIFT_EQUAL:
  case TOKEN_RSHIFT_EQUAL:
  case TOKEN_PLUS_EQUAL:
  case TOKEN_MINUS_EQUAL:
  case TOKEN_PLUS_PLUS:
  case TOKEN_MINUS_MINUS:
  case TOKEN_CARET_EQUAL:
  case TOKEN_PIPE_EQUAL:
  case TOKEN_AMP_EQUAL:
  case TOKEN_SOLIDUS_EQUAL:
  case TOKEN_ASTERISK_EQUAL:
  case TOKEN_POW_EQUAL:
  case TOKEN_DSOLIDUS_EQUAL:
  case TOKEN_AT_EQUAL:
  case TOKEN_MODULO_EQUAL:
  case TOKEN_ELLIPSIS:
    printf("\033[34m运算符\033[0m：");
    break;
  case TOKEN_STRING:
    printf("\033[33m字符串\033[0m：");
    break;
  case TOKEN_BIG_STRING:
    printf("大字符串：");
    break;
  case TOKEN_NUMBER:
    printf("\033[32m数值\033[0m：");
    break;
  case TOKEN_IDENTIFIER:
    printf("\033[31m标识符\033[0m：");
    break;
  case TOKEN_PREFIX_B:
  case TOKEN_PREFIX_F:
  case TOKEN_PREFIX_R:
  case TOKEN_INDENTATION:
    printf("缩进：");
    break;
  case TOKEN_ERROR:
    printf("错误：");
    break;
  case TOKEN_EOL:
    printf("end of line");
    break;
  case TOKEN_RETRY:
    printf("retry");
    break;
  case TOKEN_EOF:
    printf("\033[35mEnd Of File\033[0m");
    break;
  }
  printf("%.*s\r\n", (int)(t.length), t.start);
}