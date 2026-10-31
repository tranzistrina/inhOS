#include "Glagol.h"
#include <fstream>
#include <iostream>
#include <sstream>
int main(int argc, char **argv) {
  for (int j = 1; j < argc; j++) {
    std::ifstream f(argv[j]);
    if (!f)
      return 1;
    std::ostringstream s;
    s << f.rdbuf();
    inh::VM v([](const std::string &n, const std::vector<inh::Value> &) {
      if (n == "адрес")
        return inh::Value(std::string("192.168.4.1"));
      return inh::Value(0);
    });
    v.start(inh::compile(s.str()));
    for (int i = 0; i < 10000 && v.running(); i++)
      v.tick(1);
    if (!v.error().empty()) {
      std::cerr << argv[j] << ": " << v.error() << '\n';
      return 1;
    }
    v.stop();
    std::cout << "PASS " << argv[j] << '\n';
  }
}
