#include "token.h"

const char *token_kind_name(TokenKind kind)
{
    switch (kind) {
    case TOK_EOF:         return "EOF";
    case TOK_ERROR:       return "ERROR";
    case TOK_NEWLINE:     return "NEWLINE";
    case TOK_IDENT:       return "IDENT";

    case TOK_INT:         return "INT";
    case TOK_FLOAT:       return "FLOAT";
    case TOK_CHAR:        return "CHAR";
    case TOK_STRING:      return "STRING";
    case TOK_BYTES:       return "BYTES";

    case TOK_KW_TYPE:     return "type";
    case TOK_KW_STRUCT:   return "struct";
    case TOK_KW_ENUM:     return "enum";
    case TOK_KW_FN:       return "fn";
    case TOK_KW_LET:      return "let";
    case TOK_KW_IF:       return "if";
    case TOK_KW_THEN:     return "then";
    case TOK_KW_ELSE:     return "else";
    case TOK_KW_MATCH:    return "match";
    case TOK_KW_IMPORT:   return "import";
    case TOK_KW_TRUE:     return "true";
    case TOK_KW_FALSE:    return "false";

    case TOK_LPAREN:      return "(";
    case TOK_RPAREN:      return ")";
    case TOK_LBRACE:      return "{";
    case TOK_RBRACE:      return "}";
    case TOK_LBRACKET:    return "[";
    case TOK_RBRACKET:    return "]";
    case TOK_COMMA:       return ",";
    case TOK_SEMI:        return ";";
    case TOK_COLON:       return ":";
    case TOK_DOT:         return ".";
    case TOK_UNDERSCORE:  return "_";

    case TOK_ARROW:       return "->";
    case TOK_FAT_ARROW:   return "=>";
    case TOK_ASSIGN:      return "=";

    case TOK_PLUS:        return "+";
    case TOK_MINUS:       return "-";
    case TOK_STAR:        return "*";
    case TOK_SLASH:       return "/";
    case TOK_PERCENT:     return "%";

    case TOK_EQ:          return "==";
    case TOK_NE:          return "!=";
    case TOK_LT:          return "<";
    case TOK_LE:          return "<=";
    case TOK_GT:          return ">";
    case TOK_GE:          return ">=";

    case TOK_AND_AND:     return "&&";
    case TOK_OR_OR:       return "||";
    case TOK_BANG:        return "!";

    case TOK_AMP:         return "&";
    case TOK_PIPE:        return "|";
    case TOK_CARET:       return "^";
    case TOK_TILDE:       return "~";
    case TOK_SHL:         return "<<";
    case TOK_SHR:         return ">>";
    }

    return "UNKNOWN";
}
