// Per-app content insets for the cluster video (Android Auto's map on the AID). Pure functions, no
// libc: used by cluster_sink.h and the host test tests/shim/test_app_insets.cpp.
//
// libkb_dm.so writes the navigation app it sees on the cluster to /dev/shmem/aa_nav_app (a name
// from native/src/nav_app_classify.h and a newline). The table, /tsd/var/aatokombi/app-insets, is
// edited on the unit:
//   # comment
//   google   150 260 470 470
//   waze     150 260 470 470
//   yandex   150 260 470 470
//   2gis     150 260 470 470
//   default  150 260 470 470      (any app without a line of its own; optional)
// The numbers are top bottom left right, in pixels of the phone's 1280x720 frame, the same as the
// insets files. Yandex Navigator and Yango are both "yandex".
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

// The insets for `app` from the table text: its own line, else the "default" line. 1 = found.
static int appins_lookup(const char* table,const char* app,unsigned v[4]){
  unsigned dflt[4]; int haveDflt=0;
  const char* p=table;
  if(!app || !*app) return 0;
  while(*p){
    char w[16]; unsigned n=0, t[4];
    while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
    if(*p=='#'){ while(*p && *p!='\n') p++; continue; }
    while(*p && *p!=' ' && *p!='\t' && *p!='\r' && *p!='\n'){ if(n+1<sizeof w) w[n++]=*p; p++; }
    w[n]=0;
    if(n && appins_numbers(p,t)){
      if(appins_same(w,app)){ for(unsigned k=0;k<4;k++) v[k]=t[k]; return 1; }
      if(appins_same(w,"default")){ for(unsigned k=0;k<4;k++) dflt[k]=t[k]; haveDflt=1; }
    }
    while(*p && *p!='\n') p++;                    // the rest of a line, also a bad one
  }
  if(haveDflt){ for(unsigned k=0;k<4;k++) v[k]=dflt[k]; return 1; }
  return 0;
}

#endif
