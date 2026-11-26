// Host test of shim/src/sdr_density.h: the main display's density in the ServiceDiscoveryResponse.
#include <stdio.h>
#include <string.h>
#include "../../shim/src/sdr_density.h"
static int failed=0;
#define CHECK(what,cond) do{ if(cond) printf("%-52s ok\n",what); else { printf("%-52s FAIL\n",what); failed=1; } }while(0)
int main(){
  // MediaSinkService: 1 available_type=3, 4 VideoConfiguration{1 res=1, 2 fps=2, 5 density=140, 6 depth=3},
  // 4 VideoConfiguration{1 res=2, 5 density=160}, 9 = 7
  const unsigned char s[]={0x08,0x03, 0x22,0x09,0x08,0x01,0x10,0x02,0x28,0x8c,0x01,0x30,0x03,
                           0x22,0x05,0x08,0x02,0x28,0xa0,0x01, 0x48,0x07};
  CHECK("the first config's density", sdr_density_of(s,sizeof s)==140);
  unsigned char o[64]; unsigned n=sdr_set_density(s,sizeof s,o,sizeof o,200);
  CHECK("rewritten", n>0);
  CHECK("density is the new one", sdr_density_of(o,n)==200);
  const unsigned char want[]={0x08,0x03, 0x22,0x09,0x08,0x01,0x10,0x02,0x30,0x03,0x28,0xc8,0x01,
                              0x22,0x05,0x08,0x02,0x28,0xc8,0x01, 0x48,0x07};
  CHECK("every config, other fields unchanged", n==sizeof want && memcmp(o,want,n)==0);
  // a config without a density gets one
  const unsigned char s2[]={0x22,0x02,0x08,0x01};
  n=sdr_set_density(s2,sizeof s2,o,sizeof o,125);
  const unsigned char w2[]={0x22,0x04,0x08,0x01,0x28,0x7d};
  CHECK("missing density added", n==sizeof w2 && memcmp(o,w2,n)==0);
  CHECK("no config: density 0", sdr_density_of((const unsigned char*)"\x08\x03",2)==0);
  CHECK("cut input: 0", sdr_set_density(s,5,o,sizeof o,200)==0);
  CHECK("output too small: 0", sdr_set_density(s,sizeof s,o,10,200)==0);
  CHECK("bad length: 0", sdr_set_density((const unsigned char*)"\x22\x7f\x08",3,o,sizeof o,200)==0);
  printf(failed ? "TESTS FAILED\n" : "all tests passed\n");
  return failed;
}
