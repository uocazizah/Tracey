#include <tracey/linker.hxx>
#include <tracey/elf.hxx>

#include <cstring>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Obj {
    std::vector<std::uint8_t> text;
    // symbols: name -> {offset, global, defined}
    struct Sym { std::string name; std::uint64_t value; bool global; bool defined; int sec; };
    std::vector<Sym> syms;
    struct Rel { std::uint64_t offset; std::uint32_t type; std::string symbol; };
    std::vector<Rel> rels;
};

bool read_obj(const std::string& path, Obj& o) {
    std::ifstream f(path, std::ios::binary);
    if(!f) return false;
    std::vector<std::uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    // parse ELF header
    if(buf.size() < 64) return false;
    if(buf[0]!=0x7f||buf[1]!='E'||buf[2]!='L'||buf[3]!='F') return false;
    // e_shoff at offset 40
    auto rd16=[&](size_t p){ return (uint16_t)buf[p] | ((uint16_t)buf[p+1]<<8); };
    auto rd32=[&](size_t p){ return (uint32_t)buf[p] | ((uint32_t)buf[p+1]<<8) | ((uint32_t)buf[p+2]<<16) | ((uint32_t)buf[p+3]<<24); };
    auto rd64=[&](size_t p){ uint64_t v=0; for(int i=0;i<8;i++) v |= (uint64_t)buf[p+i]<<(8*i); return v; };
    size_t e_shoff = rd64(40);
    uint16_t e_shnum = rd16(60);
    uint16_t e_shstrndx = rd16(62);
    // section headers
    std::vector<size_t> sh_offsets(e_shnum), sh_sizes(e_shnum), sh_types(e_shnum), sh_names(e_shnum), sh_links(e_shnum);
    for(int i=0;i<e_shnum;i++){
        size_t base=e_shoff + i*64;
        sh_names[i]=rd32(base);
        sh_types[i]=rd32(base+4);
        sh_offsets[i]=rd64(base+24);
        sh_sizes[i]=rd64(base+32);
        sh_links[i]=rd32(base+40);
    }
    // shstrtab
    size_t shstr_off=sh_offsets[e_shstrndx];
    auto getname=[&](size_t idx)->std::string { const char* p=(const char*)&buf[shstr_off+idx]; return std::string(p); };

    // find sections
    int text_idx=-1, symtab_idx=-1, strtab_idx=-1, rela_idx=-1;
    for(int i=0;i<e_shnum;i++){
        std::string name=getname(sh_names[i]);
        if(name==".text") text_idx=i;
        if(name==".symtab") symtab_idx=i;
        if(name==".strtab") strtab_idx=i;
        if(name==".rela.text") rela_idx=i;
    }
    if(text_idx<0) return false;
    o.text.assign(buf.begin()+sh_offsets[text_idx], buf.begin()+sh_offsets[text_idx]+sh_sizes[text_idx]);

    // strtab for symbols
    size_t symstr_off = (strtab_idx>=0)? sh_offsets[strtab_idx] : 0;
    auto symname=[&](uint32_t idx)->std::string { const char* p=(const char*)&buf[symstr_off+idx]; return std::string(p); };

    if(symtab_idx>=0){
        size_t base=sh_offsets[symtab_idx];
        size_t entsize=24;
        size_t count=sh_sizes[symtab_idx]/entsize;
        for(size_t i=0;i<count;i++){
            size_t p=base+i*entsize;
            uint32_t st_name=rd32(p);
            unsigned char info=buf[p+4];
            uint16_t shndx=rd16(p+6);
            uint64_t value=rd64(p+8);
            // size at p+16
            Obj::Sym s;
            s.name = (st_name==0)? "" : symname(st_name);
            s.value = value;
            s.global = ((info>>4)==1);
            s.defined = (shndx!=0);
            s.sec = shndx;
            o.syms.push_back(s);
        }
    }

    if(rela_idx>=0){
        size_t base=sh_offsets[rela_idx];
        size_t count=sh_sizes[rela_idx]/24;
        // symtab link
        int link = sh_links[rela_idx];
        // map symbol index to name via that symtab
        // reuse same symtab if link==symtab_idx
        for(size_t i=0;i<count;i++){
            size_t p=base+i*24;
            uint64_t off=rd64(p);
            uint64_t info=rd64(p+8);
            // int64 addend
            int64_t addend=(int64_t)rd64(p+16);
            uint32_t symidx = (uint32_t)(info>>32);
            uint32_t type = (uint32_t)(info & 0xffffffff);
            // get symbol name from linked symtab
            size_t symtab_base = sh_offsets[link];
            size_t sym_p = symtab_base + symidx*24;
            uint32_t st_name = rd32(sym_p);
            size_t symstr = sh_offsets[sh_links[link]];
            auto name=[&](uint32_t idx)->std::string { const char* p=(const char*)&buf[symstr+idx]; return std::string(p); };
            Obj::Rel r; r.offset=off; r.type=type; r.symbol = (st_name==0)?"":name(st_name);
            o.rels.push_back(r);
        }
    }

    return true;
}

} // anon

