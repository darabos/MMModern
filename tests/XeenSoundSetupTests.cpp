#include "formats/xeen/XeenSoundSetup.h"
#include "app/XeenPlayerSupport.h"
#include "compat/scummvm/XeenMt32.h"
#include "platform/XeenSaveFile.h"
#include "games/xeen/XeenInstallationDetector.h"
#include "XeenTestInstallation.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace mmodern;
namespace {
void check(bool v,const char *s){if(!v)throw std::runtime_error(s);}
std::vector<std::uint8_t> read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
}
int main(int argc,char **argv){try{
 check(argc==4,"usage: sound setup tests <data> <scratch> <roms>");
#ifdef _WIN32
 const std::filesystem::path profile=L"C:\\Users\\Jos\u00e9",local=profile/"AppData"/"Local";
 check(xeenPrivatePaths(u8"file: C:/Users/JOS\u00c9/config with spaces/XEEN.CFG\n",local,profile)=="file: %USERPROFILE%\\config with spaces\\XEEN.CFG\n","Unicode/case/profile redaction");
 check(xeenPrivatePaths(u8"C:/Users/JOS\u00c9ph/config",local,profile)==u8"C:/Users/JOS\u00c9ph/config","Profile boundary redaction");
 check(xeenPrivatePaths(u8"C:/Users/JOS\u00c9/AppData/Local/MMModern/XEEN.CFG",local,profile)=="%LOCALAPPDATA%\\MMModern\\XEEN.CFG","Local AppData precedence");
#else
 const std::filesystem::path profile=u8"/home/Jos\u00e9",local=profile/".local/share/MMModern";
 check(xeenPrivatePaths(local.native()+"/XEEN.CFG",local,profile)=="~/.local/share/MMModern/XEEN.CFG","fallback base label");
 check(xeenPrivatePaths(profile.native()+"/config with spaces/XEEN.CFG",local,profile)=="~/config with spaces/XEEN.CFG","POSIX profile redaction");
 check(xeenPrivatePaths(u8"/home/JOS\u00c9/config",local,profile)==u8"/home/JOS\u00c9/config","POSIX case-sensitive redaction");
 check(xeenPrivatePaths(profile.native()+"ph/config",local,profile)==profile.native()+"ph/config","POSIX profile boundary");
#endif
 const unsigned digital[]{0,2,1,6,7,8,5,9,10,6},music[]{0,1,7,2,3,6,5,4,8,9};
 const unsigned ports[]{0,0x388,0,0x388,0x220,0x388,0x330,0x330,0x330,0};
 for(unsigned n=0;n<10;++n) {
  const auto &d=xeenSpeechMenu()[n],&m=xeenMusicMenu()[n];
  const bool sb=n==3 || n==4 || n==8 || n==9,covox=n==7;
  check(d.id==digital[n] && m.id==music[n] && m.musicPort==ports[n],"Menu golden ids/ports");
  check(d.ports==(sb?std::vector<unsigned>{0x210,0x220,0x230,0x240,0x250,0x260}:covox?std::vector<unsigned>{0x22f,0x24f,0x28f,0x2cf}:std::vector<unsigned>{}),"Installer digital port list");
  check(d.irqs==((sb || covox)?std::vector<unsigned>{2,3,5,7,10}:std::vector<unsigned>{}),"Installer IRQ list");
  check(d.dmas==((n==4 || n==8)?std::vector<unsigned>{0,1,3}:std::vector<unsigned>{}) && d.volume==sb,"Installer DMA/volume questions");
  check(m.ports==(n==9?std::vector<unsigned>{0x220,0x230,0x240,0x250,0x320,0x330,0x340,0x350}:std::vector<unsigned>{}),"Installer TAP-10 port list");
 }
 for(unsigned size:{28,29,30})for(unsigned d=0;d<10;++d)for(unsigned m=0;m<10;++m)for(unsigned volume:{0,1}) {
  std::vector<std::uint8_t> base(size,0x5a);base[0]=10;base[1]=0;base[4]=3;base[5]=0;
  auto expected=base;XeenSoundAnswers a;a.speech=d;a.music=m;
  const auto put=[&](unsigned at,unsigned v){expected[at]=v;expected[at+1]=v>>8;};
  put(0,digital[d]);put(4,music[m]);if(ports[m])put(6,ports[m]);
  const bool sb=d==3 || d==4 || d==8 || d==9,covox=d==7;
  if(sb || covox){a.digitalPort=covox?0x2cf:0x260;put(2,*a.digitalPort);a.irq=10;put(8,10);}
  if(d==4 || d==8){a.dma=0;put(0x18,0);}
  if(sb){a.volume=volume;put(0x14,volume);}
  if(m==9){a.musicPort=0x350;put(6,0x350);}
  check(xeenApplySoundSetup(base,a)==expected,"Installer fields or retained bytes differ");
  const auto cfg=XeenAudioConfig::parse(expected);
  check(xeenSoundStatus(cfg,true)==((!cfg.music || cfg.deferredMusic())?"silent":"played"),"Music status predicate");
  check(xeenSoundStatus(cfg,false)==((!cfg.digital || cfg.deferredDigital())?"silent":"played"),"Digital status predicate");
 }
 for(unsigned n:{0,6,27,31}){bool refused=false;try{xeenValidateSetupBase(std::vector<std::uint8_t>(n));}catch(...){refused=true;}check(refused,"Malformed base accepted");}
 std::vector<std::uint8_t> base(30);base.back()=0xa5;
 XeenSoundAnswers a;a.speech=0;a.music=9;bool refused=false;try{xeenApplySoundSetup(base,a);}catch(...){refused=true;}check(refused,"TAP-10 needs port");
 a.music=0;a.dma=1;refused=false;try{xeenApplySoundSetup(base,a);}catch(...){refused=true;}check(refused,"Unasked DMA accepted");
 const auto scratch=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(scratch);
 const auto path=scratch/"XEEN.CFG";
 xeenWriteOwnedCfg(path,base);check(read(path)==base,"Atomic creation");base[0]=6;xeenWriteOwnedCfg(path,base);check(read(path)==base,"Atomic replacement");
 refused=false;try{xeenWriteOwnedCfg(path,{});}catch(...){refused=true;}check(refused && read(path)==base,"Rejected write changed existing CFG");
 std::filesystem::create_directory(scratch/"blocked");refused=false;try{xeenWriteOwnedCfg(scratch/"blocked",base);}catch(...){refused=true;}check(refused,"Failed replacement accepted");
 for(const auto &f:std::filesystem::directory_iterator(scratch))check(f.path().filename().u8string().find(".tmp-")==std::string::npos,"Temporary CFG leaked");
 xeenResetOwnedCfg(path);xeenResetOwnedCfg(path);check(!std::filesystem::exists(path),"Reset");
 const auto installation=xeenTestInstallationDetector().detect(std::filesystem::u8path(argv[1]));check(bool(installation),"Original data missing");
 const auto signature=XeenSaveFile::fingerprint(*installation),again=XeenSaveFile::fingerprint(*installation);
 check(XeenSaveFile::dataId(signature)==XeenSaveFile::dataId(again),"Data id unstable");
 const auto roms=XeenMt32::identify(std::filesystem::u8path(argv[3]));
 check(roms.control.find("identified")==0 && roms.pcm.find("identified")==0,"Original ROM identification");
 const auto missing=XeenMt32::identify(scratch/"absent");check(missing.control=="missing" && missing.pcm=="missing","Missing ROMs");
 std::ofstream(scratch/"wrong.rom")<<"wrong";const auto wrong=XeenMt32::identify(scratch);check(wrong.control.find("wrong")==0 && wrong.pcm.find("wrong")==0,"Wrong ROMs");
 std::filesystem::remove(scratch/"wrong.rom");std::filesystem::remove(scratch/"blocked");
 std::cout<<"Installer rows, retention, atomic publication/reset and synth-free ROM identification passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
