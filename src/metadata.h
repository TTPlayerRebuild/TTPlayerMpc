#pragma once
#include "io.h"
#include <shlwapi.h>
#include <cmath>
#include <cwchar>
namespace ttp::mpc {
struct Tag {std::string key;Bytes value;DWORD flags{};};
class Tags {
    uint64_t audio_end_{};
    Bytes suffix_;
    FILETIME filetime_{};
    bool have_filetime_{};
public:
    std::vector<Tag> items;
    bool dirty{},rg_dirty{};
    static bool equal(const std::string& a,const char *b){return b&&_stricmp(a.c_str(),b)==0;}
    Tag *find(const char *k){for(auto& t:items)if(equal(t.key,k))return &t;return nullptr;}
    void set(const char *k,const wchar_t *v,bool edited=true) {
        require(k&&*k,E_INVALIDARG);
        std::string key(k);require(key.size()<=255,E_INVALIDARG);
        for(auto c:key)require(static_cast<unsigned char>(c)>=32&&static_cast<unsigned char>(c)<=126&&c!='=',E_INVALIDARG);
        auto it=std::find_if(items.begin(),items.end(),[&](const Tag& t){return equal(t.key,k);});
        if(v&&*v){auto s=utf8(v);require(s.size()<=16*1024*1024,E_INVALIDARG);Tag t{key,Bytes(s.begin(),s.end()),0};if(it==items.end())items.push_back(std::move(t));else *it=std::move(t);}
        else if(it!=items.end())items.erase(it);
        if(edited){
            dirty=true;if(!_strnicmp(k,"replaygain_",11))rg_dirty=true;
            // A removed APE field must not reappear from an older ID3v1 copy.
            // Edited values are stored losslessly in UTF-8 APEv2; clear only
            // the corresponding obsolete field in the retained legacy tag.
            if(!suffix_.empty()){
                for(auto f:{std::pair<const char*,int>{"Title",3},{"Artist",33},{"Album",63},{"Year",93},{"Comment",97}})
                    if(equal(key,f.first))std::fill_n(suffix_.begin()+f.second,f.second==93?4:(f.second==97?28:30),BYTE(0));
                if(equal(key,"Track")){suffix_[125]=0;suffix_[126]=0;}
                if(equal(key,"Genre"))suffix_[127]=255;
            }
        }
    }
    void clear(){items.erase(std::remove_if(items.begin(),items.end(),[](const Tag&t){return !(t.flags&6);}),items.end());suffix_.clear();dirty=rg_dirty=true;}
    std::vector<std::wstring> values(const Tag& t) const {
        std::vector<std::wstring> result;size_t from=0;
        while(from<t.value.size()){auto end=std::find(t.value.begin()+from,t.value.end(),0);result.push_back(wide(std::string(t.value.begin()+from,end)));from=size_t(end-t.value.begin())+1;}
        if(result.empty())result.push_back(L"");return result;
    }
    std::wstring value(const Tag& t) const {std::wstring result;for(const auto& v:values(t)){if(!result.empty())result+=L"; ";result+=v;}return result;}
    void load(Input& in) {
        auto saved=tell(in.stream.p);audio_end_=in.size;
        WIN32_FILE_ATTRIBUTE_DATA attrs{};
        if(!in.path.empty()&&GetFileAttributesExW(in.path.c_str(),GetFileExInfoStandard,&attrs)){filetime_=attrs.ftLastWriteTime;have_filetime_=true;}
        if(audio_end_>=128){auto tail=read_at(in.stream.p,audio_end_-128,128);if(!memcmp(tail.data(),"TAG",3)){suffix_=tail;audio_end_-=128;}}
        if(audio_end_>=32){auto footer=read_at(in.stream.p,audio_end_-32,32);
            if(!memcmp(footer.data(),"APETAGEX",8)){
                auto version=le32(footer.data()+8),size=le32(footer.data()+12),count=le32(footer.data()+16);
                require((version==1000||version==2000)&&size>=32&&size<=16*1024*1024&&size<=audio_end_&&count<=100000);
                auto data=read_at(in.stream.p,audio_end_-size,size-32);size_t p=0;
                for(DWORD i=0;i<count;++i){require(data.size()-p>=9);auto n=le32(data.data()+p),flags=le32(data.data()+p+4);p+=8;auto end=std::find(data.begin()+p,data.end(),0);require(end!=data.end());size_t k=end-data.begin();require(k-p<=255&&n<=data.size()-k-1);Tag t{std::string(data.begin()+p,end),Bytes(data.begin()+k+1,data.begin()+k+1+n),flags};require(!t.key.empty());items.push_back(std::move(t));p=k+1+n;}
                audio_end_-=size;
                if(audio_end_>=32){auto header=read_at(in.stream.p,audio_end_-32,32);if(!memcmp(header.data(),"APETAGEX",8)&&le32(header.data()+8)==2000&&(le32(header.data()+20)&0x20000000))audio_end_-=32;}
            }
        }
        // Delegate legacy ID3 decoding to the host while retaining every APE
        // binary item. No host tag object is modified or retained for writing.
        if(auto host=standard_content()){
            ComPtr<Metadata> tags;ULONGLONG bytes{};seek(in.stream.p,0);
            if(SUCCEEDED(host(in.stream.p,4,tags.put(),&bytes))&&tags){DWORD count{};if(SUCCEEDED(tags->Count(&count))&&count<=100000)for(DWORD i=0;i<count;++i){wchar_t *k{},*v{};if(SUCCEEDED(tags->At(i,&k,&v))&&k&&v){auto key=utf8(k);if(!find(key.c_str()))set(key.c_str(),v,false);}CoTaskMemFree(k);CoTaskMemFree(v);}}
        }
        // ID3v1-only streams remain readable without a host helper.
        if(!suffix_.empty())for(auto field: {std::pair<const char*,int>{"Title",3},{"Artist",33},{"Album",63},{"Year",93},{"Comment",97}}){if(find(field.first))continue;int n=field.second==93?4:(field.second==97&&suffix_[125]==0?28:30);std::string s(reinterpret_cast<char*>(suffix_.data()+field.second),n);while(!s.empty()&&(s.back()==0||s.back()==' '))s.pop_back();int len=MultiByteToWideChar(CP_ACP,0,s.data(),int(s.size()),nullptr,0);std::wstring w(len,0);if(len)MultiByteToWideChar(CP_ACP,0,s.data(),int(s.size()),w.data(),len);set(field.first,w.c_str(),false);}
        if(!suffix_.empty()&&!suffix_[125]&&suffix_[126]&&!find("Track")){auto track=std::to_wstring(suffix_[126]);set("Track",track.c_str(),false);}
        seek(in.stream.p,saved);
    }
    double number(const char *key) {
        auto t=find(key);if(!t)return 0;auto s=value(*t);wchar_t *end{};double v=wcstod(s.c_str(),&end);require(end!=s.c_str()&&std::isfinite(v),E_INVALIDARG);while(*end==L' ')++end;if(!_wcsicmp(end,L"db"))end+=2;require(!*end,E_INVALIDARG);return v;
    }
    Bytes encode() {
        Bytes b;
        for(const auto& t:items){require(b.size()+t.key.size()+t.value.size()+9<=16*1024*1024,E_OUTOFMEMORY);put_le32(b,DWORD(t.value.size()));put_le32(b,t.flags);b.insert(b.end(),t.key.begin(),t.key.end());b.push_back(0);b.insert(b.end(),t.value.begin(),t.value.end());}
        auto n=DWORD(b.size()+32);b.insert(b.end(),{'A','P','E','T','A','G','E','X'});put_le32(b,2000);put_le32(b,n);put_le32(b,DWORD(items.size()));put_le32(b,0);b.resize(b.size()+8);return b;
    }
    void patch_gain(IStream *out,uint64_t origin,DWORD version) {
        if(!rg_dirty)return;
        double g=number("replaygain_track_gain"),p=number("replaygain_track_peak"),ag=number("replaygain_album_gain"),ap=number("replaygain_album_peak");
        auto clamp=[](double v){require(std::isfinite(v)&&v>=0&&v<=65535,E_INVALIDARG);return WORD(std::floor(v+.5));};
        if(version==7){require(g>=-327.68&&g<=327.67&&ag>=-327.68&&ag<=327.67,E_INVALIDARG);auto h=read_at(out,origin,20);auto word=[&](size_t at,WORD n){h[at]=BYTE(n);h[at+1]=BYTE(n>>8);};word(12,clamp(p*32768));word(14,WORD(short(std::floor(g*100+.5))));word(16,clamp(ap*32768));word(18,WORD(short(std::floor(ag*100+.5))));seek(out,origin);write_exact(out,h.data(),DWORD(h.size()));}
        else {
            uint64_t at=origin+4;bool found{};
            while(at<audio_end_){auto h=read_at(out,at,size_t(std::min<uint64_t>(12,audio_end_-at)));require(h.size()>=3);uint64_t n=0;size_t pos=2;do{require(pos<h.size()&&pos<11&&n<=(UINT64_MAX>>7));auto c=h[pos++];n=(n<<7)|(c&127);if(!(c&128))break;}while(true);require(n>=pos&&n<=audio_end_-at);if(h[0]=='R'&&h[1]=='G'){require(n-pos>=9);BYTE rg[9]{1};auto word=[&](int i,WORD v){rg[i]=BYTE(v>>8);rg[i+1]=BYTE(v);};word(1,find("replaygain_track_gain")?clamp((64.82-g)*256):0);word(3,p>0?clamp(20*std::log10(p*32768)*256):0);word(5,find("replaygain_album_gain")?clamp((64.82-ag)*256):0);word(7,ap>0?clamp(20*std::log10(ap*32768)*256):0);seek(out,at+pos);write_exact(out,rg,sizeof(rg));found=true;break;}if(h[0]=='A'&&h[1]=='P')break;at+=n;}
            // RG is optional in SV8. Adding it would shift SO/ST seek offsets.
            // When absent, the standard APEv2 ReplayGain fields written above
            // are authoritative; never move audio blocks just to add RG.
            (void)found;
        }
    }
    HRESULT save(Input& in,uint64_t origin,DWORD version) {
        return protect([&]{
            if(!dirty)return S_OK;
            require(!in.remote&&!in.path.empty()&&in.path.find(L"::")==std::wstring::npos,STG_E_ACCESSDENIED);
            auto split=in.path.find_last_of(L"/\\");require(split!=in.path.npos&&in.path.size()<MAX_PATH-16,STG_E_INVALIDNAME);
            auto unchanged=[&]{WIN32_FILE_ATTRIBUTE_DATA attrs{};wincheck(GetFileAttributesExW(in.path.c_str(),GetFileExInfoStandard,&attrs));require(have_filetime_&&CompareFileTime(&attrs.ftLastWriteTime,&filetime_)==0,STG_E_REVERTED);require(!(attrs.dwFileAttributes&FILE_ATTRIBUTE_READONLY),STG_E_ACCESSDENIED);};
            unchanged();
            auto dir=in.path.substr(0,split+1);wchar_t temp[MAX_PATH]{};wincheck(GetTempFileNameW(dir.c_str(),L"mpc",0,temp));
            struct Temp{wchar_t *name;~Temp(){if(*name)DeleteFileW(name);}} guard{temp};
            ComPtr<IStream> out;check(SHCreateStreamOnFileEx(temp,STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,FALSE,nullptr,out.put()));
            auto size=stream_size(in.stream.p);require(size==in.size,STG_E_REVERTED);
            seek(in.stream.p,0);Bytes buffer(1024*1024);uint64_t left=audio_end_;
            while(left){auto n=DWORD(std::min<uint64_t>(left,buffer.size()));read_exact(in.stream.p,buffer.data(),n);write_exact(out.p,buffer.data(),n);left-=n;}
            auto tag=encode();write_exact(out.p,tag.data(),DWORD(tag.size()));if(!suffix_.empty())write_exact(out.p,suffix_.data(),DWORD(suffix_.size()));
            patch_gain(out.p,origin,version);commit_stream(out.p);auto newsize=stream_size(out.p);out.reset();unchanged();
            ComPtr<IStream> writable;
            if((in.mode&3)==STGM_READWRITE)writable=ComPtr<IStream>(in.stream.p,true);
            in.extended.reset();in.stream.reset();
            if(!writable){
                // ReplaceFile can fail after removing/renaming the original
                // (ERROR_UNABLE_TO_MOVE_REPLACEMENT). Keep its backup path so
                // the failure cannot discard the only original file image.
                wchar_t replaced_backup[MAX_PATH]{};
                wincheck(GetTempFileNameW(dir.c_str(),L"mpb",0,replaced_backup));
                Temp replaced_guard{replaced_backup};
                wincheck(DeleteFileW(replaced_backup));
                if(ReplaceFileW(in.path.c_str(),temp,replaced_backup,0,nullptr,nullptr)){
                    *temp=0;dirty=false;return S_OK;
                }
                DWORD replace_error=GetLastError();
                if(GetFileAttributesW(replaced_backup)!=INVALID_FILE_ATTRIBUTES){
                    if(!MoveFileExW(replaced_backup,in.path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
                        OutputDebugStringW((std::wstring(L"ttp_mpc: recovery file: ")+replaced_backup+L"\n").c_str());
                        *replaced_backup=0;
                    }
                    return HRESULT_FROM_WIN32(replace_error?replace_error:ERROR_WRITE_FAULT);
                }
            }
            // Some original hosts retain the editable stream without delete
            // sharing. Preserve that handle and take a full rollback image.
            if(!writable)check(SHCreateStreamOnFileEx(in.path.c_str(),STGM_READWRITE|STGM_SHARE_DENY_WRITE,0,FALSE,nullptr,writable.put()));
            require(stream_size(writable.p)==in.size,STG_E_REVERTED);unchanged();
            wchar_t backup_name[MAX_PATH]{};wincheck(GetTempFileNameW(dir.c_str(),L"mpb",0,backup_name));Temp backup_guard{backup_name};
            ComPtr<IStream> backup;check(SHCreateStreamOnFileEx(backup_name,STGM_READWRITE|STGM_SHARE_EXCLUSIVE,0,FALSE,nullptr,backup.put()));
            auto copy=[&](IStream*from,IStream*to,uint64_t length){seek(from,0);seek(to,0);while(length){DWORD n=DWORD(std::min<uint64_t>(length,buffer.size()));read_exact(from,buffer.data(),n);write_exact(to,buffer.data(),n);length-=n;}};
            copy(writable.p,backup.p,in.size);commit_stream(backup.p);
            check(SHCreateStreamOnFileEx(temp,STGM_READ|STGM_SHARE_DENY_WRITE,0,FALSE,nullptr,out.put()));
            auto overwrite=[&](IStream*from,uint64_t n){copy(from,writable.p,n);ULARGE_INTEGER size{};size.QuadPart=n;check(writable->SetSize(size));commit_stream(writable.p);};
            auto hr=protect([&]{overwrite(out.p,newsize);return S_OK;});
            if(FAILED(hr)){auto rollback=protect([&]{overwrite(backup.p,in.size);return S_OK;});if(FAILED(rollback)){OutputDebugStringW((std::wstring(L"ttp_mpc: recovery file: ")+backup_name+L"\n").c_str());*backup_name=0;}return hr;}
            dirty=false;return S_OK;
        });
    }
};
}
