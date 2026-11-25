// Host test of shim/src/app_insets.h: the per-app insets table and the aa_nav_app text.
#include <cstdio>
#include "../../shim/src/app_insets.h"

static int failed;
#define CHECK(what, cond) do { if (!(cond)) { failed++; std::printf("  FAIL %s (line %d)\n", what, __LINE__); } } while (0)

static bool is(const unsigned* v,unsigned a,unsigned b,unsigned c,unsigned d){ return v[0]==a&&v[1]==b&&v[2]==c&&v[3]==d; }

int main(){
  const char* table =
    "# per-app insets\n"
    "google 150 260 470 470\r\n"
    "  waze\t100 200 300 400\n"
    "yandex 1,2,3,4   # trailing comment\n"
    "2gis 10 20 30\n"                              // only three numbers: ignored
    "default 5 6 7 8\n"
    "googleX 9 9 9 9\n";
  unsigned v[4]={0,0,0,0};
  CHECK("google", appins_lookup(table,"google",v) && is(v,150,260,470,470));
  CHECK("waze (indent, tab)", appins_lookup(table,"waze",v) && is(v,100,200,300,400));
  CHECK("yandex (commas, comment)", appins_lookup(table,"yandex",v) && is(v,1,2,3,4));
  CHECK("2gis bad line -> default", appins_lookup(table,"2gis",v) && is(v,5,6,7,8));
  CHECK("unlisted app -> default", appins_lookup(table,"other",v) && is(v,5,6,7,8));
  CHECK("prefix is not a match", appins_lookup("googleX 9 9 9 9\n","google",v)==0);
  CHECK("no default, unlisted", appins_lookup("google 1 2 3 4","waze",v)==0);
  CHECK("last line without newline", appins_lookup("waze 1 2 3 4","waze",v) && is(v,1,2,3,4));
  CHECK("empty app name", appins_lookup(table,"",v)==0);
  CHECK("empty table", appins_lookup("","google",v)==0);
  CHECK("huge numbers stay bounded", appins_lookup("google 99999999999 1 2 3","google",v) && v[0]<1000000u);

  char a[16];
  appins_app("yandex\n",a,sizeof a);   CHECK("app name", appins_same(a,"yandex"));
  appins_app("  2gis\n",a,sizeof a);   CHECK("app name with digits", appins_same(a,"2gis"));
  appins_app("",a,sizeof a);           CHECK("no app", a[0]==0);
  appins_app("waze",a,3);              CHECK("cut to the buffer", appins_same(a,"wa"));
  std::puts(failed ? "TESTS FAILED" : "all tests passed");
  return failed ? 1 : 0;
}