namespace tracey {

bool link_elf_aarch64(const std::vector<std::string>& objects, const std::string& output) {
    std::vector<Obj> objs;
    for(auto& p : objects){ Obj o; if(!read_obj(p,o)){ std::cerr<<"cannot read "<<p<<"\n"; return false;} objs.push_back(o); }

    // combine text sections
    std::vector<std::uint8_t> text;
    // map object index -> base offset
    std::vector<size_t> bases(objs.size());
    for(size_t i=0;i<objs.size();i++){ bases[i]=text.size(); text.insert(text.end(), objs[i].text.begin(), objs[i].text.end()); }

    // symbol table global: name -> address (virtual). Base virtual address 0x400000.
    const uint64_t text_file_off = 0x1000;
    const uint64_t code_base_va = 0x400000 + text_file_off;
    const uint64_t base_va = 0x400000;
    std::map<std::string,uint64_t> sym_addr;
    // collect defined global symbols
    for(size_t i=0;i<objs.size();i++){
        for(auto& s : objs[i].syms){
            if(s.defined && s.global && !s.name.empty()){
                sym_addr[s.name] = code_base_va + bases[i] + s.value;
            }
            if(s.defined && !s.name.empty() && s.name[0]=='.' ) {} // skip
            // also local labels? For relocations within same object, need local symbol addresses too. Store all defined symbols per object.
        }
    }
    // For relocation, need per-object symbol addresses including locals. We'll resolve by name globally; if duplicate, first wins. Also need local labels; they are in syms with global false but defined. Add them too (name may unique).
    for(size_t i=0;i<objs.size();i++){
        for(auto& s : objs[i].syms){
            if(s.defined && !s.name.empty()){
                if(sym_addr.find(s.name)==sym_addr.end()) sym_addr[s.name]=code_base_va + bases[i] + s.value;
            }
        }
    }

    // Apply relocations
    for(size_t i=0;i<objs.size();i++){
        for(auto& r : objs[i].rels){
            size_t pos = bases[i] + r.offset;
            // find target symbol address
            auto it = sym_addr.find(r.symbol);
            uint64_t target = (it!=sym_addr.end()) ? it->second : 0;
            // patch instruction at pos (little-endian 32-bit)
            uint32_t insn = text[pos] | (text[pos+1]<<8) | (text[pos+2]<<16) | (text[pos+3]<<24);
            uint64_t pc = code_base_va + pos;
            int64_t delta = (int64_t)(target - pc);
            if(r.type==283 || r.type==282){ // CALL26/JUMP26
                int32_t imm26 = (int32_t)((delta>>2) & 0x3FFFFFF);
                insn = (insn & ~0x3FFFFFFu) | (imm26 & 0x3FFFFFFu);
            } else if(r.type==280){ // CONDBR19
                int32_t imm19 = (int32_t)((delta>>2) & 0x7FFFF);
                insn = (insn & ~0x00FFFFE0u) | ((imm19 & 0x7FFFF)<<5);
            }
            // write back
            text[pos]=insn&0xff; text[pos+1]=(insn>>8)&0xff; text[pos+2]=(insn>>16)&0xff; text[pos+3]=(insn>>24)&0xff;
        }
    }

    // entry point / runtime startup
    bool need_start = (sym_addr.find("_start") == sym_addr.end());
    size_t startup_size = need_start ? 12 : 0;
    if (need_start) {
        // adjust all symbol addresses for prepended stub
        for (auto& kv : sym_addr) kv.second += startup_size;
        // prepend startup stub: bl main; mov x8,#93; svc #0
        uint32_t bl_main = 0x94000000u | ((startup_size / 4) & 0x3FFFFFFu);
        uint32_t mov_x8 = 0xD2800BA8u;
        uint32_t svc0 = 0xD4000001u;
        std::vector<std::uint8_t> stub;
        auto push32=[&](uint32_t v){ for(int i=0;i<4;i++){ stub.push_back(v&0xff); v>>=8;} };
        push32(bl_main); push32(mov_x8); push32(svc0);
        std::vector<std::uint8_t> full;
        full.insert(full.end(), stub.begin(), stub.end());
        full.insert(full.end(), text.begin(), text.end());
        text.swap(full);
    }
    uint64_t entry = code_base_va;
    if (!need_start && sym_addr.count("_start")) entry = sym_addr["_start"];
    else if (!need_start && sym_addr.count("main")) entry = sym_addr["main"];

    // Write ELF executable
    using namespace tracey::elf;
    std::ofstream f(output, std::ios::binary);
    if(!f) return false;

    // Build file: ELF header, program header, text.
    // Program header one PT_LOAD covering text.
    Ehdr ehdr{};
    unsigned char ident[16]={0x7f,'E','L','F',2,1,1,0,0,0,0,0,0,0,0,0};
    std::memcpy(ehdr.e_ident,ident,16);
    ehdr.e_type=ET_EXEC;
    ehdr.e_machine=EM_AARCH64;
    ehdr.e_version=1;
    ehdr.e_entry=entry;
    ehdr.e_phoff=64;
    ehdr.e_shoff=0;
    ehdr.e_ehsize=64;
    ehdr.e_phentsize=56;
    ehdr.e_phnum=1;
    ehdr.e_shentsize=64;
    ehdr.e_shnum=0;
    ehdr.e_shstrndx=0;

    Phdr ph{};
    ph.p_type=PT_LOAD;
    ph.p_flags=PF_R|PF_X;
    ph.p_offset=0;
    ph.p_vaddr=base_va;
    ph.p_paddr=base_va;
    ph.p_filesz=text.size();
    ph.p_memsz=text.size();
    ph.p_align=0x10000;

    // Write ehdr
    // manually
    auto pu16=[&](uint16_t v){ f.put(v&0xff); f.put((v>>8)&0xff); };
    auto pu32=[&](uint32_t v){ for(int i=0;i<4;i++){ f.put(v&0xff); v>>=8;} };
    auto pu64=[&](uint64_t v){ for(int i=0;i<8;i++){ f.put(v&0xff); v>>=8;} };
    for(int i=0;i<16;i++) f.put(ehdr.e_ident[i]);
    pu16(ehdr.e_type); pu16(ehdr.e_machine); pu32(ehdr.e_version);
    pu64(ehdr.e_entry); pu64(ehdr.e_phoff); pu64(ehdr.e_shoff);
    pu32(ehdr.e_flags); pu16(ehdr.e_ehsize); pu16(ehdr.e_phentsize);
    pu16(ehdr.e_phnum); pu16(ehdr.e_shentsize); pu16(ehdr.e_shnum); pu16(ehdr.e_shstrndx);

    // phdr
    pu32(ph.p_type); pu32(ph.p_flags); pu64(ph.p_offset); pu64(ph.p_vaddr);
    pu64(ph.p_paddr); pu64(ph.p_filesz); pu64(ph.p_memsz); pu64(ph.p_align);

    // text at offset? ELF header+phdr = 120 bytes. But PT_LOAD offset 0 includes headers; vaddr base_va maps file offset 0. That means entry at base_va corresponds to file offset entry-base_va = 0? Actually if offset 0 and vaddr base_va, then virtual address base_va maps file offset 0. Text should start at base_va + (text_offset_in_file - ph_offset). We set ph_offset=0, filesz=text.size()+headers? Need include headers in filesz or set text offset to page boundary. Simpler: set ph_offset=0, p_vaddr=base_va, p_filesz = headers+text? But then execution starts at base_va which maps to file 0 (ELF header) not text. Need align.

    // Better: put text at offset 0x10000? Let's instead set base_va = 0x400000, and place text immediately after headers but adjust entry and vaddr.
    // We already wrote headers at file offset 0. Let's not; redo properly:
    // We'll truncate and rewrite with text at offset 0x1000.
    f.close();
    // Rewrite correctly:
    std::ofstream f2(output, std::ios::binary | std::ios::trunc);
    if(!f2) return false;
    // ELF header
    ehdr.e_phoff = 64;
    // write ehdr
    for(int i=0;i<16;i++) f2.put(ehdr.e_ident[i]);
    // redefine pu functions for f2
    auto w16=[&](uint16_t v){ f2.put(v&0xff); f2.put((v>>8)&0xff); };
    auto w32=[&](uint32_t v){ for(int i=0;i<4;i++){ f2.put(v&0xff); v>>=8;} };
    auto w64=[&](uint64_t v){ for(int i=0;i<8;i++){ f2.put(v&0xff); v>>=8;} };
    w16(ehdr.e_type); w16(ehdr.e_machine); w32(ehdr.e_version);
    w64(ehdr.e_entry); w64(ehdr.e_phoff); w64(ehdr.e_shoff);
    w32(ehdr.e_flags); w16(ehdr.e_ehsize); w16(ehdr.e_phentsize);
    w16(ehdr.e_phnum); w16(ehdr.e_shentsize); w16(ehdr.e_shnum); w16(ehdr.e_shstrndx);

    ph.p_offset = 0;
    ph.p_vaddr = base_va;
    ph.p_paddr = base_va;
    ph.p_filesz = text_file_off + text.size();
    ph.p_memsz = text_file_off + text.size();
    ph.p_align = 0x1000;

    // phdr
    w32(ph.p_type); w32(ph.p_flags); w64(ph.p_offset); w64(ph.p_vaddr);
    w64(ph.p_paddr); w64(ph.p_filesz); w64(ph.p_memsz); w64(ph.p_align);

    // pad to text_file_off
    size_t cur = 64 + 56;
    while(cur < text_file_off){ f2.put(0); cur++; }
    // write text
    f2.write(reinterpret_cast<const char*>(text.data()), text.size());
    f2.close();
    return true;
}

} // namespace tracey
