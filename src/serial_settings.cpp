#include "serial_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void trim_in_place(char *s)
{
  char *start = s;
  char *end = NULL;

  while(*start && isspace((unsigned char)*start))
  {
    start++;
  }

  if(start != s)
  {
    memmove(s, start, strlen(start) + 1);
  }

  if(*s == 0)
  {
    return;
  }

  end = s + strlen(s);
  while(end > s && isspace((unsigned char)*(end - 1)))
  {
    end--;
  }
  *end = 0;
}

static char *ltrim(char *s)
{
  while(*s && isspace((unsigned char)*s))
  {
    s++;
  }
  return s;
}

static bool starts_with_icase(const char *s, const char *prefix)
{
  while(*prefix)
  {
    if(tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
    {
      return false;
    }
    s++;
    prefix++;
  }
  return true;
}

static bool is_word_boundary(char c)
{
  return c == 0 || isspace((unsigned char)c);
}

static bool match_cmd(char *line, const char *cmd, char **out_arg)
{
  size_t len = strlen(cmd);

  if(!starts_with_icase(line, cmd))
  {
    return false;
  }

  if(!is_word_boundary(line[len]))
  {
    return false;
  }

  *out_arg = ltrim(line + len);
  return true;
}

static bool parse_u32(const char *s, uint32_t *out)
{
  char *end = NULL;
  int base = 10;

  if((s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
  {
    base = 16;
  }
  else
  {
    for(const char *p = s; *p; p++)
    {
      if(isalpha((unsigned char)*p))
      {
        base = 16;
        break;
      }
    }
  }

  unsigned long val = strtoul(s, &end, base);
  if(end == s || *end != 0)
  {
    return false;
  }

  *out = (uint32_t)val;
  return true;
}

static bool parse_bool(const char *s, bool *out)
{
  if((strcmp(s, "1") == 0) || (strcasecmp(s, "true") == 0) || (strcasecmp(s, "on") == 0))
  {
    *out = true;
    return true;
  }
  if((strcmp(s, "0") == 0) || (strcasecmp(s, "false") == 0) || (strcasecmp(s, "off") == 0))
  {
    *out = false;
    return true;
  }
  return false;
}

static bool parse_ip(const char *s, IPAddress *out)
{
  int a, b, c, d;
  if(sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d) != 4)
  {
    return false;
  }
  if((a < 0 || a > 255) || (b < 0 || b > 255) || (c < 0 || c > 255) || (d < 0 || d > 255))
  {
    return false;
  }
  *out = IPAddress((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d);
  return true;
}

static void print_hex_padded(Print *out, uint32_t val, uint8_t digits)
{
  for(int i = digits - 1; i >= 0; i--)
  {
    uint8_t nib = (val >> (i * 4)) & 0xF;
    char c = (nib < 10) ? ('0' + nib) : ('A' + (nib - 10));
    out->print(c);
  }
}

static void print_value(Print *out, const cfg_item_t *item)
{
  switch(item->type)
  {
    case CFG_U8:
      out->print(*(uint8_t *)item->ptr, DEC);
      break;
    case CFG_U16:
      out->print(*(uint16_t *)item->ptr, DEC);
      break;
    case CFG_U32:
      out->print(*(uint32_t *)item->ptr, DEC);
      break;
    case CFG_BOOL:
      out->print(*(bool *)item->ptr ? "1" : "0");
      break;
    case CFG_STRING:
      out->print((const char *)item->ptr);
      break;
    case CFG_COLOR:
      out->print("0x");
      print_hex_padded(out, *(uint32_t *)item->ptr, 6);
      break;
    case CFG_IP:
      {
        IPAddress *ip = (IPAddress *)item->ptr;
        out->print((*ip)[0]);
        out->print(".");
        out->print((*ip)[1]);
        out->print(".");
        out->print((*ip)[2]);
        out->print(".");
        out->print((*ip)[3]);
      }
      break;
  }
}

static const cfg_item_t *find_item(const cfg_item_t *items, size_t count, const char *key)
{
  for(size_t i = 0; i < count; i++)
  {
    if(strcasecmp(items[i].key, key) == 0)
    {
      return &items[i];
    }
  }
  return NULL;
}

static void print_error(Print *out, const char *msg)
{
  if(out)
  {
    out->print("ERROR: ");
    out->println(msg);
  }
}

static void print_ok(Print *out)
{
  if(out)
  {
    out->println("OK");
  }
}

bool serial_settings_handle_line(char *line,
                                 const cfg_item_t *items,
                                 size_t item_count,
                                 const serial_settings_ctx_t *ctx)
{
  char *arg = NULL;
  char *key = NULL;
  char *value = NULL;

  if(line == NULL || items == NULL || ctx == NULL)
  {
    return false;
  }

  trim_in_place(line);
  line = ltrim(line);
  if(*line == 0)
  {
    return false;
  }

  if(match_cmd(line, "get", &arg))
  {
    if(*arg == 0)
    {
      print_error(ctx->out, "Missing key");
      return true;
    }

    size_t arg_len = strlen(arg);
    bool is_prefix = arg_len > 2 && arg[arg_len - 2] == '.' && arg[arg_len - 1] == '*';
    if(is_prefix)
    {
      arg[arg_len - 2] = 0;
    }

    for(size_t i = 0; i < item_count; i++)
    {
      if(is_prefix)
      {
        if(strncasecmp(items[i].key, arg, strlen(arg)) != 0)
        {
          continue;
        }
      }
      else
      {
        if(strcasecmp(items[i].key, arg) != 0)
        {
          continue;
        }
      }

      if(ctx->out)
      {
        ctx->out->print(items[i].key);
        ctx->out->print("=");
        print_value(ctx->out, &items[i]);
        ctx->out->println();
      }

      if(!is_prefix)
      {
        break;
      }
    }

    return true;
  }

  if(match_cmd(line, "set", &arg))
  {
    if(!ctx->remote_on)
    {
      print_error(ctx->out, "Remote control not enabled");
      return true;
    }

    key = arg;
    value = strchr(arg, '=');
    if(value == NULL)
    {
      print_error(ctx->out, "Missing =");
      return true;
    }

    *value = 0;
    value++;
    key = ltrim(key);
    value = ltrim(value);
    trim_in_place(key);
    trim_in_place(value);

    const cfg_item_t *item = find_item(items, item_count, key);
    if(item == NULL)
    {
      print_error(ctx->out, "Unknown key");
      return true;
    }

    switch(item->type)
    {
      case CFG_U8:
      case CFG_U16:
      case CFG_U32:
      case CFG_COLOR:
        {
          uint32_t parsed = 0;
          if(!parse_u32(value, &parsed))
          {
            print_error(ctx->out, "Invalid number");
            return true;
          }
          if(item->min_val <= item->max_val)
          {
            if(parsed < item->min_val || parsed > item->max_val)
            {
              print_error(ctx->out, "Out of range");
              return true;
            }
          }
          if(item->type == CFG_U8)
          {
            *(uint8_t *)item->ptr = (uint8_t)parsed;
          }
          else if(item->type == CFG_U16)
          {
            *(uint16_t *)item->ptr = (uint16_t)parsed;
          }
          else
          {
            *(uint32_t *)item->ptr = (uint32_t)parsed;
          }
        }
        break;
      case CFG_BOOL:
        {
          bool b = false;
          if(!parse_bool(value, &b))
          {
            print_error(ctx->out, "Invalid bool");
            return true;
          }
          *(bool *)item->ptr = b;
        }
        break;
      case CFG_STRING:
        {
          size_t len = strlen(value);
          if(item->max_len > 0 && len > item->max_len)
          {
            print_error(ctx->out, "String too long");
            return true;
          }
          strncpy((char *)item->ptr, value, item->max_len);
          ((char *)item->ptr)[item->max_len] = 0;
        }
        break;
      case CFG_IP:
        {
          IPAddress ip;
          if(!parse_ip(value, &ip))
          {
            print_error(ctx->out, "Invalid IP");
            return true;
          }
          *(IPAddress *)item->ptr = ip;
        }
        break;
    }

    if(item->apply)
    {
      if(!item->apply(ctx->user, item))
      {
        print_error(ctx->out, "Apply failed");
        return true;
      }
    }

    print_ok(ctx->out);
    return true;
  }

  if(match_cmd(line, "save", &arg))
  {
    if(!ctx->remote_on)
    {
      print_error(ctx->out, "Remote control not enabled");
      return true;
    }

    if(ctx->on_save)
    {
      if(ctx->on_save(ctx->user))
      {
        print_ok(ctx->out);
      }
      else
      {
        print_error(ctx->out, "Save failed");
      }
    }
    else
    {
      print_error(ctx->out, "Save not supported");
    }
    return true;
  }

  if(match_cmd(line, "dump", &arg))
  {
    for(size_t i = 0; i < item_count; i++)
    {
      if(ctx->out)
      {
        ctx->out->print("set ");
        ctx->out->print(items[i].key);
        ctx->out->print("=");
        print_value(ctx->out, &items[i]);
        ctx->out->println();
      }
    }
    return true;
  }

  if(match_cmd(line, "help", &arg))
  {
    for(size_t i = 0; i < item_count; i++)
    {
      if(ctx->out)
      {
        ctx->out->println(items[i].key);
      }
    }
    return true;
  }

  return false;
}
