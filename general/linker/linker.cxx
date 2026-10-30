#include <tracey/linker.hxx>
#include <tracey/elf.hxx>

#include <cstring>
#include <cstdio>

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
    std::vector<std::uint8_t> debug_line;
    std::vector<std::uint8_t> debug_info;
    std::vector<std::uint8_t> debug_abbrev;
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
    auto rd16=[&](size_t p){ return static_cast<std::uint16_t>(static_cast<std::uint16_t>(buf[p]) | (static_cast<std::uint16_t>(buf[p+1])<<8)); };
    auto rd32=[&](size_t p){ return static_cast<std::uint32_t>(buf[p]) | (static_cast<std::uint32_t>(buf[p+1])<<8) | (static_cast<std::uint32_t>(buf[p+2])<<16) | (static_cast<std::uint32_t>(buf[p+3])<<24); };
    auto rd64=[&](size_t p){ std::uint64_t v=0; for(size_t i=0;i<8;i++) v |= (static_cast<std::uint64_t>(buf[p+i])<<(8*i)); return v; };
    size_t e_shoff = rd64(40);
    std::uint16_t e_shnum = rd16(60);
    std::uint16_t e_shstrndx = rd16(62);
    // section headers
    std::vector<size_t> sh_offsets(e_shnum), sh_sizes(e_shnum), sh_types(e_shnum), sh_names(e_shnum), sh_links(e_shnum);
    for(size_t i=0;i<e_shnum;i++){
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
    for(size_t i=0;i<e_shnum;i++){
        std::string name=getname(sh_names[i]);
        if(name==".text") text_idx=static_cast<int>(i);
        if(name==".symtab") symtab_idx=static_cast<int>(i);
        if(name==".strtab") strtab_idx=static_cast<int>(i);
        if(name==".rela.text") rela_idx=static_cast<int>(i);
    }
    if(text_idx<0) return false;
    o.text.assign(buf.data()+sh_offsets[static_cast<size_t>(text_idx)], buf.data()+sh_offsets[static_cast<size_t>(text_idx)]+sh_sizes[static_cast<size_t>(text_idx)]);
    for(size_t i=0;i<e_shnum;i++){
        std::string name=getname(sh_names[i]);
        if(name==".debug_line" && sh_sizes[i]>0){
            o.debug_line.assign(buf.data()+sh_offsets[i], buf.data()+sh_offsets[i]+sh_sizes[i]);
        }
        if(name==".debug_info" && sh_sizes[i]>0){
            o.debug_info.assign(buf.data()+sh_offsets[i], buf.data()+sh_offsets[i]+sh_sizes[i]);
        }
        if(name==".debug_abbrev" && sh_sizes[i]>0){
            o.debug_abbrev.assign(buf.data()+sh_offsets[i], buf.data()+sh_offsets[i]+sh_sizes[i]);
        }
    }

    // strtab for symbols
    size_t symstr_off = (strtab_idx>=0)? sh_offsets[static_cast<size_t>(strtab_idx)] : 0;
    auto symname=[&](std::uint32_t idx)->std::string { const char* p=(const char*)&buf[symstr_off+idx]; return std::string(p); };

    if(symtab_idx>=0){
        size_t base=sh_offsets[static_cast<size_t>(symtab_idx)];
        size_t entsize=24;
        size_t count=sh_sizes[static_cast<size_t>(symtab_idx)]/entsize;
        for(size_t i=0;i<count;i++){
            size_t p=base+i*entsize;
            std::uint32_t st_name=rd32(p);
            unsigned char info=buf[p+4];
            std::uint16_t shndx=rd16(p+6);
            std::uint64_t value=rd64(p+8);
            // size at p+16
            Obj::Sym s;
            s.name = (st_name==0)? "" : symname(st_name);
            s.value = value;
            s.global = ((info>>4)==1);
            s.defined = (shndx!=0);
            s.sec = static_cast<int>(shndx);
            o.syms.push_back(s);
        }
    }

    if(rela_idx>=0){
        size_t base=sh_offsets[static_cast<size_t>(rela_idx)];
        size_t count=sh_sizes[static_cast<size_t>(rela_idx)]/24;
        // symtab link
        size_t link = sh_links[static_cast<size_t>(rela_idx)];
        // map symbol index to name via that symtab
        // reuse same symtab if link==symtab_idx
        for(size_t i=0;i<count;i++){
            size_t p=base+i*24;
            std::uint64_t off=rd64(p);
            std::uint64_t info=rd64(p+8);
            std::uint32_t symidx = static_cast<std::uint32_t>(info>>32);
            std::uint32_t type = static_cast<std::uint32_t>(info & 0xffffffffu);
            // get symbol name from linked symtab
            size_t symtab_base = sh_offsets[link];
            size_t sym_p = symtab_base + static_cast<size_t>(symidx)*24;
            std::uint32_t st_name = rd32(sym_p);
            size_t symstr = sh_offsets[sh_links[link]];
            auto name=[&](std::uint32_t idx)->std::string { const char* ptr=(const char*)&buf[symstr+static_cast<size_t>(idx)]; return std::string(ptr); };
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
    const std::uint64_t text_file_off = 0x1000;
    const std::uint64_t code_base_va = 0x400000 + text_file_off;
    const std::uint64_t base_va = 0x400000;
    std::map<std::string,std::uint64_t> sym_addr;
    std::map<std::string,bool> sym_global;
    // collect defined global symbols
    for(size_t i=0;i<objs.size();i++){
        for(auto& s : objs[i].syms){
            if(s.defined && s.global && !s.name.empty()){
                if(sym_addr.find(s.name)==sym_addr.end()) sym_addr[s.name] = code_base_va + bases[i] + s.value;
                sym_global[s.name] = true;
            }
            if(s.defined && !s.name.empty() && s.name[0]=='.' ) {} // skip
            // also local labels? For relocations within same object, need local symbol addresses too. Store all defined symbols per object.
        }
    }
    // For relocation, need per-object symbol addresses including locals. We'll resolve by name globally; if duplicate, first wins. Also need local labels; they are in syms with global false but defined. Add them too (name may unique).
    for(size_t i=0;i<objs.size();i++){
        for(auto& s : objs[i].syms){
            if(s.defined && !s.name.empty()){
                if(sym_addr.find(s.name)==sym_addr.end()) {
                    sym_addr[s.name]=code_base_va + bases[i] + s.value;
                    sym_global[s.name]=s.global;
                }
            }
        }
    }

    // Apply relocations
    for(size_t i=0;i<objs.size();i++){
        for(auto& r : objs[i].rels){
            size_t pos = bases[i] + r.offset;
            // find target symbol address
            auto it = sym_addr.find(r.symbol);
            std::uint64_t target = (it!=sym_addr.end()) ? it->second : 0;
            // patch instruction at pos (little-endian 32-bit)
            std::uint32_t insn = text[pos] | (text[pos+1]<<8) | (text[pos+2]<<16) | (text[pos+3]<<24);
            std::uint64_t pc = code_base_va + pos;
            std::int64_t delta = static_cast<std::int64_t>(target - pc);
            if(r.type==283 || r.type==282){ // CALL26/JUMP26
                std::int32_t imm26 = static_cast<std::int32_t>((delta>>2) & 0x3FFFFFF);
                insn = (insn & ~0x3FFFFFFu) | (static_cast<std::uint32_t>(imm26) & 0x3FFFFFFu);
            } else if(r.type==280){ // CONDBR19
                std::int32_t imm19 = static_cast<std::int32_t>((delta>>2) & 0x7FFFF);
                insn = (insn & ~0x00FFFFE0u) | ((static_cast<std::uint32_t>(imm19) & 0x7FFFF)<<5);
            }
            // write back
            text[pos]=static_cast<std::uint8_t>(insn&0xffu); text[pos+1]=static_cast<std::uint8_t>((insn>>8)&0xffu); text[pos+2]=static_cast<std::uint8_t>((insn>>16)&0xffu); text[pos+3]=static_cast<std::uint8_t>((insn>>24)&0xffu);
        }
    }

    std::vector<std::uint8_t> debug_line;
    std::vector<std::uint8_t> debug_info;
    std::vector<std::uint8_t> debug_abbrev;
    for (auto& o : objs) if (!o.debug_line.empty() && debug_line.empty()) debug_line = o.debug_line;
    for (auto& o : objs) if (!o.debug_info.empty() && debug_info.empty()) debug_info = o.debug_info;
    for (auto& o : objs) if (!o.debug_abbrev.empty() && debug_abbrev.empty()) debug_abbrev = o.debug_abbrev;

    // entry point / runtime startup
    bool need_start = (sym_addr.find("_start") == sym_addr.end());
    size_t startup_size = need_start ? 12 : 0;
    if (need_start) {
        // adjust all symbol addresses for prepended stub
        for (auto& kv : sym_addr) kv.second += startup_size;
        // determine main address (after adjustment)
        std::uint64_t main_addr = code_base_va + startup_size;
        auto mit = sym_addr.find("main");
        if (mit != sym_addr.end()) main_addr = mit->second;
        // prepend startup stub: bl main; mov x8,#93; svc #0
        std::int64_t off_bytes = static_cast<std::int64_t>(main_addr) - static_cast<std::int64_t>(code_base_va);
        std::uint32_t bl_imm = static_cast<std::uint32_t>((off_bytes / 4) & 0x3FFFFFFu);
        std::uint32_t bl_main = 0x94000000u | bl_imm;
        std::uint32_t mov_x8 = 0xD2800BA8u;
        std::uint32_t svc0 = 0xD4000001u;
        std::vector<std::uint8_t> stub;
        auto push32=[&](std::uint32_t v){ for(int i=0;i<4;i++){ stub.push_back(static_cast<std::uint8_t>(v&0xffu)); v>>=8;} };
        push32(bl_main); push32(mov_x8); push32(svc0);
        std::vector<std::uint8_t> full;
        full.insert(full.end(), stub.begin(), stub.end());
        full.insert(full.end(), text.begin(), text.end());
        text.swap(full);
    }
    if (need_start) sym_global["_start"] = true;
    std::uint64_t entry = code_base_va;
    if (!need_start && sym_addr.count("_start")) entry = sym_addr["_start"];
    else if (!need_start && sym_addr.count("main")) entry = sym_addr["main"];

    // Write ELF executable with section headers and symbol table.
    using namespace tracey::elf;

    // Filter symbols for a cleaner, GCC-like table.
    std::vector<std::pair<std::string,std::uint64_t>> final_syms;
    for (auto& kv : sym_addr) {
        const std::string& name = kv.first;
        if (name.empty()) continue;
        if (name.rfind(".L", 0) == 0) continue;
        auto git = sym_global.find(name);
        if (git == sym_global.end() || !git->second) continue;
        final_syms.push_back(kv);
    }
    // Ensure _start is present when a startup stub was prepended.
    if (need_start) {
        bool found = false;
        for (auto& kv : final_syms) if (kv.first == "_start") { found = true; break; }
        if (!found) final_syms.push_back({"_start", code_base_va});
    }

    // Build string table for symbols.
    std::string strtab; strtab.push_back('\0');
    auto addstr = [&](const std::string& s) -> size_t {
        size_t idx = strtab.size();
        strtab += s;
        strtab.push_back('\0');
        return idx;
    };
    std::vector<size_t> name_idx(final_syms.size());
    for (size_t i = 0; i < final_syms.size(); i++) {
        name_idx[i] = addstr(final_syms[i].first);
    }

    // Build symbol table entries (null entry first).
    std::vector<Sym> symtab;
    symtab.push_back(Sym{}); // null
    for (size_t i = 0; i < final_syms.size(); i++) {
        Sym sym{};
        sym.st_name = static_cast<std::uint32_t>(name_idx[i]);
        bool is_global = true;
        auto git = sym_global.find(final_syms[i].first);
        if (git != sym_global.end()) is_global = git->second;
        sym.st_info = static_cast<unsigned char>((is_global ? (STB_GLOBAL << 4) : (STB_LOCAL << 4)) | STT_FUNC);
        sym.st_other = 0;
        sym.st_shndx = 1; // .text
        sym.st_value = final_syms[i].second;
        sym.st_size = 0;
        symtab.push_back(sym);
    }

    // Section name string table.
    std::string shstrtab; shstrtab.push_back('\0');
    auto addsh = [&](const std::string& s) -> size_t {
        size_t idx = shstrtab.size();
        shstrtab += s;
        shstrtab.push_back('\0');
        return idx;
    };
    size_t idx_text_name = addsh(".text");
    size_t idx_symtab_name = addsh(".symtab");
    size_t idx_strtab_name = addsh(".strtab");
    size_t idx_shstrtab_name = addsh(".shstrtab");
    size_t idx_debug_line_name = 0;
    if (!debug_line.empty()) idx_debug_line_name = addsh(".debug_line");
    size_t idx_debug_info_name = 0;
    if (!debug_info.empty()) idx_debug_info_name = addsh(".debug_info");
    size_t idx_debug_abbrev_name = 0;
    if (!debug_abbrev.empty()) idx_debug_abbrev_name = addsh(".debug_abbrev");

    // Compute offsets.
    size_t off = text_file_off + text.size();
    // align to 8 for symtab
    while (off % 8 != 0) off++;
    size_t symtab_off = off;
    size_t symtab_size = symtab.size() * sizeof(Sym);
    off += symtab_size;
    while (off % 8 != 0) off++;
    size_t strtab_off = off;
    size_t strtab_size = strtab.size();
    off += strtab_size;
    while (off % 8 != 0) off++;
    size_t shstrtab_off = off;
    size_t shstrtab_size = shstrtab.size();
    off += shstrtab_size;
    while (off % 8 != 0) off++;
    size_t debug_line_off = off;
    size_t debug_line_size = debug_line.size();
    if (!debug_line.empty()) off += debug_line_size;
    while (off % 8 != 0) off++;
    size_t debug_info_off = off;
    size_t debug_info_size = debug_info.size();
    if (!debug_info.empty()) off += debug_info_size;
    while (off % 8 != 0) off++;
    size_t debug_abbrev_off = off;
    size_t debug_abbrev_size = debug_abbrev.size();
    if (!debug_abbrev.empty()) off += debug_abbrev_size;
    while (off % 8 != 0) off++;
    size_t shdr_off = off;

    // Write ELF.
    std::ofstream f(output, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    Ehdr ehdr{};
    unsigned char ident[16] = {0x7f,'E','L','F',2,1,1,0,0,0,0,0,0,0,0,0};
    std::memcpy(ehdr.e_ident, ident, 16);
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_version = 1;
    ehdr.e_entry = entry;
    ehdr.e_phoff = 64;
    ehdr.e_shoff = shdr_off;
    ehdr.e_ehsize = 64;
    ehdr.e_phentsize = 56;
    ehdr.e_phnum = 1;
    ehdr.e_shentsize = 64;
    ehdr.e_shnum = static_cast<std::uint16_t>(5 + (debug_line.empty()?0:1) + (debug_info.empty()?0:1) + (debug_abbrev.empty()?0:1));
    ehdr.e_shstrndx = 4;

    Phdr ph{};
    ph.p_type = PT_LOAD;
    ph.p_flags = PF_R | PF_X;
    ph.p_offset = 0;
    ph.p_vaddr = base_va;
    ph.p_paddr = base_va;
    ph.p_filesz = text_file_off + text.size();
    ph.p_memsz = text_file_off + text.size();
    ph.p_align = 0x1000;

    auto w16 = [&f](std::uint16_t v){ f.put(static_cast<char>(v & 0xffu)); f.put(static_cast<char>((v >> 8) & 0xffu)); };
    auto w32 = [&f](std::uint32_t v){ for (int i = 0; i < 4; i++) { f.put(static_cast<char>(v & 0xffu)); v >>= 8; } };
    auto w64 = [&f](std::uint64_t v){ for (int i = 0; i < 8; i++) { f.put(static_cast<char>(v & 0xffu)); v >>= 8; } };

    for (int i = 0; i < 16; i++) f.put(static_cast<char>(ident[i]));
    w16(ehdr.e_type); w16(ehdr.e_machine); w32(ehdr.e_version);
    w64(ehdr.e_entry); w64(ehdr.e_phoff); w64(ehdr.e_shoff);
    w32(ehdr.e_flags); w16(ehdr.e_ehsize); w16(ehdr.e_phentsize);
    w16(ehdr.e_phnum); w16(ehdr.e_shentsize); w16(ehdr.e_shnum); w16(ehdr.e_shstrndx);

    // program header
    w32(ph.p_type); w32(ph.p_flags); w64(ph.p_offset); w64(ph.p_vaddr);
    w64(ph.p_paddr); w64(ph.p_filesz); w64(ph.p_memsz); w64(ph.p_align);

    // pad to text offset
    size_t cur = 64 + 56;
    while (cur < text_file_off) { f.put(0); cur++; }
    f.write(reinterpret_cast<const char*>(text.data()), static_cast<std::streamsize>(text.size()));

    // symtab
    while (f.tellp() < static_cast<std::streamoff>(symtab_off)) f.put(0);
    for (auto& sym : symtab) {
        write_sym(f, sym);
    }

    // strtab
    while (f.tellp() < static_cast<std::streamoff>(strtab_off)) f.put(0);
    f.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));

    // shstrtab
    while (f.tellp() < static_cast<std::streamoff>(shstrtab_off)) f.put(0);
    f.write(shstrtab.data(), static_cast<std::streamsize>(shstrtab.size()));

    // debug_line
    if (!debug_line.empty()) {
        while (f.tellp() < static_cast<std::streamoff>(debug_line_off)) f.put(0);
        f.write(reinterpret_cast<const char*>(debug_line.data()), static_cast<std::streamsize>(debug_line.size()));
    }
    // debug_info
    if (!debug_info.empty()) {
        while (f.tellp() < static_cast<std::streamoff>(debug_info_off)) f.put(0);
        f.write(reinterpret_cast<const char*>(debug_info.data()), static_cast<std::streamsize>(debug_info.size()));
    }
    // debug_abbrev
    if (!debug_abbrev.empty()) {
        while (f.tellp() < static_cast<std::streamoff>(debug_abbrev_off)) f.put(0);
        f.write(reinterpret_cast<const char*>(debug_abbrev.data()), static_cast<std::streamsize>(debug_abbrev.size()));
    }

    // section headers
    while (f.tellp() < static_cast<std::streamoff>(shdr_off)) f.put(0);

    // null
    Shdr sh_null{}; write_shdr(f, sh_null);

    // .text
    Shdr sh_text{};
    sh_text.sh_name = static_cast<std::uint32_t>(idx_text_name);
    sh_text.sh_type = SHT_PROGBITS;
    sh_text.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    sh_text.sh_addr = code_base_va;
    sh_text.sh_offset = text_file_off;
    sh_text.sh_size = text.size();
    sh_text.sh_addralign = 16;
    write_shdr(f, sh_text);

    // .symtab
    Shdr sh_sym{};
    sh_sym.sh_name = static_cast<std::uint32_t>(idx_symtab_name);
    sh_sym.sh_type = SHT_SYMTAB;
    sh_sym.sh_offset = symtab_off;
    sh_sym.sh_size = symtab_size;
    sh_sym.sh_link = 3; // .strtab
    sh_sym.sh_info = 1;
    sh_sym.sh_addralign = 8;
    sh_sym.sh_entsize = 24;
    write_shdr(f, sh_sym);

    // .strtab
    Shdr sh_str{};
    sh_str.sh_name = static_cast<std::uint32_t>(idx_strtab_name);
    sh_str.sh_type = SHT_STRTAB;
    sh_str.sh_offset = strtab_off;
    sh_str.sh_size = strtab_size;
    sh_str.sh_addralign = 1;
    write_shdr(f, sh_str);

    // .shstrtab
    Shdr sh_shstr{};
    sh_shstr.sh_name = static_cast<std::uint32_t>(idx_shstrtab_name);
    sh_shstr.sh_type = SHT_STRTAB;
    sh_shstr.sh_offset = shstrtab_off;
    sh_shstr.sh_size = shstrtab_size;
    sh_shstr.sh_addralign = 1;
    write_shdr(f, sh_shstr);

    if (!debug_line.empty()) {
        Shdr sh_dbg{};
        sh_dbg.sh_name = static_cast<std::uint32_t>(idx_debug_line_name);
        sh_dbg.sh_type = SHT_PROGBITS;
        sh_dbg.sh_offset = debug_line_off;
        sh_dbg.sh_size = debug_line_size;
        sh_dbg.sh_addralign = 1;
        write_shdr(f, sh_dbg);
    }
    if (!debug_info.empty()) {
        Shdr sh{};
        sh.sh_name = static_cast<std::uint32_t>(idx_debug_info_name);
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = debug_info_off;
        sh.sh_size = debug_info_size;
        sh.sh_addralign = 1;
        write_shdr(f, sh);
    }
    if (!debug_abbrev.empty()) {
        Shdr sh{};
        sh.sh_name = static_cast<std::uint32_t>(idx_debug_abbrev_name);
        sh.sh_type = SHT_PROGBITS;
        sh.sh_offset = debug_abbrev_off;
        sh.sh_size = debug_abbrev_size;
        sh.sh_addralign = 1;
        write_shdr(f, sh);
    }

    f.close();
    return true;
}

} // namespace tracey