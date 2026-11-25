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

  // templates and guidance lines
  const char* tt =
    "default narrow 140 260 470 470\n"
    "default medium 140 260 400 400\n"
    "default wide   160 260 70 70\n"
    "yandex 140 200 460 460\n"
    "yandex medium 140 200 400 400\n"
    "yandex guide 1 150 2 2\n"
    "yandex medium guide 140 150 400 400\n"
    "google wide 140 265 130 130\n"
    "google wide guide 140 265 400 130\n"
    "hide-card\n";
  CHECK("default narrow", appins_lookup_tpl(tt,"waze","narrow",0,v) && is(v,140,260,470,470));
  CHECK("default wide", appins_lookup_tpl(tt,"waze","wide",0,v) && is(v,160,260,70,70));
  CHECK("no app seen: default", appins_lookup_tpl(tt,"","medium",0,v) && is(v,140,260,400,400));
  CHECK("app line without template, any template", appins_lookup_tpl(tt,"yandex","wide",0,v) && is(v,140,200,460,460));
  CHECK("app template beats app plain", appins_lookup_tpl(tt,"yandex","medium",0,v) && is(v,140,200,400,400));
  CHECK("guide line only while guiding", appins_lookup_tpl(tt,"yandex","narrow",1,v) && is(v,1,150,2,2));
  CHECK("template+guide is the most specific", appins_lookup_tpl(tt,"yandex","medium",1,v) && is(v,140,150,400,400));
  CHECK("google wide guiding", appins_lookup_tpl(tt,"google","wide",1,v) && is(v,140,265,400,130));
  CHECK("google wide not guiding", appins_lookup_tpl(tt,"google","wide",0,v) && is(v,140,265,130,130));
  CHECK("google other template: default", appins_lookup_tpl(tt,"google","narrow",0,v) && is(v,140,260,470,470));
  CHECK("no template chosen: only plain lines", appins_lookup_tpl(tt,"waze","",0,v)==0);
  CHECK("unknown template word: plain lines only", appins_lookup_tpl(tt,"yandex","huge",0,v) && is(v,140,200,460,460));
  CHECK("hide-card found", appins_hide_card(tt));
  CHECK("no hide-card", !appins_hide_card(table));
  CHECK("hide-card in a comment is not one", !appins_hide_card("# hide-card\n"));
  CHECK("old lookup ignores template lines", appins_lookup("google wide 1 2 3 4\n","google",v)==0);
  CHECK("last of equal lines wins", appins_lookup_tpl("waze 1 1 1 1\nwaze 2 2 2 2\n","waze","",0,v) && is(v,2,2,2,2));

  char a[16];
  appins_app("yandex\n",a,sizeof a);   CHECK("app name", appins_same(a,"yandex"));
  appins_app("  2gis\n",a,sizeof a);   CHECK("app name with digits", appins_same(a,"2gis"));
  appins_app("",a,sizeof a);           CHECK("no app", a[0]==0);
  appins_app("waze",a,3);              CHECK("cut to the buffer", appins_same(a,"wa"));
  std::puts(failed ? "TESTS FAILED" : "all tests passed");
  return failed ? 1 : 0;
}
