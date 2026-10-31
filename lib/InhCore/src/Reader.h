#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
namespace inh {
struct TextSource {
    virtual ~TextSource()=default;
    virtual uint32_t size() const=0;
    virtual int byte(uint32_t offset)=0;
};
struct Page { uint32_t start=0, next=0; std::vector<std::string> lines; };
// Pure layout: never calls the display, writes flash or waits for a button.
inline Page paginate(TextSource& s,uint32_t start,unsigned columns,unsigned rows) {
    if(!columns||!rows||columns>128||rows>64||start>s.size()) throw std::runtime_error("bad layout");
    Page p;p.start=start;p.next=start;
    while(p.next<s.size()&&p.lines.size()<rows) {
        std::string line;unsigned cells=0;uint32_t lastSpace=0;size_t spaceBytes=0;
        while(p.next<s.size()) {
            uint32_t at=p.next;int first=s.byte(at);if(first<0)throw std::runtime_error("book read failed");
            if(first=='\r'){++p.next;continue;}
            if(first=='\n'){++p.next;break;}
            if(cells>=columns) {
                if(lastSpace>start&&lastSpace>at-line.size()) {p.next=lastSpace;line.resize(spaceBytes);}
                break;
            }
            unsigned n= first<128?1:((first&0xe0)==0xc0?2:((first&0xf0)==0xe0?3:((first&0xf8)==0xf0?4:0)));
            uint32_t cp=n?first&((1u<<(7-n))-1):0;bool ok=n&&at+n<=s.size();
            if(n==1)cp=first;
            for(unsigned k=1;ok&&k<n;k++){int ch=s.byte(at+k);ok=ch>=0&&(ch&0xc0)==0x80;cp=(cp<<6)|(ch&63);}
            if(n>1&&(cp<(n==2?0x80u:n==3?0x800u:0x10000u)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)))ok=false;
            if(!ok){line+='?';++p.next;}
            else {for(unsigned k=0;k<n;k++)line+=char(s.byte(at+k));p.next+=n;}
            if(first==' '||first=='\t'){if(first=='\t')line.back()=' ';lastSpace=p.next;spaceBytes=line.size()-1;}
            ++cells;
        }
        p.lines.push_back(line);
    }
    if(start<s.size()&&p.next<=start)throw std::runtime_error("layout made no progress");
    return p;
}
inline uint32_t crc32(const uint8_t* data,size_t n,uint32_t state=0xffffffffu) {
    for(size_t i=0;i<n;i++){state^=data[i];for(int k=0;k<8;k++)state=(state>>1)^(0xedb88320u&uint32_t(-int(state&1)));}return state;
}
struct IndexHeader {uint32_t magic=0x49484e32,version=1,bookSize=0,bookCrc=0,columns=40,rows=8,count=0;};
class PageIndex {
public:
    std::vector<uint32_t> starts;
    void begin(){starts={0};}
    bool extend(TextSource& s,unsigned cols,unsigned rows) {
        if(starts.empty())begin();
        const auto page=paginate(s,starts.back(),cols,rows);
        if(page.next>=s.size())return false;
        if(starts.size()>=100000)throw std::runtime_error("too many pages");
        starts.push_back(page.next);return true;
    }
    size_t pageAt(uint32_t offset)const {
        if(starts.empty())return 0;
        size_t lo=0,hi=starts.size();
        while(lo+1<hi){size_t m=(lo+hi)/2;if(starts[m]<=offset)lo=m;else hi=m;}return lo;
    }
    bool validate(uint32_t bookSize)const {
        if(starts.empty()||starts[0]!=0)return false;
        for(size_t i=1;i<starts.size();i++)if(starts[i]<=starts[i-1]||starts[i]>=bookSize)return false;
        return true;
    }
};
enum class Gesture { None, Single, Double, Triple, Hold };
class Button {
    bool raw_=false,stable_=false,held_=false,armed_=true;
    uint32_t changed_=0,pressed_=0,released_=0;unsigned clicks_=0;
public:
    void reset(uint32_t now,bool down){raw_=stable_=down;armed_=!down;held_=false;clicks_=0;changed_=pressed_=released_=now;}
    Gesture tick(uint32_t now,bool down) {
        if(down!=raw_){raw_=down;changed_=now;}
        if(raw_!=stable_&&uint32_t(now-changed_)>=30) {
            stable_=raw_;
            if(stable_){pressed_=now;held_=false;}
            else {if(!armed_)armed_=true;else if(!held_){++clicks_;released_=now;}held_=false;}
        }
        if(!armed_)return Gesture::None;
        if(stable_&&!held_&&uint32_t(now-pressed_)>=1300){held_=true;clicks_=0;return Gesture::Hold;}
        if(!stable_&&clicks_&&(clicks_>=3||uint32_t(now-released_)>=360)) {
            auto c=clicks_;clicks_=0;return c>=3?Gesture::Triple:c==2?Gesture::Double:Gesture::Single;
        }
        return Gesture::None;
    }
};
}
