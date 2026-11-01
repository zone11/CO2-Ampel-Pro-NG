#include "json_parser.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
  const char *start;
  const char *p;
  json_value_fn cb;
  void *user;
  const char *error;
  char key[JSON_MAX_KEY + 1];
  char value[JSON_MAX_VALUE + 1];
} parser_t;

static bool fail(parser_t *ps, const char *msg)
{
  if(ps->error == NULL)
  {
    ps->error = msg;
  }
  return false;
}

static void skip_ws(parser_t *ps)
{
  while((*ps->p == ' ') || (*ps->p == '\t') || (*ps->p == '\r') || (*ps->p == '\n'))
  {
    ps->p++;
  }
}

static int hex_digit(char c)
{
  if((c >= '0') && (c <= '9')) { return c - '0'; }
  if((c >= 'a') && (c <= 'f')) { return c - 'a' + 10; }
  if((c >= 'A') && (c <= 'F')) { return c - 'A' + 10; }
  return -1;
}

// Reads a string (p on the opening quote) into out; non-ASCII \u escapes become '?'
static bool parse_string(parser_t *ps, char *out, size_t size)
{
  size_t n = 0;

  ps->p++; //"
  while(*ps->p != '"')
  {
    char c = *ps->p++;
    if(c == 0)
    {
      return fail(ps, "unterminated string");
    }
    if((unsigned char)c < 0x20)
    {
      return fail(ps, "control character in string");
    }
    if(c == '\\')
    {
      c = *ps->p++;
      switch(c)
      {
        case '"': case '\\': case '/': break;
        case 'b': c = '\b'; break;
        case 'f': c = '\f'; break;
        case 'n': c = '\n'; break;
        case 'r': c = '\r'; break;
        case 't': c = '\t'; break;
        case 'u':
        {
          unsigned int v = 0;
          for(int i = 0; i < 4; i++)
          {
            int d = hex_digit(*ps->p++);
            if(d < 0)
            {
              return fail(ps, "invalid \\u escape");
            }
            v = (v << 4) | (unsigned int)d;
          }
          c = ((v >= 0x20) && (v < 0x80)) ? (char)v : '?';
          break;
        }
        default:
          return fail(ps, "invalid escape");
      }
    }
    if(n >= (size - 1))
    {
      return fail(ps, "string too long");
    }
    out[n++] = c;
  }
  ps->p++; //"
  out[n] = 0;
  return true;
}

static bool parse_number(parser_t *ps)
{
  const char *s = ps->p;

  if(*ps->p == '-') { ps->p++; }
  if((*ps->p < '0') || (*ps->p > '9')) { return fail(ps, "invalid number"); }
  while((*ps->p >= '0') && (*ps->p <= '9')) { ps->p++; }
  if(*ps->p == '.')
  {
    ps->p++;
    if((*ps->p < '0') || (*ps->p > '9')) { return fail(ps, "invalid number"); }
    while((*ps->p >= '0') && (*ps->p <= '9')) { ps->p++; }
  }
  if((*ps->p == 'e') || (*ps->p == 'E'))
  {
    ps->p++;
    if((*ps->p == '+') || (*ps->p == '-')) { ps->p++; }
    if((*ps->p < '0') || (*ps->p > '9')) { return fail(ps, "invalid number"); }
    while((*ps->p >= '0') && (*ps->p <= '9')) { ps->p++; }
  }
  size_t n = (size_t)(ps->p - s);
  if(n > JSON_MAX_VALUE)
  {
    return fail(ps, "number too long");
  }
  memcpy(ps->value, s, n);
  ps->value[n] = 0;
  return true;
}

static bool literal(parser_t *ps, const char *word)
{
  size_t n = strlen(word);
  if(strncmp(ps->p, word, n) != 0)
  {
    return fail(ps, "invalid value");
  }
  ps->p += n;
  return true;
}

static bool parse_object(parser_t *ps, size_t key_len, int depth);

// One value for the key in ps->key
static bool parse_value(parser_t *ps, size_t key_len, int depth)
{
  json_type_t type;

  skip_ws(ps);
  switch(*ps->p)
  {
    case '{':
      return parse_object(ps, key_len, depth + 1);
    case '[':
      return fail(ps, "arrays not supported");
    case '"':
      if(!parse_string(ps, ps->value, sizeof(ps->value))) { return false; }
      type = JSON_STRING;
      break;
    case 't':
      if(!literal(ps, "true")) { return false; }
      strcpy(ps->value, "true");
      type = JSON_BOOL;
      break;
    case 'f':
      if(!literal(ps, "false")) { return false; }
      strcpy(ps->value, "false");
      type = JSON_BOOL;
      break;
    case 'n':
      if(!literal(ps, "null")) { return false; }
      ps->value[0] = 0;
      type = JSON_NULL;
      break;
    default:
      if(!parse_number(ps)) { return false; }
      type = JSON_NUMBER;
      break;
  }

  if(!ps->cb(ps->user, ps->key, type, ps->value))
  {
    return fail(ps, "stopped");
  }
  return true;
}

// Object members; key_len = length of the key prefix of the enclosing objects
static bool parse_object(parser_t *ps, size_t key_len, int depth)
{
  char name[JSON_MAX_KEY + 1];

  if(depth > JSON_MAX_DEPTH)
  {
    return fail(ps, "nested too deep");
  }
  ps->p++; //{
  skip_ws(ps);
  if(*ps->p == '}')
  {
    ps->p++;
    return true;
  }

  while(1)
  {
    skip_ws(ps);
    if(*ps->p != '"')
    {
      return fail(ps, "expected key");
    }
    if(!parse_string(ps, name, sizeof(name)))
    {
      return false;
    }
    size_t n = strlen(name);
    size_t len = key_len + ((key_len > 0) ? 1 : 0) + n;
    if((n == 0) || (len > JSON_MAX_KEY))
    {
      return fail(ps, (n == 0) ? "empty key" : "key too long");
    }
    if(key_len > 0)
    {
      ps->key[key_len] = '.';
    }
    memcpy(&ps->key[len - n], name, n + 1);

    skip_ws(ps);
    if(*ps->p != ':')
    {
      return fail(ps, "expected ':'");
    }
    ps->p++;
    if(!parse_value(ps, len, depth))
    {
      return false;
    }
    ps->key[key_len] = 0; //restore the key of the enclosing object

    skip_ws(ps);
    if(*ps->p == ',')
    {
      ps->p++;
      continue;
    }
    if(*ps->p == '}')
    {
      ps->p++;
      return true;
    }
    return fail(ps, "expected ',' or '}'");
  }
}

bool json_parse(const char *text, json_value_fn cb, void *user, char *err, size_t err_size)
{
  parser_t ps;

  ps.start = text;
  ps.p = text;
  ps.cb = cb;
  ps.user = user;
  ps.error = NULL;
  ps.key[0] = 0;

  skip_ws(&ps);
  if(*ps.p != '{')
  {
    fail(&ps, "expected '{'");
  }
  else if(parse_object(&ps, 0, 1))
  {
    skip_ws(&ps);
    if(*ps.p != 0)
    {
      fail(&ps, "text after the object");
    }
  }

  if(ps.error != NULL)
  {
    if(err != NULL)
    {
      snprintf(err, err_size, "at %u: %s", (unsigned int)(ps.p - ps.start), ps.error);
    }
    return false;
  }
  return true;
}
