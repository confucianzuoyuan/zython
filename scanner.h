#pragma once

#include <stddef.h>
typedef enum {
  TOKEN_LEFT_PAREN,     /* `(` */
  TOKEN_RIGHT_PAREN,    /* `)` */
  TOKEN_LEFT_BRACE,     /* `{` */
  TOKEN_RIGHT_BRACE,    /* `}` */
  TOKEN_LEFT_SQUARE,    /* `[` */
  TOKEN_RIGHT_SQUARE,   /* `]` */
  TOKEN_COLON,          /* `:` */
  TOKEN_COMMA,          /* `,` */
  TOKEN_DOT,            /* `.` */
  TOKEN_MINUS,          /* `-` */
  TOKEN_PLUS,           /* `+` */
  TOKEN_SEMICOLON,      /* `;` */
  TOKEN_SOLIDUS,        /* `/` */
  TOKEN_DOUBLE_SOLIDUS, /* `//` */
  TOKEN_ASTERISK,       /* `*` */
  TOKEN_POW,            /* `**` */
  TOKEN_MODULO,         /* `%` */
  TOKEN_AT,             /* `@` */
  TOKEN_CARET,          /* `^` (xor) */
  TOKEN_AMPERSAND,      /* `&` (and) */
  TOKEN_PIPE,           /* `|` (or) */
  TOKEN_TILDE,          /* `~` (negate) */
  TOKEN_LEFT_SHIFT,     /* `<<` */
  TOKEN_RIGHT_SHIFT,    /* `>>` */
  TOKEN_BANG,           /* `!` */
  TOKEN_GREATER,        /* `>` */
  TOKEN_LESS,           /* `<` */
  TOKEN_ARROW,          /* `->` */
  TOKEN_WALRUS,         /* `:=` */

  /* Comparisons */
  TOKEN_GREATER_EQUAL, /* `>=` */
  TOKEN_LESS_EQUAL,    /* `<=` */
  TOKEN_BANG_EQUAL,    /* `!=` */
  TOKEN_EQUAL_EQUAL,   /* `==` */

  /* Assignments */
  TOKEN_EQUAL,          /* `=` */
  TOKEN_LSHIFT_EQUAL,   /* `<<=` */
  TOKEN_RSHIFT_EQUAL,   /* `>>=` */
  TOKEN_PLUS_EQUAL,     /* `+=` */
  TOKEN_MINUS_EQUAL,    /* `-=` */
  TOKEN_PLUS_PLUS,      /* `++` */
  TOKEN_MINUS_MINUS,    /* `--` */
  TOKEN_CARET_EQUAL,    /* `^=` */
  TOKEN_PIPE_EQUAL,     /* `|=` */
  TOKEN_AMP_EQUAL,      /* `&=` */
  TOKEN_SOLIDUS_EQUAL,  /* `/=` */
  TOKEN_ASTERISK_EQUAL, /* `*=` */
  TOKEN_POW_EQUAL,      /* `**=` */
  TOKEN_DSOLIDUS_EQUAL, /* `//=` */
  TOKEN_AT_EQUAL,       /* `@=` */
  TOKEN_MODULO_EQUAL,   /* `%=` */

  TOKEN_STRING,
  TOKEN_BIG_STRING,
  TOKEN_NUMBER,

  /*
   * Everything after this, up to indentation,
   * consists of alphanumerics.
   */
  TOKEN_IDENTIFIER, /* 标识符 */
  TOKEN_AND,        /* and */
  TOKEN_CLASS,      /* class */
  TOKEN_DEF,        /* def */
  TOKEN_DEL,        /* del */
  TOKEN_ELSE,       /* else */
  TOKEN_FALSE,      /* False */
  TOKEN_FINALLY,    /* finally */
  TOKEN_FOR,        /* for */
  TOKEN_IF,         /* if */
  TOKEN_IMPORT,     /* import */
  TOKEN_IN,         /* in */
  TOKEN_IS,         /* is */
  TOKEN_NONE,       /* None */
  TOKEN_NOT,        /* not */
  TOKEN_OR,         /* or */
  TOKEN_ELIF,       /* elif */
  TOKEN_PASS,       /* pass */
  TOKEN_RETURN,     /* return */
  TOKEN_SUPER,      /* super */
  TOKEN_TRUE,       /* True */
  TOKEN_WHILE,      /* while */
  TOKEN_TRY,        /* try */
  TOKEN_EXCEPT,     /* except */
  TOKEN_RAISE,      /* raise */
  TOKEN_BREAK,      /* break */
  TOKEN_CONTINUE,   /* continue */
  TOKEN_AS,         /* as */
  TOKEN_FROM,       /* from */
  TOKEN_LAMBDA,     /* lambda */
  TOKEN_ASSERT,     /* assert */
  TOKEN_YIELD,      /* yield */
  TOKEN_ASYNC,      /* async */
  TOKEN_AWAIT,      /* await */
  TOKEN_WITH,       /* with */
  TOKEN_GLOBAL,     /* global */

  TOKEN_PREFIX_B,
  TOKEN_PREFIX_F,
  TOKEN_PREFIX_R,

  TOKEN_INDENTATION, /* 缩进相关 */

  TOKEN_EOL,
  TOKEN_RETRY,
  TOKEN_ERROR,
  TOKEN_EOF,

  TOKEN_ELLIPSIS, /* ... */
} ZyTokenType;

typedef struct {
  ZyTokenType type;
  const char *start;
  size_t length;
  size_t line;
  const char *linePtr;
  size_t col;
  size_t literalWidth;
} ZyToken;

typedef struct {
  const char *start;
  const char *cur;
  const char *linePtr;
  size_t line;
  int startOfLine;
  int hasUnget;
  ZyToken unget;
} ZyScanner;

extern ZyScanner zy_initScanner(const char *src);
extern ZyToken zy_scanToken(ZyScanner *);
extern void zy_ungetToken(ZyScanner *, ZyToken token);
extern void zy_rewindScanner(ZyScanner *, ZyScanner to);
extern ZyScanner zy_tellScanner(ZyScanner *);
extern void printToken(ZyToken t);