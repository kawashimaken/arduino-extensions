#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Strip HTTP chunk framing and ICY metadata before feeding the audio decoder.
// Independent of Arduino, Ethernet and SPI; bounded buffers, no heap.
class RadioStream {
 public:
  enum class Result:uint8_t { Skip,Audio,Done,Error };
  enum class Error:uint8_t { None,Status,Header,Type,Encoding,Chunk };
  void reset() { *this=RadioStream(); }
  bool headersComplete() const { return body_; }
  // Once HTTP headers are validated, unframed endless audio can bypass the
  // byte parser. Never use this for chunked, ICY metadata or finite bodies.
  bool plainAudio() const {
    return body_ && !done_ && error_==Error::None &&
           !chunked_ && !metaInterval_ && !hasLength_;
  }
  bool done() const { return done_; }
  Error error() const { return error_; }
  uint16_t statusCode() const { return status_; }
  Result consume(uint8_t byte) {
    if(error_!=Error::None) return Result::Error;
    if(done_) return Result::Done;
    if(!body_) {
      if(++headerBytes_>4096) return fail(Error::Header);
      if(byte=='\r') return Result::Skip;
      if(byte!='\n') { append(byte); return Result::Skip; }
      line_[length_]=0;
      if(!status_) {
        if(overflow_) return fail(Error::Status);
        const char* code=nullptr;
        if(!strncmp(line_,"HTTP/1.0 ",9)||!strncmp(line_,"HTTP/1.1 ",9)) code=line_+9;
        else if(!strncmp(line_,"ICY ",4)) code=line_+4;
        if(!code || strlen(code)<3 || code[0]<'1'||code[0]>'5'||code[1]<'0'||code[1]>'9'||
           code[2]<'0'||code[2]>'9'||(code[3]&&code[3]!=' ')) return fail(Error::Status);
        status_=(code[0]-'0')*100+(code[1]-'0')*10+code[2]-'0';
        if(status_!=200) return fail(Error::Status);
      } else if(!length_&&!overflow_) {
        if(!mpeg_||(chunked_&&hasLength_)) return fail(Error::Type);
        body_=true; audioLeft_=metaInterval_;
        if(hasLength_&&!bodyLeft_) done_=true;
      } else if(!header()) return Result::Error;
      clearLine(); return done_?Result::Done:Result::Skip;
    }
    if(chunked_) {
      if(chunkState_==ChunkState::Size || chunkState_==ChunkState::Trailer) {
        if(++headerBytes_>8192) return fail(Error::Chunk);
        if(byte=='\r') return Result::Skip;
        if(byte!='\n') { append(byte); return Result::Skip; }
        line_[length_]=0;
        if(overflow_) return fail(Error::Chunk);
        if(chunkState_==ChunkState::Trailer) {
          if(!length_) done_=true;
        } else {
          uint32_t value=0; uint8_t digits=0;
          for(uint8_t i=0;line_[i]&&line_[i]!=';';++i) {
            const char c=line_[i];
            const int8_t n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if(n<0 || ++digits>6) return fail(Error::Chunk);
            value=(value<<4)|n;
          }
          if(!digits) return fail(Error::Chunk);
          chunkLeft_=value; chunkState_=value?ChunkState::Data:ChunkState::Trailer;
        }
        clearLine(); return done_?Result::Done:Result::Skip;
      }
      if(chunkState_==ChunkState::Cr) {
        if(byte!='\r') return fail(Error::Chunk);
        chunkState_=ChunkState::Lf; return Result::Skip;
      }
      if(chunkState_==ChunkState::Lf) {
        if(byte!='\n') return fail(Error::Chunk);
        chunkState_=ChunkState::Size; headerBytes_=0; return Result::Skip;
      }
      if(--chunkLeft_==0) chunkState_=ChunkState::Cr;
    }
    if(hasLength_&&--bodyLeft_==0) done_=true;
    if(metaInterval_) {
      if(metadataLeft_) { --metadataLeft_; return Result::Skip; }
      if(!audioLeft_) {
        metadataLeft_=uint16_t(byte)*16; audioLeft_=metaInterval_; return Result::Skip;
      }
      --audioLeft_;
    }
    return Result::Audio;
  }
 private:
  enum class ChunkState:uint8_t { Size,Data,Cr,Lf,Trailer };
  char line_[128]={};
  uint32_t metaInterval_=0,audioLeft_=0,chunkLeft_=0,bodyLeft_=0;
  uint16_t status_=0,headerBytes_=0,metadataLeft_=0;
  uint8_t length_=0;
  Error error_=Error::None;
  ChunkState chunkState_=ChunkState::Size;
  bool body_=false,done_=false,overflow_=false,mpeg_=false,chunked_=false,hasLength_=false;
  void append(uint8_t c) {
    if(!c) { error_=Error::Header; return; }
    if(length_<sizeof(line_)-1) line_[length_++]=char(c); else overflow_=true;
  }
  void clearLine() { length_=0; overflow_=false; }
  Result fail(Error e) { error_=e; return Result::Error; }
  static char lower(char c) { return c>='A'&&c<='Z'?c+('a'-'A'):c; }
  static bool equal(const char* a,const char* b) {
    while(*a&&*b&&lower(*a)==lower(*b)) { ++a; ++b; } return *a==*b;
  }
  static bool prefix(const char* a,const char* b) {
    while(*b) { if(lower(*a++)!=*b++) return false; } return true;
  }
  // Exact media type, optionally followed by ";parameters".
  static bool mediaType(const char* value,const char* type) {
    if(!prefix(value,type)) return false;
    const char next=value[strlen(type)]; return next==0||next==';';
  }
  static bool number(const char* s,uint32_t& out) {
    if(!*s) return false;
    uint32_t value=0;
    for(;*s;++s) {
      if(*s<'0'||*s>'9'||value>(UINT32_MAX-uint8_t(*s-'0'))/10) return false;
      value=value*10+(*s-'0');
    }
    out=value; return true;
  }
  bool header() {
    char* colon=strchr(line_,':'); if(!colon) { fail(Error::Header); return false; }
    *colon=0; char* value=colon+1;
    while(*value==' '||*value=='\t') ++value;
    char* end=value+strlen(value);
    while(end>value&&(end[-1]==' '||end[-1]=='\t')) *--end=0;
    const bool relevant=equal(line_,"content-type")||equal(line_,"icy-metaint")||
      equal(line_,"transfer-encoding")||equal(line_,"content-length")||equal(line_,"content-encoding");
    if(overflow_) { if(relevant) { fail(Error::Header); return false; } return true; }
    if(equal(line_,"content-type")) {
      // MP3, or ADTS AAC / HE-AAC; VS1053 decodes both formats natively.
      mpeg_=mediaType(value,"audio/mpeg")||mediaType(value,"audio/mp3")||
            mediaType(value,"audio/aac")||mediaType(value,"audio/aacp");
    } else if(equal(line_,"icy-metaint")) {
      if(!number(value,metaInterval_)||metaInterval_>1000000) { fail(Error::Header); return false; }
    } else if(equal(line_,"transfer-encoding")) {
      if(!equal(value,"chunked")) { fail(Error::Encoding); return false; } chunked_=true;
    } else if(equal(line_,"content-encoding")&&!equal(value,"identity")) {
      fail(Error::Encoding); return false;
    } else if(equal(line_,"content-length")) {
      uint32_t n;
      if(!number(value,n)||(hasLength_&&n!=bodyLeft_)) { fail(Error::Header); return false; }
      hasLength_=true; bodyLeft_=n;
    }
    return true;
  }
};
