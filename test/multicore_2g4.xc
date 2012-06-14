// RUN: xcc %s.xn %s -o %t1.xe
// RUN: axe %t1.xe > %t2.txt
// RUN: cmp %t2.txt %s.expect

#include <print.h>
#include <platform.h>

int main()
{
  par {
    on stdcore[0]: printstr("hello\n");
    on stdcore[1]: printstr("hello\n");
    on stdcore[2]: printstr("hello\n");
    on stdcore[3]: printstr("hello\n");
    on stdcore[4]: printstr("hello\n");
    on stdcore[5]: printstr("hello\n");
    on stdcore[6]: printstr("hello\n");
    on stdcore[7]: printstr("hello\n");
  }
  return 0;
}
