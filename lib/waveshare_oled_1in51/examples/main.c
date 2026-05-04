#include "test.h"
#include <signal.h> //signal()
#include <stdlib.h> //exit()
#include <string.h>


void Handler(int signo) {
  // System Exit
  printf("\r\nHandler:exit\r\n");
  DEV_ModuleExit();

  exit(0);
}

int main(int argc, char *argv[]) {
  // Exception handling:ctrl + c
  signal(SIGINT, Handler);

  if (argc != 2 || strcmp(argv[1], "1.51") != 0) {
    printf("usage: sudo ./main 1.51\r\n");
    exit(1);
  }

  printf("1.51 OLED Module\r\n");
  return OLED_1in51_test();
}
