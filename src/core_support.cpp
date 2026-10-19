#include "common.h"
#include <array>
// r475 rebuilds shared Huffman tables on every decoder open. Metadata scans
// and playback may run concurrently: initialise exactly once per loaded DLL.
extern "C" void ttplayer_huff_init_lut_impl(int);
extern "C" void huff_init_lut(int bits){static volatile LONG state=0;if(InterlockedCompareExchange(&state,1,0)==0){ttplayer_huff_init_lut_impl(bits);InterlockedExchange(&state,2);}else while(InterlockedCompareExchange(&state,2,2)!=2)Sleep(0);}
namespace {
constexpr std::array<unsigned long,256> crc_table(){std::array<unsigned long,256> table{};for(unsigned long n=0;n<256;++n){auto value=n;for(int bit=0;bit<8;++bit)value=(value&1)?(value>>1)^0xedb88320u:value>>1;table[n]=value;}return table;}
constexpr auto table=crc_table();
}
// Immutable CRC table replaces the upstream unsynchronised lazy table.
extern "C" unsigned long mpc_crc32(unsigned char*p,int n){unsigned long value=0xffffffffu;for(int i=0;i<n;++i)value=table[(value^p[i])&255]^(value>>8);return value^0xffffffffu;}
