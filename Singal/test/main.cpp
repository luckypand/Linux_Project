#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <iostream>
using namespace std;

int main() {
   raise(SIGFPE);

   return 0;
}
