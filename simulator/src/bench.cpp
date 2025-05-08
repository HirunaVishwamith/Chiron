#define SHOW_TERMINAL

#include "simulator.h"

int main() {
  simulator bench;

  int step=0;

  bench.init();
  printf("bench inititated!\n");
  cout << endl;
  cout <<  flush;

  while(step<1000) {
    // cout<<std::hex<<bench.prev_pc<<endl;
    bench.step();
    step++;
  }

  cout<<bench.accumulatedChars0<<endl;
  cout<<bench.accumulatedChars1<<endl;
}
