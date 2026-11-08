#include "formats/xeen/XeenSoundSetup.h"
#include <algorithm>
namespace mmodern {
const std::array<XeenSoundRow,10> &xeenSpeechMenu() {
 static const std::vector<unsigned> sb{0x210,0x220,0x230,0x240,0x250,0x260},irq{2,3,5,7,10},dma{0,1,3};
 static const std::array<XeenSoundRow,10> rows{{
  {"None",0,0,{}, {}, {}},{"Adlib",2,0,{}, {}, {}},{"PC Speaker",1,0,{}, {}, {}},
  {"Sound Blaster",6,0,sb,irq,{},true},{"Sound Blaster Pro",7,0,sb,irq,dma,true},
  {"Pro-Audio Spectrum",8,0,{}, {}, {}},{"Disney Sound Source",5,0,{}, {}, {}},
  {"Covox Sound Master 2",9,0,{0x22f,0x24f,0x28f,0x2cf},irq,{}},
  {"Sound Blaster 16",10,0,sb,irq,dma,true},{"Pro-Audio Spectrum Alt",6,0,sb,irq,{},true}
 }};return rows;
}
const std::array<XeenSoundRow,10> &xeenMusicMenu() {
 static const std::array<XeenSoundRow,10> rows{{
  {"None",0,0,{}, {}, {}},{"Adlib",1,0x388,{}, {}, {}},{"PC Speaker",7,0,{}, {}, {}},
  {"Sound Blaster",2,0x388,{}, {}, {}},{"Sound Blaster Pro",3,0x220,{}, {}, {}},
  {"Pro-Audio Spectrum",6,0x388,{}, {}, {}},{"Roland Canvas / GS",5,0x330,{}, {}, {}},
  {"Roland LAPC-1 / MT-32",4,0x330,{}, {}, {}},{"Wave Blaster",8,0x330,{}, {}, {}},
  {"Roland TAP-10",9,0,{0x220,0x230,0x240,0x250,0x320,0x330,0x340,0x350}, {}, {}}
 }};return rows;
}
void xeenValidateSetupBase(const std::vector<std::uint8_t> &bytes) {
    if(bytes.size()!=28 && bytes.size()!=29 && bytes.size()!=30)throw std::runtime_error("Malformed XEEN.CFG: expected 28, 29 or 30 bytes");
 const auto cfg=XeenAudioConfig::parse(bytes);
 if(cfg.digital>10 || cfg.music>9)throw std::runtime_error("Malformed XEEN.CFG: unknown device id");
}
std::vector<std::uint8_t> xeenApplySoundSetup(const std::vector<std::uint8_t> &base,const XeenSoundAnswers &a) {
 xeenValidateSetupBase(base);
 if(a.speech>=10 || a.music>=10)throw std::runtime_error("Sound setup requires installer keys 1..9 or 0");
 auto bytes=base;const auto &d=xeenSpeechMenu()[a.speech],&m=xeenMusicMenu()[a.music];
 const auto put=[&](unsigned at,unsigned value){bytes[at]=value;bytes[at+1]=value>>8;};
 const auto answer=[&](unsigned at,const std::vector<unsigned> &values,std::optional<unsigned> value) {
  if(values.empty()) {if(value)throw std::runtime_error("No installer question for supplied sound answer");return;}
  if(!value || std::find(values.begin(),values.end(),*value)==values.end())throw std::runtime_error("Missing or invalid installer sound answer");
  put(at,*value);
 };
 put(0,d.id);put(4,m.id);
 answer(2,d.ports,a.digitalPort);answer(6,m.ports,a.musicPort);
 if(m.musicPort)put(6,m.musicPort);
 answer(8,d.irqs,a.irq);answer(0x18,d.dmas,a.dma);
 answer(0x14,d.volume?std::vector<unsigned>{0,1}:std::vector<unsigned>{},a.volume);
 return bytes;
}
std::string xeenSoundCaption(unsigned id,bool music) {
 const auto &rows=music?xeenMusicMenu():xeenSpeechMenu();
 for(const auto &row:rows)if(row.id==id)return row.caption;
 return "Unknown ("+std::to_string(id)+")";
}
std::string xeenSoundStatus(const XeenAudioConfig &cfg,bool music) {
 const auto id=music?cfg.music:cfg.digital;
 return id==0 || (music?cfg.deferredMusic():cfg.deferredDigital())?"silent":"played";
}
}
