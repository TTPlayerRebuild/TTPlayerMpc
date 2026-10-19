#include "ttpcomm/audio_format_probe_impl.h"
#include "metadata.h"
#include <mpc/mpcdec.h>
#include <array>
namespace ttp::mpc {
class MpcReader final : public Reader,public Metadata,public MetadataCommit, public ttp::format::InfoSupport {
    LONG refs_{1};
    std::unique_ptr<Input> input_;
    mpc_reader callbacks_{};
    mpc_demux *decoder_{};
    mpc_streaminfo info_{};
    WAVEFORMATEX format_{};
    std::array<MPC_SAMPLE_FORMAT,MPC_DECODER_BUFFER_LENGTH> pcm_{};
    size_t pending_{},offset_{};
    uint64_t position_{},total_{};
    DWORD flags_{};
    HRESULT io_error_{S_OK},decode_error_{S_OK},commit_result_{S_OK};
    bool opened_{},committed_{};
    Tags tags_;
    void destroy_decoder(){if(decoder_){mpc_demux_exit(decoder_);decoder_=nullptr;}}
    void close(){destroy_decoder();if(input_&&!committed_){commit_result_=tags_.save(*input_,info_.header_position,info_.stream_version);committed_=true;}input_.reset();opened_=false;}
    template<class F> int callback(F f) noexcept {int result{};auto hr=protect([&]{result=f();return S_OK;});if(FAILED(hr)&&SUCCEEDED(io_error_))io_error_=hr;return FAILED(hr)?0:result;}
    static MpcReader& self(mpc_reader *r){return *static_cast<MpcReader*>(r->data);}
    static mpc_int32_t read(mpc_reader *r,void *p,mpc_int32_t n){auto& s=self(r);return s.callback([&]{require(n>=0,E_INVALIDARG);ULONG total=0;while(total<ULONG(n)){require(WaitForSingleObject(s.input_->event,0)!=WAIT_OBJECT_0,E_ABORT);auto got=s.input_->read(static_cast<BYTE*>(p)+total,ULONG(n)-total);total+=got;if(!got)break;}return int(total);});}
    static mpc_bool_t at(mpc_reader *r,mpc_int32_t n){auto& s=self(r);return mpc_bool_t(s.callback([&]{require(n>=0,STG_E_INVALIDFUNCTION);s.input_->at(n);return 1;}));}
    static mpc_int32_t current(mpc_reader *r){auto& s=self(r);return s.callback([&]{auto n=tell(s.input_->stream.p);require(n<=INT_MAX,HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));return int(n);});}
    static mpc_int32_t size(mpc_reader *r){return int(self(r).input_->size);}
    static mpc_bool_t seekable(mpc_reader *r){return mpc_bool_t(self(r).input_->seekable?1:0);}
    void gain_field(const char *name,uint16_t raw,bool peak){if(!raw||tags_.find(name))return;double v=peak?std::pow(10.,raw/(20.*256.))/32768.:64.82-raw/256.;wchar_t b[64];swprintf_s(b,L"%.8f%s",v,peak?L"":L" dB");tags_.set(name,b,false);}
    std::vector<Tag*> text_tags(){std::vector<Tag*> v;for(auto& t:tags_.items)if(!(t.flags&6))v.push_back(&t);return v;}
    template<class T>HRESULT property(T *p,T value){if(!p)return E_POINTER;*p={};if(!opened_||committed_)return E_UNEXPECTED;*p=value;return S_OK;}
public:
    ~MpcReader(){close();}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **p) override {
        if (!p) return E_POINTER;
        if (IsEqualIID(iid, ttp::format::iid_info)) { *p=static_cast<ttp::format::SourceInfo*>(this); AddRef(); return S_OK; }

        if(!p)return E_POINTER;*p=nullptr;
        if(same(iid,IID_IUnknown)||same(iid,iid_reader))*p=static_cast<Reader*>(this);
        else if(same(iid,iid_metadata))*p=static_cast<Metadata*>(this);
        else if(same(iid,iid_metadata_commit))*p=static_cast<MetadataCommit*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs_);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Open(IStream* probe_stream, DWORD probe_flags) override {
        const auto probed = PrepareFormatInfo(probe_stream, ttp::format::Mpc);
        if (FAILED(probed)) return probed;
        return CompleteFormatInfo(OpenImpl(probe_stream, probe_flags));
    }
    HRESULT OpenImpl(IStream *s,DWORD f) {
        if(opened_||committed_)return E_UNEXPECTED;
        auto hr=protect([&]{input_=std::make_unique<Input>();input_->open(s);require(input_->seekable,STG_E_INVALIDFUNCTION);require(input_->size>=4&&input_->size<=INT_MAX,HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));input_->at(0);tags_.load(*input_);input_->at(0);callbacks_={read,at,current,size,seekable,this};decoder_=mpc_demux_init(&callbacks_);check(io_error_);require(decoder_!=nullptr,HRESULT_FROM_WIN32(ERROR_INVALID_DATA));mpc_demux_get_info(decoder_,&info_);require((info_.stream_version==7||info_.stream_version==8)&&info_.sample_freq&&info_.channels>=1&&info_.channels<=2);auto samples=mpc_streaminfo_get_length_samples(&info_);require(samples>=0);total_=uint64_t(samples);format_={WAVE_FORMAT_IEEE_FLOAT,WORD(info_.channels),info_.sample_freq,info_.sample_freq*info_.channels*4,WORD(info_.channels*4),32,0};flags_=f;opened_=true;
            gain_field("replaygain_track_gain",info_.gain_title,false);gain_field("replaygain_track_peak",info_.peak_title,true);gain_field("replaygain_album_gain",info_.gain_album,false);gain_field("replaygain_album_peak",info_.peak_album,true);return S_OK;});
        if(FAILED(hr)){destroy_decoder();input_.reset();}return hr;
    }
    HRESULT STDMETHODCALLTYPE Capabilities(DWORD *p) override{return property(p,input_?input_->capabilities(false):0u);}
    HRESULT STDMETHODCALLTYPE Duration(DWORD *p) override{return property(p,info_.sample_freq?milliseconds(total_,info_.sample_freq):0u);}
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX **p) override{if(!p)return E_POINTER;*p=nullptr;if(!opened_||committed_)return E_UNEXPECTED;*p=static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(format_)));if(!*p)return E_OUTOFMEMORY;**p=format_;return S_OK;}
    HRESULT STDMETHODCALLTYPE BufferSize(DWORD *p) override{return property(p,DWORD(MPC_FRAME_LENGTH*format_.nBlockAlign));}
    HRESULT STDMETHODCALLTYPE CodecName(wchar_t **p) override{return text(L"MPC|Musepack Audio",p);}
    HRESULT STDMETHODCALLTYPE Bitrate(DWORD *p) override{return property(p,dword(uint64_t(std::max(0.,info_.average_bitrate))));}
    HRESULT STDMETHODCALLTYPE BitDepth(WORD *p) override{return property(p,WORD(16));}
    HRESULT STDMETHODCALLTYPE SetCallback(IUnknown *p) override{return input_?input_->callback(p):E_UNEXPECTED;}
    HRESULT STDMETHODCALLTYPE Start() override{if(!opened_||committed_)return E_UNEXPECTED;ResetEvent(input_->event);return S_OK;}
    HRESULT STDMETHODCALLTYPE Stop() override{if(!opened_||committed_)return E_UNEXPECTED;SetEvent(input_->event);return S_OK;}
    HRESULT STDMETHODCALLTYPE Read(Buffer *b) override {
        return protect([&]()->HRESULT{require(b,E_POINTER);check(b->SetLength(0));require(opened_&&!committed_,E_UNEXPECTED);require(WaitForSingleObject(input_->event,0)!=WAIT_OBJECT_0,E_ABORT);check(decode_error_);DWORD capacity{},ignored{};BYTE *p{};check(b->Capacity(&capacity));check(b->Data(&p,&ignored));require(p,E_POINTER);require(capacity>=format_.nBlockAlign,E_INVALIDARG);size_t wanted=capacity/format_.nBlockAlign,done=0;
            while(done<wanted&&position_<total_){
                if(offset_==pending_){mpc_frame_info frame{};frame.buffer=pcm_.data();unsigned skipped=0;do{auto status=mpc_demux_decode(decoder_,&frame);check(io_error_);require(status==MPC_STATUS_OK);require(frame.samples<=MPC_FRAME_LENGTH);if(frame.bits==-1){decode_error_=HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);break;}require(++skipped<65536); }while(!frame.samples);
                    if(FAILED(decode_error_))break;offset_=0;pending_=frame.samples;}
                auto n=std::min<uint64_t>(std::min(wanted-done,pending_-offset_),total_-position_);memcpy(p+done*format_.nBlockAlign,pcm_.data()+offset_*info_.channels,size_t(n)*format_.nBlockAlign);offset_+=size_t(n);done+=size_t(n);position_+=n;
            }
            if(done){check(b->SetLength(DWORD(done*format_.nBlockAlign)));return S_OK;}check(decode_error_);return S_FALSE;});
    }
    HRESULT STDMETHODCALLTYPE Seek(DWORD *ms) override{return protect([&]{require(ms,E_POINTER);require(opened_&&!committed_,E_UNEXPECTED);DWORD duration=milliseconds(total_,info_.sample_freq);uint64_t target=*ms>duration?total_:uint64_t(*ms)*info_.sample_freq/1000;ResetEvent(input_->event);io_error_=decode_error_=S_OK;offset_=pending_=0;if(target<total_){auto status=mpc_demux_seek_sample(decoder_,target);check(io_error_);require(status==MPC_STATUS_OK);}position_=target;if(*ms>duration)*ms=duration;return S_OK;});}
    HRESULT STDMETHODCALLTYPE Count(DWORD *p) override{return protect([&]{require(p,E_POINTER);*p=DWORD(text_tags().size());return S_OK;});}
    HRESULT STDMETHODCALLTYPE At(DWORD i,wchar_t **k,wchar_t **v) override{return protect([&]{require(k&&v,E_POINTER);*k=*v=nullptr;auto tags=text_tags();require(i<tags.size(),E_INVALIDARG);auto key=wide(tags[i]->key),val=tags_.value(*tags[i]);check(text(key,k));auto hr=text(val,v);if(FAILED(hr)){CoTaskMemFree(*k);*k=nullptr;}return hr;});}
    HRESULT STDMETHODCALLTYPE Get(const char *k,wchar_t **v) override{return protect([&]{require(k&&v,E_POINTER);*v=nullptr;auto t=tags_.find(k);require(t&&!(t->flags&6),E_INVALIDARG);return text(tags_.value(*t),v);});}
    HRESULT STDMETHODCALLTYPE Set(const char *k,const wchar_t *v) override{return protect([&]{require(opened_&&!committed_,E_UNEXPECTED);require(input_->capabilities(false)&4,STG_E_ACCESSDENIED);if(!k&&!v){tags_.clear();}else tags_.set(k,v);return S_OK;});}
    HRESULT STDMETHODCALLTYPE Commit() override{if(committed_)return commit_result_;if(!opened_)return E_UNEXPECTED;committed_=true;destroy_decoder();commit_result_=tags_.save(*input_,info_.header_position,info_.stream_version);return commit_result_;}
};
HRESULT make_reader(void **p){if(!p)return E_POINTER;*p=nullptr;return protect([&]{*p=static_cast<Reader*>(new MpcReader);return S_OK;});}
}
