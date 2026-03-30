#include "log.hpp"

int main() {
  //   sum(4, 2, 3, 4, 5);
  Log log;
  // log.Enable(ONEFILE);
  log.Enable(CLASSIFY);
  // log.logmessage(INFO, "helloworld");
  // log.logmessage(WARNING, "helloworld");
  // log.logmessage(ERROR, "helloworld");
  // log.logmessage(FATAL, "helloworld");
  // log.logmessage(DEBUG, "helloworld");

  log(INFO, "helloworld");
  log(WARNING, "helloworld");
  log(ERROR, "helloworld");
  log(FATAL, "helloworld");
  log(DEBUG, "helloworld");
  return 0;
}