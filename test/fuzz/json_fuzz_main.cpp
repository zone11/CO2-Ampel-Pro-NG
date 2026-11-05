// Reads one JSON text per line and prints what the parser delivers (see test/fuzz/json_fuzz.py)
#include <stdio.h>
#include <string>
#include <iostream>

#include "json_parser.h"

static bool print_value(void *, const char *key, json_type_t type, const char *value)
{
  printf("%d\t", (int)type);
  for(const char *p = key; *p; p++) { printf("%02x", (unsigned char)*p); }
  printf("\t");
  for(const char *p = value; *p; p++) { printf("%02x", (unsigned char)*p); }
  printf("\n");
  return true;
}

int main(void)
{
  std::string line;
  while(std::getline(std::cin, line))
  {
    char err[64];
    bool ok = json_parse(line.c_str(), print_value, NULL, err, sizeof(err));
    printf("%s\n", ok ? "OK" : "FAIL");
  }
  return 0;
}
