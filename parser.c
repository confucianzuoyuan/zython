#include "parser.h"
#include "scanner.h"

/**
 * @brief Token parser state.
 *
 * The parser is fairly simplistic, requiring essentially
 * no lookahead. 'previous' is generally the currently-parsed
 * token: whatever was matched by @ref match. 'current' is the
 * token to be parsed, and can be examined with @ref check.
 */
typedef struct {
  ZyToken current; /**< @brief Token to be parsed. */
  ZyToken
      previous;  /**< @brief Last token matched, consumed, or advanced over. */
  char hadError; /**< @brief Flag indicating if the parser encountered an error.
                  */
  unsigned int eatingWhitespace; /**< @brief Depth of whitespace-ignoring parse
                                    functions. */
} Parser;

/**
 * @brief Parse precedence ladder.
 *
 * Lower values (values listed first) bind more loosely than
 * higher values (values listed later).
 */
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  /**<  `=` */
  PREC_COMMA,       /**<  `,` */
  PREC_MUST_ASSIGN, /**<  Multple assignment target   */
  PREC_CAN_ASSIGN,  /**<  Single assignment target, inside parens */
  PREC_DEL_TARGET,  /**<  Like above, but del target list */
  PREC_TERNARY,     /**<  TrueBranch `if` Condition `else` FalseBranch */
  PREC_OR,          /**<  `or` */
  PREC_AND,         /**<  `and` */
  PREC_NOT,         /**<  `not` */
  PREC_COMPARISON,  /**<  `< > <= >= in not in` */
  PREC_BITOR,       /**<  `|` */
  PREC_BITXOR,      /**<  `^` */
  PREC_BITAND,      /**<  `&` */
  PREC_SHIFT,       /**<  `<< >>` */
  PREC_SUM,         /**<  `+ -` */
  PREC_TERM,        /**<  `* / %` */
  PREC_FACTOR,      /**<  `+ - ~ !` */
  PREC_EXPONENT,    /**<  `**` */
  PREC_PRIMARY,     /**<  `. () []` */
} Precedence;

/**
 * @brief Expression type.
 *
 * Determines how an expression should be compiled.
 */
typedef enum {
  EXPR_NORMAL,     /**< This expression can not be an assignment target. */
  EXPR_CAN_ASSIGN, /**< This expression may be an assignment target, check for
                      assignment operators at the end. */
  EXPR_ASSIGN_TARGET, /**< This expression is definitely an assignment target or
                         chained to one. */
  EXPR_DEL_TARGET,    /**< This expression is in the target list of a 'del'
                         statement. */
  EXPR_METHOD_CALL, /**< This expression is the parameter list of a method call;
                       only used by @ref dot and @ref call */
  EXPR_CLASS_PARAMETERS,
} ExpressionType;

struct RewindState;
struct GlobalState;

/**
 * @brief Subexpression parser function.
 *
 * Used by the parse rule table for infix and prefix expression
 * parser functions. The argument passed is the @ref ExpressionType
 * to compile the expression as.
 */
typedef void (*ParseFn)(struct GlobalState *, int, struct RewindState *);

/**
 * @brief Parse rule table entry.
 *
 * Maps tokens to prefix and infix rules. Precedence values here
 * are for the infix parsing.
 */
typedef struct {
  ParseFn prefix; /**< @brief Parse function to call when this token appears at
                     the start of an expression. */
  ParseFn infix;  /**< @brief Parse function to call when this token appears
                     after an expression. */
  Precedence precedence; /**< @brief Precedence ordering for Pratt parsing, @ref
                            Precedence */
} ParseRule;

/**
 * @brief Function compilation type.
 *
 * Determines the context of the function being compiled,
 * as different kinds of functions have different semantics.
 */
typedef enum {
  TYPE_FUNCTION,    /**< Normal 'def' function. */
  TYPE_MODULE,      /**< Top level of a script. */
  TYPE_METHOD,      /**< Class method with `self` binding. */
  TYPE_INIT,        /**< Class \__init__ */
  TYPE_LAMBDA,      /**< Lambda expression body, must be a single expression. */
  TYPE_STATIC,      /**< Static class method, no `self` binding. */
  TYPE_CLASS,       /**< Class body, not a normal series of declarations. */
  TYPE_CLASSMETHOD, /**< Class method, binds first argument to the class. */
  TYPE_COROUTINE,   /**< `await def` function. */
  TYPE_COROUTINE_METHOD, /**< `await def` class method. */
} FunctionType;

/**
 * @brief Linked list of indices.
 *
 * Primarily used to track the indices of class properties
 * so that they can be referenced again later. @ref ind
 * will be the index of an identifier constant.
 */
struct IndexWithNext {
  size_t ind;                 /**< @brief Index of an identifier constant. */
  struct IndexWithNext *next; /**< @brief Linked list next pointer. */
};

/**
 * @brief Tracks 'break' and 'continue' statements.
 */
struct LoopExit {
  int offset;    /**< @brief Offset of the jump expression to patch. */
  ZyToken token; /**< @brief Token for this exit statement, so its location can
                     be printed in an error message. */
};

/**
 * @brief Compiler emit and parse state prior to this expression.
 *
 * Used to rewind the parser for ternary and comma expressions.
 */
typedef struct RewindState {
  ZyScanner oldScanner; /**< @brief Scanner cursor state. */
  Parser oldParser;     /**< @brief Previous/current tokens. */
} RewindState;

typedef struct GlobalState {
  Parser parser;     /**< @brief Parser state */
  ZyScanner scanner; /**< @brief Scanner state */
} GlobalState;

static int isMethod(int type) {
  return type == TYPE_METHOD || type == TYPE_INIT ||
         type == TYPE_COROUTINE_METHOD;
}

static int isCoroutine(int type) {
  return type == TYPE_COROUTINE || type == TYPE_COROUTINE_METHOD;
}

