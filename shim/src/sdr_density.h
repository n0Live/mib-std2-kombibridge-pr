// Display density (dpi) of a video sink in the ServiceDiscoveryResponse: the phone sizes its UI by
// it. MediaSinkService field 4 = VideoConfiguration (repeated), whose field 5 = density.
// Standalone (no libc, no shim types), host-tested in tests/sdr.
#ifndef SDR_DENSITY_H
#define SDR_DENSITY_H

static int sdrd_varint_in(const unsigned char* p,unsigned n,unsigned* i,unsigned* v){
  unsigned r=0,s=0;
  while(*i<n && s<35){ unsigned char b=p[(*i)++]; r|=(unsigned)(b&0x7f)<<s; s+=7; if(!(b&0x80)){ *v=r; return 1; } }
  return 0;
}
static unsigned char* sdrd_varint_out(unsigned char* o,unsigned char* end,unsigned v){
  do{ if(o>=end) return 0; unsigned char b=(unsigned char)(v&0x7f); v>>=7; *o++=(unsigned char)(b|(v?0x80:0)); }while(v);
  return o;
}
// One field from p[*i]: its wire type, start and end; payload of a length-delimited one. 0 = bad.
static int sdrd_field(const unsigned char* p,unsigned n,unsigned* i,unsigned* f,unsigned* wt,
                      unsigned* v,const unsigned char** pl,unsigned* pn){
  unsigned key;
  if(!sdrd_varint_in(p,n,i,&key)) return 0;
  *f=key>>3; *wt=key&7; *pl=0; *pn=0; *v=0;
  if(*wt==0) return sdrd_varint_in(p,n,i,v);
  if(*wt==2){ if(!sdrd_varint_in(p,n,i,v) || *v>n-*i) return 0; *pl=p+*i; *pn=*v; *i+=*v; return 1; }
  if(*wt==5){ if(n-*i<4) return 0; *i+=4; return 1; }
  if(*wt==1){ if(n-*i<8) return 0; *i+=8; return 1; }
  return 0;
}

// The density of the first VideoConfiguration of a MediaSinkService; 0 if there is none.
static unsigned sdr_density_of(const unsigned char* sink,unsigned n){
  unsigned i=0,f,wt,v,pn; const unsigned char* pl;
  while(i<n){
    if(!sdrd_field(sink,n,&i,&f,&wt,&v,&pl,&pn)) return 0;
    if(f==4 && wt==2){
      unsigned j=0,g,w2,x,qn; const unsigned char* q;
      while(j<pn){ if(!sdrd_field(pl,pn,&j,&g,&w2,&x,&q,&qn)) return 0; if(g==5 && w2==0) return x; }
      return 0;
    }
  }
  return 0;
}

// The MediaSinkService `sink` with the density of every VideoConfiguration set to dpi (added where
// it is missing); the other fields unchanged and in order. Returns the new length, 0 on bad input
// or when out (cap bytes) is too small.
static unsigned sdr_set_density(const unsigned char* sink,unsigned n,unsigned char* out,unsigned cap,unsigned dpi){
  unsigned char* o=out; unsigned char* end=out+cap;
  unsigned i=0,f,wt,v,pn,fs; const unsigned char* pl;
  while(i<n){
    fs=i;
    if(!sdrd_field(sink,n,&i,&f,&wt,&v,&pl,&pn)) return 0;
    if(f==4 && wt==2){
      unsigned char vc[256]; unsigned char* t=vc; unsigned char* tend=vc+sizeof vc;
      unsigned j=0,g,w2,x,qn,gs; const unsigned char* q;
      while(j<pn){
        gs=j;
        if(!sdrd_field(pl,pn,&j,&g,&w2,&x,&q,&qn)) return 0;
        if(g==5 && w2==0) continue;                       // the old density: replaced below
        if(t+(j-gs)>tend) return 0;
        for(unsigned k=gs;k<j;k++) *t++=pl[k];
      }
      t=sdrd_varint_out(t,tend,(5u<<3)|0); if(!t) return 0;
      t=sdrd_varint_out(t,tend,dpi); if(!t) return 0;
      o=sdrd_varint_out(o,end,(4u<<3)|2); if(!o) return 0;
      o=sdrd_varint_out(o,end,(unsigned)(t-vc)); if(!o) return 0;
      if(o+(t-vc)>end) return 0;
      for(unsigned char* k=vc;k<t;k++) *o++=*k;
    } else {
      if(o+(i-fs)>end) return 0;
      for(unsigned k=fs;k<i;k++) *o++=sink[k];
    }
  }
  return (unsigned)(o-out);
}

#endif
