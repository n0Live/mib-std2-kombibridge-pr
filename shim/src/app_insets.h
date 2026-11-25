// Per-app content insets for the cluster video (Android Auto's map on the AID). Pure functions, no
// libc: used by cluster_sink.h and the host test tests/shim/test_app_insets.cpp.
//
// libkb_dm.so writes the navigation app it sees on the cluster to /dev/shmem/aa_nav_app (a name
// from native/src/nav_app_classify.h and a newline). The table, /tsd/var/aatokombi/app-insets, is
// the owner's, edited on the unit:
//   # comment
//   google   150 260 470 470
//   waze     150 260 470 470
//   yandex   150 260 470 470
//   2gis     150 260 470 470
//   default  150 260 470 470      (any app without a line of its own; optional)
// The numbers are top bottom left right, in pixels of the phone's 1280x720 frame, the same as the
// insets files. Yandex Navigator and Yango are both "yandex". Templates and guidance lines: see
// appins_lookup_tpl; the template chosen on the wheel is in /tsd/var/aatokombi/insets-template.
#ifndef APP_INSETS_H
#define APP_INSETS_H

// the first word of the aa_nav_app text ([a-z0-9]), "" when there is none
static void appins_app(const char* text,char* out,unsigned outsz){
  unsigned n=0;
  if(!outsz) return;
  while(*text==' '||*text=='\t') text++;
  while(n+1<outsz && ((*text>='a'&&*text<='z')||(*text>='0'&&*text<='9'))) out[n++]=*text++;
  out[n]=0;
}

static int appins_same(const char* a,const char* b){
  while(*a && *a==*b){ a++; b++; }
  return *a==*b;
}

// up to the end of the line: 4 numbers (separated by spaces, tabs or commas); 1 = got 4
static int appins_numbers(const char*& p,unsigned v[4]){
  unsigned k=0;
  while(k<4){
    while(*p==' '||*p=='\t'||*p==',') p++;
    if(*p<'0'||*p>'9') break;
    unsigned x=0; while(*p>='0'&&*p<='9'){ if(x<100000u) x=x*10u+(unsigned)(*p-'0'); p++; }
    v[k++]=x;
  }
  return k==4;
}

// One line of the table: "<app> [narrow|medium|wide] [guide] top bottom left right". A line without
// a template is for all three, a line without "guide" is for guidance and without it, a "guide" line
// only for the time of route guidance. The most specific line wins (template 2, guide 1); among lines
// as specific, the last one. The app's lines win over the "default" lines.
// tpl: "narrow", "medium" or "wide" ("" = none chosen: only lines without a template match).
// Returns 1 when a line matched.
static int appins_word(const char*& p,char* w,unsigned wsz){
  unsigned n=0;
  while(*p==' '||*p=='\t') p++;
  while(*p && *p!=' ' && *p!='\t' && *p!='\r' && *p!='\n' && *p!=','){ if(n+1<wsz) w[n++]=*p; p++; }
  w[n]=0; return (int)n;
}
static int appins_lookup_tpl(const char* table,const char* app,const char* tpl,int guiding,unsigned v[4]){
  int best[2]={-1,-1};                            // [0] the app's best score, [1] the default's
  unsigned got[2][4];
  const char* p=table;
  if(!app || !*app) app="default";
  if(!tpl) tpl="";
  while(*p){
    char w[16]; unsigned t[4];
    while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    if(*p=='#'){ while(*p && *p!='\n') p++; continue; }
    if(appins_word(p,w,sizeof w)){
      int who = appins_same(w,app) ? 0 : appins_same(w,"default") ? 1 : -1;
      int score=0, ok=1;
      const char* q=p; char x[16];
      // optional template, then optional "guide", then the numbers
      const char* r=q; appins_word(r,x,sizeof x);
      if(appins_same(x,"narrow")||appins_same(x,"medium")||appins_same(x,"wide")){
        if(!appins_same(x,tpl)) ok=0;
        score+=2; q=r;
        r=q; appins_word(r,x,sizeof x);
      }
      if(appins_same(x,"guide")){ if(!guiding) ok=0; score+=1; q=r; }
      p=q;
      if(who>=0 && appins_numbers(p,t) && ok && score>=best[who]){
        best[who]=score; for(unsigned k=0;k<4;k++) got[who][k]=t[k];
      }
    }
    while(*p && *p!='\n') p++;                   // the rest of a line, also a bad one
  }
  int who = best[0]>=0 ? 0 : best[1]>=0 ? 1 : -1;
  if(who<0) return 0;
  for(unsigned k=0;k<4;k++) v[k]=got[who][k];
  return 1;
}

// The insets for `app` from the table text: its own line, else the "default" line. 1 = found.
// (Lines with a template or "guide" do not match here.)
static int appins_lookup(const char* table,const char* app,unsigned v[4]){
  if(!app || !*app) return 0;
  return appins_lookup_tpl(table,app,"",0,v);
}

#endif
