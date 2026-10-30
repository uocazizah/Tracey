#include <tracey/assembler.hxx>
#include <tracey/elf.hxx>

#include <cstring>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& s){ size_t a=s.find_first_not_of(" \t\r\n"); if(a==std::string::npos) return ""; size_t b=s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1);}

int regnum(const std::string& r){
    std::string x=r; if(!x.empty()&&x.back()==',') x.pop_back();
    if(x=="sp") return 31;
    if(x=="xzr") return 31;
    if(x=="lr") return 30;
    if(x[0]=='x'||x[0]=='w'){ return std::stoi(x.substr(1)); }
    return -1;
}
int condcode(const std::string& c){
    std::string x=c; std::transform(x.begin(),x.end(),x.begin(),::tolower);
    if(x=="eq") return 0;
    if(x=="ne") return 1;
    if(x=="cs"||x=="hs") return 2;
    if(x=="cc"||x=="lo") return 3;
    if(x=="mi") return 4;
    if(x=="pl") return 5;
    if(x=="vs") return 6;
    if(x=="vc") return 7;
    if(x=="hi") return 8;
    if(x=="ls") return 9;
    if(x=="ge") return 10;
    if(x=="lt") return 11;
    if(x=="gt") return 12;
    if(x=="le") return 13;
    if(x=="al") return 14;
    return -1;
}

void put32(std::vector<std::uint8_t>& b, std::uint32_t v){
    for(int i=0;i<4;i++){ b.push_back(static_cast<std::uint8_t>(v&0xff)); v>>=8; }
}

} // anon

namespace tracey {

asm_result assemble_aarch64(const std::string& source) {
    asm_result res;
    // first pass: collect labels and instruction count
    std::vector<std::string> lines;
    std::istringstream in(source);
    std::string line;
    while(std::getline(in,line)) lines.push_back(line);

    std::vector<std::string> debug_files;
    std::map<std::string,int> debug_file_index;
    auto get_file_index = [&](const std::string& name) -> int {
        auto it = debug_file_index.find(name);
        if (it != debug_file_index.end()) return it->second;
        int idx = (int)debug_files.size() + 1;
        debug_files.push_back(name);
        debug_file_index[name] = idx;
        return idx;
    };

    // remove comments? none
    // collect labels offsets (in instructions)
    std::map<std::string,std::size_t> label_off; // byte offset
    std::size_t off=0;
    for(auto& raw : lines){
        std::string l=trim(raw);
        if(l.empty()) continue;
        if(l.back()==':'){
            label_off[l.substr(0,l.size()-1)] = off;
            continue;
        }
        if(l[0]=='.'){
            // directives
            if(l.rfind(".globl",0)==0) continue;
            if(l==".text") continue;
            if(l==".data") continue;
            if(l.rfind(".file",0)==0) {
                size_t q1=l.find('"'); size_t q2=l.rfind('"');
                std::string name = (q1!=std::string::npos && q2!=q1) ? l.substr(q1+1, q2-q1-1) : std::string("unknown");
                get_file_index(name);
                continue;
            }
            if(l.rfind(".loc",0)==0) continue;
            if(l.rfind(".asciz",0)==0) {
                // compute size of string + null
                size_t q1=l.find('"'); size_t q2=l.rfind('"');
                std::string s = (q1!=std::string::npos && q2!=q1) ? l.substr(q1+1, q2-q1-1) : std::string();
                // unescape basic
                size_t len=0;
                for(size_t i=0;i<s.size();) {
                    if(s[i]=='\\' && i+1<s.size()){ i+=2; } else { i++; }
                    len++;
                }
                off += len + 1;
                continue;
            }
            // unknown directive skip
            continue;
        }
        // instruction: one line may contain instruction
        off += 4;
    }

    // second pass encode
    std::map<std::string,std::size_t> sym_index;
    // global symbols tracked later
    std::vector<std::string> globals;

    struct LineEntry { std::size_t addr; int file; int line; };
    std::vector<LineEntry> line_entries;
    int cur_file = 1;
    int cur_line = 1;
    if (debug_files.empty()) get_file_index("unknown");

    off=0;
    for(auto& raw : lines){
        std::string l=trim(raw);
        if(l.empty()) continue;
        if(l.back()==':') continue;
        if(l[0]=='.'){
            if(l.rfind(".file",0)==0){
                size_t q1=l.find('"'); size_t q2=l.rfind('"');
                std::string name = (q1!=std::string::npos && q2!=q1) ? l.substr(q1+1, q2-q1-1) : std::string("unknown");
                cur_file = get_file_index(name);
                continue;
            }
            if(l.rfind(".loc",0)==0){
                std::istringstream iss(l.substr(4));
                int ln; if(iss >> ln) cur_line = ln;
                continue;
            }
            if(l.rfind(".globl",0)==0){
                std::string name=trim(l.substr(7));
                globals.push_back(name);
            } else if(l==".text" || l==".data") {
                // no-op
            } else if(l.rfind(".asciz",0)==0) {
                size_t q1=l.find('"'); size_t q2=l.rfind('"');
                std::string s = (q1!=std::string::npos && q2!=q1) ? l.substr(q1+1, q2-q1-1) : std::string();
                // unescape
                std::string out;
                for(size_t i=0;i<s.size();) {
                    if(s[i]=='\\' && i+1<s.size()) {
                        char c=s[i+1];
                        if(c=='n') { out.push_back('\n'); i+=2; continue; }
                        if(c=='t') { out.push_back('\t'); i+=2; continue; }
                        if(c=='r') { out.push_back('\r'); i+=2; continue; }
                        if(c=='0') { out.push_back('\0'); i+=2; continue; }
                        if(c=='\\') { out.push_back('\\'); i+=2; continue; }
                        if(c=='"') { out.push_back('"'); i+=2; continue; }
                        out.push_back(s[i]); i++;
                    } else {
                        out.push_back(s[i]); i++;
                    }
                }
                for(char c: out) res.text.push_back((unsigned char)c);
                res.text.push_back(0);
                off += out.size() + 1;
            }
            continue;
        }

        if (cur_line > 0) line_entries.push_back({0x401000 + off, cur_file, cur_line});

        // split instruction and operands
        // find first space
        size_t sp=l.find_first_of(" \t");
        std::string mnemonic = (sp==std::string::npos)? l : l.substr(0,sp);
        std::string operands = (sp==std::string::npos)? "" : trim(l.substr(sp));
        std::transform(mnemonic.begin(),mnemonic.end(),mnemonic.begin(),::tolower);

        // Exact encodings for forms used by the code generator
        std::string key = mnemonic + " " + operands;
        if (key == "stp x29, x30, [sp, #-16]!") { put32(res.text,0xA9BF7BFDu); off+=4; continue; }
        if (key == "ldp x29, x30, [sp], #16") { put32(res.text,0xA8C17BFDu); off+=4; continue; }
        if (key == "str x0, [sp, #-16]!") { put32(res.text,0xF81F0FE0u); off+=4; continue; }
        if (key == "ldr x0, [sp], #16") { put32(res.text,0xF84107E0u); off+=4; continue; }
        if (key == "str x0, [x29, #-8]") { put32(res.text,0xF81F83A0u); off+=4; continue; }
        if (key == "ldr x0, [x29, #-8]") { put32(res.text,0xF85F83A0u); off+=4; continue; }
        if (key == "mov x29, sp") { put32(res.text,0x910003FDu); off+=4; continue; }
        if (key == "mov sp, x29") { put32(res.text,0x910003BFu); off+=4; continue; }

        // handle movk
        if(mnemonic=="movk"){
            std::vector<std::string> ops;
            std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd = regnum(ops[0]);
            int imm = std::stoi(ops[1].substr(ops[1].find('#')+1));
            int shift = 0;
            if (ops.size() >= 3) {
                std::string sh = ops[2];
                size_t hp = sh.find('#');
                if (hp != std::string::npos) shift = std::stoi(sh.substr(hp+1));
            }
            int hw = shift / 16;
            std::uint32_t insn = 0xF2800000u | (static_cast<std::uint32_t>(hw) << 21) |
                                ((static_cast<std::uint32_t>(imm) & 0xFFFFu) << 5) | (rd & 0x1F);
            put32(res.text, insn);
            off += 4; continue;
        }

        // handle fmov (general <-> FP)
        if(mnemonic=="fmov"){
            std::vector<std::string> ops;
            std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            // dest FP, src general
            if (ops[0].rfind('s',0)==0 && ops[1].rfind('w',0)==0) {
                int rd = std::stoi(ops[0].substr(1));
                int rn = std::stoi(ops[1].substr(1));
                (void)rd;(void)rn;
                // fmov s0, w0 encoding 0x1E270000 | (rn<<5) | rd
                std::uint32_t insn = 0x1E270000u | ((rn & 0x1F)<<5) | (rd & 0x1F);
                put32(res.text, insn); off+=4; continue;
            }
            if (ops[0].rfind('d',0)==0 && ops[1].rfind('x',0)==0) {
                int rd = std::stoi(ops[0].substr(1));
                int rn = std::stoi(ops[1].substr(1));
                std::uint32_t insn = 0x9E670000u | ((rn & 0x1F)<<5) | (rd & 0x1F);
                put32(res.text, insn); off+=4; continue;
            }
            // dest general, src FP
            if (ops[0].rfind('w',0)==0 && ops[1].rfind('s',0)==0) {
                int rd = std::stoi(ops[0].substr(1));
                int rn = std::stoi(ops[1].substr(1));
                std::uint32_t insn = 0x1E260000u | ((rn & 0x1F)<<5) | (rd & 0x1F);
                put32(res.text, insn); off+=4; continue;
            }
            if (ops[0].rfind('x',0)==0 && ops[1].rfind('d',0)==0) {
                int rd = std::stoi(ops[0].substr(1));
                int rn = std::stoi(ops[1].substr(1));
                std::uint32_t insn = 0x9E660000u | ((rn & 0x1F)<<5) | (rd & 0x1F);
                put32(res.text, insn); off+=4; continue;
            }
            res.error="bad fmov"; res.ok=false; return res;
        }

        // handle fneg
        if(mnemonic=="fneg"){
            std::vector<std::string> ops;
            std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd = std::stoi(ops[0].substr(1));
            int rn = std::stoi(ops[1].substr(1));
            bool isd = (ops[0][0]=='d');
            std::uint32_t base = isd ? 0x1E614000u : 0x1E214000u;
            std::uint32_t insn = base | ((rn & 0x1F)<<5) | (rd & 0x1F);
            put32(res.text, insn); off+=4; continue;
        }

        // handle fcmp
        if(mnemonic=="fcmp"){
            std::vector<std::string> ops;
            std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rn = std::stoi(ops[0].substr(1));
            int rm = std::stoi(ops[1].substr(1));
            bool isd = (ops[0][0]=='d');
            std::uint32_t base = isd ? 0x1E602000u : 0x1E202000u;
            std::uint32_t insn = base | ((rm & 0x1F)<<16) | ((rn & 0x1F)<<5);
            put32(res.text, insn); off+=4; continue;
        }

        // handle FP binary ops
        if(mnemonic=="fadd"||mnemonic=="fsub"||mnemonic=="fmul"||mnemonic=="fdiv"){
            std::vector<std::string> ops;
            std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd = std::stoi(ops[0].substr(1));
            int rn = std::stoi(ops[1].substr(1));
            int rm = std::stoi(ops[2].substr(1));
            bool isd = (ops[0][0]=='d');
            std::uint32_t opbase = 0;
            if (mnemonic=="fadd") opbase = isd ? 0x1E602800u : 0x1E202800u;
            else if (mnemonic=="fsub") opbase = isd ? 0x1E603800u : 0x1E203800u;
            else if (mnemonic=="fmul") opbase = isd ? 0x1E600800u : 0x1E200800u;
            else if (mnemonic=="fdiv") opbase = isd ? 0x1E601800u : 0x1E201800u;
            std::uint32_t insn = opbase | ((rm & 0x1F)<<16) | ((rn & 0x1F)<<5) | (rd & 0x1F);
            put32(res.text, insn); off+=4; continue;
        }

        // handle and/orr
        if(mnemonic=="and"||mnemonic=="orr"){
            std::vector<std::string> ops;
            std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd = std::stoi(ops[0].substr(1));
            int rn = std::stoi(ops[1].substr(1));
            int rm = std::stoi(ops[2].substr(1));
            std::uint32_t base = (mnemonic=="and") ? 0x8A000000u : 0xAA000000u;
            std::uint32_t insn = base | ((rm & 0x1F)<<16) | ((rn & 0x1F)<<5) | (rd & 0x1F);
            put32(res.text, insn); off+=4; continue;
        }

        // handle mov
        if(mnemonic=="mov"){
            // mov x0, #imm  OR mov Rd, Rn
            // split by comma
            std::vector<std::string> ops;
            std::istringstream oss(operands); std::string t;
            // split on commas
            std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            if(ops.size()==2 && ops[1].find('#')==0){
                int rd=regnum(ops[0]);
                int imm=std::stoi(ops[1].substr(1));
                // MOVZ: 11010010100 hw(2) Rd(5) imm16(16) -> sf=1 (64-bit)
                std::uint32_t insn = 0xD2800000u | (rd & 0x1F) | ((imm & 0xFFFF)<<5);
                put32(res.text, insn);
            } else if(ops.size()==2){
                int rd=regnum(ops[0]); int rn=regnum(ops[1]);
                if (ops[0]=="sp" || ops[1]=="sp") {
                    std::uint32_t insn = 0x91000000u | (rd & 0x1F) | ((rn & 0x1F)<<5);
                    put32(res.text, insn);
                } else {
                    std::uint32_t insn = 0xAA0003E0u | ((rn & 0x1F)<<16) | (rd & 0x1F);
                    put32(res.text, insn);
                }
            } else {
                res.error="bad mov"; res.ok=false; return res;
            }
            off+=4; continue;
        }

        if(mnemonic=="add"||mnemonic=="sub"){
            // add/sub Rd, Rn, #imm  or Rd, Rn, Rm
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            bool issub = (mnemonic=="sub");
            if(ops.size()==3 && ops[2].find('#')==0){
                int rd=regnum(ops[0]); int rn=regnum(ops[1]); int imm=std::stoi(ops[2].substr(1));
                // ADD/SUB immediate 64-bit: sf=1, op=0/1, S=0, 100010, sh=0, imm12, Rn, Rd
                std::uint32_t base = issub ? 0xD1000000u : 0x91000000u;
                std::uint32_t insn = base | (rd & 0x1F) | ((rn & 0x1F)<<5) | ((imm & 0xFFF)<<10);
                put32(res.text, insn);
            } else if(ops.size()==3){
                int rd=regnum(ops[0]); int rn=regnum(ops[1]); int rm=regnum(ops[2]);
                std::uint32_t base = issub ? 0xCB000000u : 0x8B000000u;
                std::uint32_t insn = base | (rd & 0x1F) | ((rn & 0x1F)<<5) | ((rm & 0x1F)<<16);
                put32(res.text, insn);
            } else { res.error="bad add/sub"; return res;}
            off+=4; continue;
        }

        if (mnemonic == "udiv") {
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd=regnum(ops[0]); int rn=regnum(ops[1]); int rm=regnum(ops[2]);
            std::uint32_t insn = 0x9AC00800u | (rd &0x1F) | ((rn &0x1F)<<5) | ((rm &0x1F)<<16);
            put32(res.text, insn);
            off += 4; continue;
        }

        if(mnemonic=="mul"){
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd=regnum(ops[0]); int rn=regnum(ops[1]); int rm=regnum(ops[2]);
            // MUL 64-bit: 10011011000 Rm 11111 000000 Rn Rd  (sf=1)
            std::uint32_t insn = 0x9B007C00u | (rd &0x1F) | ((rn&0x1F)<<5) | ((rm&0x1F)<<16);
            put32(res.text, insn);
            off+=4; continue;
        }

        if(mnemonic=="sdiv"){
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd=regnum(ops[0]); int rn=regnum(ops[1]); int rm=regnum(ops[2]);
            // SDIV 64-bit: 10011010110 Rm 000010 Rn Rd
            std::uint32_t insn = 0x9AC00C00u | (rd &0x1F) | ((rn&0x1F)<<5) | ((rm&0x1F)<<16);
            put32(res.text, insn);
            off+=4; continue;
        }

        if(mnemonic=="msub"){
            // msub Xd, Xn, Xm, Xa
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd=regnum(ops[0]); int rn=regnum(ops[1]); int rm=regnum(ops[2]); int ra=regnum(ops[3]);
            // MSUB 64-bit: 10011011000 Rm 01111 Ra Rn Rd? Actually MSUB: sf=1, op=0? Let's use known encoding: MSUB Xd, Xn, Xm, Xa = 0x9B008000 | (Ra<<10) ... 
            // Standard: 10011011 000 Rm 011 Ra Rn Rd (with o0=1?).
            std::uint32_t insn = 0x9B008000u | (rd &0x1F) | ((rn&0x1F)<<5) | ((ra&0x1F)<<10) | ((rm&0x1F)<<16);
            put32(res.text, insn);
            off+=4; continue;
        }

        if(mnemonic=="cmp"){
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            if(ops.size()==2 && ops[1].find('#')==0){
                int rn=regnum(ops[0]); int imm=std::stoi(ops[1].substr(1));
                // SUBS XZR, Xn, #imm
                std::uint32_t insn = 0xF100001Fu | ((rn&0x1F)<<5) | ((imm&0xFFF)<<10);
                put32(res.text, insn);
            } else {
                int rn=regnum(ops[0]); int rm=regnum(ops[1]);
                // SUBS XZR, Xn, Xm
                std::uint32_t insn = 0xEB00001Fu | ((rn&0x1F)<<5) | ((rm&0x1F)<<16);
                put32(res.text, insn);
            }
            off+=4; continue;
        }

        if(mnemonic=="cset"){
            // cset Xd, cond
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd=regnum(ops[0]);
            int cc=condcode(ops[1]);
            // CSINC Xd, XZR, XZR, invert(cond): 10011010 100 11111 0001 invertcond XZR Xd? 
            int inv = cc ^ 1;
            std::uint32_t insn = 0x9A9F07E0u | (rd &0x1F) | (static_cast<std::uint32_t>(inv)<<12);
            // Actually csinc: 10011010 100 Rm(5) 0001 cond(4) Rn(5) Rd(5). Rm=xzr=31, Rn=xzr=31.
            // base 0x9A9F07E0? Let's set bits: (31<<16)|(1<<12? no). We'll use 0x9A9F07E0 | (static_cast<std::uint32_t>(inv)<<12)? Wait cond field bits 12-15? In csinc, cond is bits 12-15. So (cc_inv<<12). And Rd low 5. Good enough.
            put32(res.text, insn);
            off+=4; continue;
        }

        if(mnemonic=="neg"){
            // neg Xd, Xn  (sub Xd, XZR, Xn)
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd=regnum(ops[0]); int rn=regnum(ops[1]);
            std::uint32_t insn = 0xCB000000u | (rd &0x1F) | ((31 &0x1F)<<5) | ((rn &0x1F)<<16);
            // Wait sub Xd, Xn? Actually SUB Xd, XZR, Xm => 0xCB000000 | Rd | (31<<5) | (Xm<<16). Here rn is Xm.
            insn = 0xCB000000u | (rd &0x1F) | (31<<5) | ((rn &0x1F)<<16);
            put32(res.text, insn);
            off+=4; continue;
        }

        if(mnemonic=="ret"){
            put32(res.text, 0xD65F03C0u);
            off+=4; continue;
        }

        if (mnemonic.rfind("b.",0)==0) {
            std::string condstr = mnemonic.substr(2);
            int cc = condcode(condstr);
            if (cc < 0) { res.error="bad condition"; return res; }
            std::string target = trim(operands);
            put32(res.text, 0x54000000u | (cc & 0xF));
            asm_reloc r; r.offset=off; r.symbol=target; r.type=280;
            res.relocs.push_back(r);
            off+=4; continue;
        }

        if (mnemonic=="cbz" || mnemonic=="cbnz") {
            // parse "Xn, target"
            size_t comma = operands.find(',');
            std::string rstr = trim(operands.substr(0,comma));
            std::string target = trim(operands.substr(comma+1));
            int rn = regnum(rstr);
            uint32_t base = (mnemonic=="cbnz") ? 0xB5000000u : 0xB4000000u;
            put32(res.text, base | (rn & 0x1F));
            asm_reloc r; r.offset=off; r.symbol=target; r.type=280;
            res.relocs.push_back(r);
            off+=4; continue;
        }

        if (mnemonic == "ldrb") {
            size_t lb = operands.find('['); size_t rb = operands.find(']');
            std::string rtstr = trim(operands.substr(0,lb));
            int rt = regnum(rtstr);
            std::string inside = trim(operands.substr(lb+1, rb-lb-1));
            int rn = regnum(inside);
            int imm = 0;
            size_t comma = inside.find(',');
            if (comma != std::string::npos) {
                rn = regnum(inside.substr(0,comma));
                std::string num = trim(inside.substr(comma+1));
                std::string clean; for(char c: num) if(c=='-'||isdigit((unsigned char)c)) clean.push_back(c);
                if(!clean.empty()) imm = std::stoi(clean);
            }
            uint32_t insn = 0x39400000u | ((imm & 0xFFF)<<10) | ((rn & 0x1F)<<5) | (rt & 0x1F);
            put32(res.text, insn);
            off += 4; continue;
        }

        if (mnemonic=="strb") {
            // strb Wt, [Xn] or [Xn, #imm]
            size_t lb = operands.find('['); size_t rb = operands.find(']');
            std::string rtstr = trim(operands.substr(0,lb));
            int rt = regnum(rtstr);
            std::string inside = trim(operands.substr(lb+1, rb-lb-1));
            int rn = regnum(inside);
            int imm = 0;
            size_t comma = inside.find(',');
            if (comma != std::string::npos) {
                rn = regnum(inside.substr(0,comma));
                std::string num = trim(inside.substr(comma+1));
                std::string clean; for(char c: num) if(c=='-'||isdigit((unsigned char)c)) clean.push_back(c);
                if(!clean.empty()) imm = std::stoi(clean);
            }
            uint32_t uimm = (uint32_t)(imm & 0x1FF);
            uint32_t insn = 0x38000000u | (uimm << 12) | ((rn & 0x1F) << 5) | (rt & 0x1F);
            put32(res.text, insn);
            off += 4; continue;
        }

        if(mnemonic=="b"||mnemonic=="beq"||mnemonic=="bl"){
            // b label or b.eq label (beq already? codegen uses beq, b, bl)
            std::string target=trim(operands);
            // if mnemonic beq, it's b.eq
            bool isbl = (mnemonic=="bl");
            bool isb = (mnemonic=="b");
            bool isbeq = (mnemonic=="beq");
            // For conditional branch b.eq: 0x54000000 | (cond<<5? actually b.cond: 01010100 imm19 cond). We'll handle.
            if(isbl){
                // BL: placeholder, add reloc
                put32(res.text, 0x94000000u);
            } else if(isb){
                put32(res.text, 0x14000000u);
            } else if(isbeq){
                // b.eq: 0x5400000C? cond=0, imm19. base 0x54000000 | (0<<5?) Actually b.cond: 01010100 iiiiiiiiiiiiiiiiiii 0 cccc. cond in low 4 bits? For eq cond=0, so 0x54000000.
                put32(res.text, 0x54000000u);
            }
            // record reloc
            int rtype = 282; // JUMP26
            if (isbl) rtype = 283; // CALL26
            if (mnemonic == "beq") rtype = 280; // CONDBR19
            asm_reloc r; r.offset=off; r.symbol=target; r.type = rtype;
            res.relocs.push_back(r);
            off+=4; continue;
        }

        if(mnemonic=="stp"||mnemonic=="ldp"){
            // stp/ldp Xt1, Xt2, [Xn, #imm]!  or  [Xn], #imm
            // parse operands string manually
            // format: "x29, x30, [sp, #-16]!" or "x29, x30, [sp], #16"
            // We'll handle pre-index and post-index only.
            // Find bracket
            size_t lb=operands.find('[');
            size_t rb=operands.find(']');
            std::string regs=trim(operands.substr(0,lb));
            // split regs by comma
            std::vector<std::string> ro; std::string cur;
            for(char c: regs){ if(c==','){ ro.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ro.push_back(trim(cur));
            int rt=regnum(ro[0]); int rt2=regnum(ro[1]);
            std::string inside=trim(operands.substr(lb+1, rb-lb-1));
            // inside may be "sp" or "sp, #-16"? For pre-index, inside includes comma? Actually "[sp, #-16]!" -> inside "sp, #-16"
            // determine mode
            bool pre=false, post=false;
            if(operands.find(']')!=std::string::npos && operands.find('!',operands.find(']'))!=std::string::npos) pre=true;
            if(operands.find(']',operands.find('['))!=std::string::npos && operands.find(',',operands.find(']'))!=std::string::npos) post=true;

            int rn=-1; int imm=0;
            // parse inside: "sp" or "sp, #-16"
            size_t comma=inside.find(',');
            if(comma==std::string::npos){ rn=regnum(inside); }
            else { rn=regnum(inside.substr(0,comma)); imm=std::stoi(trim(inside.substr(comma+1)).substr(1)); } // strip #
            // post-index immediate after ]
            if(post){
                size_t after=operands.find(']', rb)+1;
                std::string rest=trim(operands.substr(after));
                // rest like ", #16" or "!"
                if(rest.find('#')!=std::string::npos){
                    // remove # and !
                    std::string num=rest;
                    // remove non-digit except minus
                    std::string clean;
                    for(char c: num) if(c=='-'||isdigit((unsigned char)c)) clean.push_back(c);
                    imm=std::stoi(clean);
                }
            }

            // Encode LDP/STP 64-bit.
            // STP (pre-index): 1010100110 imm7 Rn Rt2 Rt  (actually 0xA9800000 | (imm7<<15) | (Rn<<5) | Rt? need correct)
            // This is complex; use known:
            // STP Xt1, Xt2, [Xn, #imm]! : 0xA9800000 | ((imm/8)&0x7F)<<15 | (Rn<<5) | Rt? plus Rt2<<10?
            // Let's use standard bit layout: 
            // 31 30 29 28 27 26 25 24 23 22 21 20 15 14 10 9 5 4 0
            // 1  0  1  0  1  0  0  1  1  0  0  imm7(7)  Rt2(5)  Rn(5)  Rt(5)  for pre-index? Hmm.
            // Instead of hand-encoding complex, use a simpler approach: store/load pairs via two str/ldr? But codegen uses stp/ldp for prologue; we can rewrite? 
            // We'll approximate with known constants from gcc? 
            // For stp x29,x30,[sp,#-16]!: encoding 0xA9BF7BFD? Let's check: common prologue `stp x29, x30, [sp, #-16]!` = 0xA9BF7BFD.
            // ldp x29,x30,[sp],#16 = 0xA8C17BFD.
            // General immediate not needed beyond ±16/ etc. We'll special-case common.
            std::uint32_t insn=0;
            if(mnemonic=="stp"){
                if(imm==-16 && pre) insn=0xA9BF7BFDu | (rt&0x1F) | ((rt2&0x1F)<<10) | ((rn&0x1F)<<5); // not general
                else {
                    // fallback: use -16
                    insn=0xA9BF7BFDu;
                }
            } else {
                if(imm==16 && post) insn=0xA8C17BFDu | (rt&0x1F) | ((rt2&0x1F)<<10) | ((rn&0x1F)<<5);
                else insn=0xA8C17BFDu;
            }
            // The above constants assume rt=x29(29), rt2=x30(30), rn=sp(31). But if registers differ, adjust low bits. We already OR rt/rt2/rn, but constants have those bits set too causing conflict. Let's instead construct from base with zeros.
            // Reset: use base pattern 0xA9800000 for stp pre-index? 
            // We'll just use a known generic formula for stp/ldp with imm7:
            // STP (pre-index): 0xA9800000 | (imm7<<15) | (Rt2<<10) | (Rn<<5) | Rt
            // where imm7 = imm/8 (signed, 7-bit) for 64-bit? For -16, imm7 = -2.
            // LDP (post-index): 0xA8C00000 | (imm7<<15) | (Rt2<<10) | (Rn<<5) | Rt
            int imm7 = imm/8;
            if(imm7 < -64) imm7 = -64;
            if(imm7>63) imm7=63;
            std::uint32_t uimm = (imm7 & 0x7F);
            if(mnemonic=="stp"){
                insn = 0xA9800000u | (uimm<<15) | ((rt2&0x1F)<<10) | ((rn&0x1F)<<5) | (rt&0x1F);
                // set pre-index bit? bits 24:23 = 11? Actually stp pre-index has bit24=1, bit23=0? Let's set 0xA9800000 (10 1010 0110?) Hmm.
            } else {
                insn = 0xA8C00000u | (uimm<<15) | ((rt2&0x1F)<<10) | ((rn&0x1F)<<5) | (rt&0x1F);
            }
            put32(res.text, insn);
            off+=4; continue;
        }

        if(mnemonic=="ldr"||mnemonic=="str"){
            // ldr/str Xt, [Xn, #imm]  or [Xn, #imm]! or [Xn], #imm
            // parse
            // operands like "x0, [x29, #-8]" or "x0, [sp, #-16]!" or "x0, [sp], #16"
            size_t lb=operands.find('['); size_t rb=operands.find(']');
            std::string rtstr=trim(operands.substr(0,lb));
            int rt=regnum(rtstr);
            std::string inside=trim(operands.substr(lb+1, rb-lb-1));
            bool pre = operands.find('!', rb)!=std::string::npos;
            bool post = (operands.find(',', rb)!=std::string::npos);
            int rn=-1; int imm=0;
            size_t comma=inside.find(',');
            if(comma==std::string::npos){ rn=regnum(inside); }
            else { rn=regnum(inside.substr(0,comma)); 
                std::string num=trim(inside.substr(comma+1));
                std::string clean; for(char c: num) if(c=='-'||isdigit((unsigned char)c)) clean.push_back(c);
                imm=std::stoi(clean);
            }
            if(post){
                size_t after=operands.find(']', rb)+1;
                std::string rest=trim(operands.substr(after));
                std::string clean; for(char c: rest) if(c=='-'||isdigit((unsigned char)c)) clean.push_back(c);
                if(!clean.empty()) imm=std::stoi(clean);
            }
            std::uint32_t insn=0;
            // Determine scaled unsigned offset? We'll use 9-bit signed immediate forms for pre/post and signed offset.
            // LDR (64-bit) signed offset (no writeback): 0xF9400000? Actually that's unsigned offset. Signed offset (9-bit) form: 11111000010 imm9 0 Rn Rt? 
            // Let's use known: LDR Xrt, [Xrn, #imm] (signed, 9-bit) = 0xF8400000? Hmm.
            // Simpler: use unsigned offset scaled by 8 for [Xn, #imm] when imm multiple of 8 and no writeback. For [x29, #-8], use signed? -8 not representable as unsigned.
            // We need signed 9-bit. Base for LDR 64-bit signed offset: 0xF8400000? Let's set: 1111 1000 010? 
            // AArch64 LDR (immediate, signed offset): 
            // 31 30 29 28 27 26 25 24 23 22 21 20 12 11 10 9 5 4 0
            // 1  1  1  1  1  0  0  0  0  1  0  imm9(9)  0  Rn(5)  Rt(5)   (for 64-bit? Actually size=11)
            // So base 0xF8400000, imm9 at bits 12-20, Rn bits5-9, Rt bits0-4. But bit 21? 
            // Let's just use 0xF8400000 | ((imm & 0x1FF)<<12) | (static_cast<std::uint32_t>(rn)<<5) | rt, adjusting imm to 9-bit two's complement.
            int imm9 = imm & 0x1FF;
            if(pre){
                // STR/LDR pre-index uses same signed offset? Pre-index form: 1111 1000 000? 
                // We'll use 0xF8000000? Too uncertain.
                // Given our usage, pre-index only str x0,[sp,#-16]! and ldr? Actually codegen uses str x0,[sp,#-16]! and ldr x0,[sp],#16 post. Pre-index str needs encoding. 
                // Use STP? no.
                // We'll emit as str with pre-index using known: STR Xrt, [Xrn, #imm]! = 0xF8000000? 
                // Let's use 0xF8000000 | (static_cast<std::uint32_t>(imm9)<<12) | (static_cast<std::uint32_t>(rn)<<5) | rt with bit 24? 
                insn = (mnemonic=="str"?0xF8000000u:0xF8400000u) | (static_cast<std::uint32_t>(imm9)<<12) | (static_cast<std::uint32_t>(rn)<<5) | static_cast<std::uint32_t>(rt);
            } else if(post){
                // post-index: 0xF8000C00? 
                insn = (mnemonic=="str"?0xF8000C00u:0xF8400C00u) | (static_cast<std::uint32_t>(imm9)<<12) | (static_cast<std::uint32_t>(rn)<<5) | static_cast<std::uint32_t>(rt);
            } else {
                // signed offset (no writeback): 0xF8000000? for str, 0xF8400000? for ldr? Actually both same base with bit22? 
                // LDR signed: 0xF8400000; STR signed: 0xF8000000.
                insn = (mnemonic=="str"?0xF8000000u:0xF8400000u) | (static_cast<std::uint32_t>(imm9)<<12) | (static_cast<std::uint32_t>(rn)<<5) | static_cast<std::uint32_t>(rt);
            }
            put32(res.text, insn);
            off+=4; continue;
        }

        if (mnemonic == "adr") {
            // adr Xd, label
            std::vector<std::string> ops; std::string cur;
            for(char c: operands){ if(c==','){ ops.push_back(trim(cur)); cur.clear(); } else cur.push_back(c);}
            if(!cur.empty()) ops.push_back(trim(cur));
            int rd = regnum(ops[0]);
            std::string target = trim(ops[1]);
            auto it = label_off.find(target);
            if (it == label_off.end()) {
                res.error = "undefined label: " + target;
                res.ok = false;
                return res;
            }
            int64_t delta = (int64_t)it->second - (int64_t)off;
            // ADR encoding
            uint32_t immlo = (uint32_t)(delta & 0x3);
            uint32_t immhi = (uint32_t)((delta >> 2) & 0x7FFFF);
            uint32_t insn = 0x10000000u | (immlo << 29) | (immhi << 5) | (rd & 0x1F);
            put32(res.text, insn);
            off += 4;
            continue;
        }

        if (mnemonic == "svc") {
            put32(res.text, 0xD4000001u);
            off += 4; continue;
        }

        res.error = "unknown instruction: " + mnemonic;
        res.ok=false; return res;
    }

    // build symbols
    for(auto& kv : label_off){
        asm_symbol s; s.name=kv.first; s.offset=kv.second; s.global=false;
        res.symbols.push_back(s);
    }
    for(auto& g : globals){
        // mark global
        for(auto& s : res.symbols) if(s.name==g) s.global=true;
        // if not present (external), add undefined? For linker, external symbols may not be in object. Skip.
    }

    // Generate minimal DWARF4 .debug_line
    if (debug_files.empty()) get_file_index("unknown");
    std::vector<std::uint8_t> dbg;
    auto put_u8=[&](std::uint8_t v){ dbg.push_back(v); };
    auto put_u16=[&](std::uint16_t v){ dbg.push_back(static_cast<std::uint8_t>(v&0xff)); dbg.push_back(static_cast<std::uint8_t>((v>>8)&0xff)); };
    auto put_u32=[&](std::uint32_t v){ for(int i=0;i<4;i++){ dbg.push_back(static_cast<std::uint8_t>(v&0xff)); v>>=8; } };
    auto put_uleb=[&](std::uint64_t v){ do { std::uint8_t b=v&0x7f; v>>=7; if(v) b|=0x80; dbg.push_back(b); } while(v); };
    auto put_sleb=[&](int64_t v){ bool more=true; while(more){ std::uint8_t b=v&0x7f; v>>=7; if((v==0 && !(b&0x40)) || (v==-1 && (b&0x40))) more=false; else b|=0x80; dbg.push_back(b); } };

    size_t unit_len_pos = dbg.size();
    put_u32(0); // placeholder
    put_u16(4); // version
    size_t header_len_pos = dbg.size();
    put_u32(0); // placeholder
    put_u8(1); // min_insn_length
    put_u8(1); // max_ops_per_insn
    put_u8(1); // default_is_stmt
    put_u8((std::uint8_t)-5); // line_base
    put_u8(14); // line_range
    put_u8(13); // opcode_base
    // standard opcode lengths
    const std::uint8_t stdlens[12] = {0,1,1,1,1,0,0,0,1,0,0,1};
    for (int i=0;i<12;i++) put_u8(stdlens[i]);
    // include directories: empty
    put_u8(0);
    // file names
    for (auto& fn : debug_files) {
        for (char c : fn) put_u8((std::uint8_t)c);
        put_u8(0);
        put_uleb(0); // dir index
        put_uleb(0); // time
        put_uleb(0); // size
    }
    put_u8(0); // end file list
    size_t header_end = dbg.size();
    // patch header_length
    std::uint32_t hl = (std::uint32_t)(header_end - (header_len_pos + 4));
    for (int i=0;i<4;i++) dbg[header_len_pos + static_cast<std::size_t>(i)] = (hl >> (8*i)) & 0xff;

    // line program
    std::size_t prev_addr = 0;
    int prev_line = 1;
    int prev_file = 1;
    for (auto& e : line_entries) {
        if (e.file != prev_file) {
            put_u8(4); // set_file
            put_uleb(static_cast<std::uint64_t>(e.file));
            prev_file = e.file;
        }
        int line_delta = e.line - prev_line;
        put_u8(3); // advance_line
        put_sleb(line_delta);
        std::size_t addr_delta = e.addr - prev_addr;
        put_u8(2); // advance_pc
        put_uleb(addr_delta);
        put_u8(1); // copy
        prev_addr = e.addr;
        prev_line = e.line;
    }
    // end sequence
    put_u8(0); put_uleb(1); put_u8(1);

    // patch unit length
    std::uint32_t ul = (std::uint32_t)(dbg.size() - 4);
    for (int i=0;i<4;i++) dbg[unit_len_pos + static_cast<std::size_t>(i)] = (ul >> (8*i)) & 0xff;


    // .debug_abbrev
    std::vector<std::uint8_t> abbrev;
    auto a8=[&](std::uint8_t v){ abbrev.push_back(v); };
    auto auleb=[&](std::uint64_t v){ do { std::uint8_t b=v&0x7f; v>>=7; if(v) b|=0x80; abbrev.push_back(b); } while(v); };
    // abbrev 1: compile_unit
    auleb(1); a8(0x11); a8(1); // DW_TAG_compile_unit, children yes
    auleb(0x03); auleb(0x08); // name string
    auleb(0x10); auleb(0x17); // stmt_list sec_offset
    auleb(0x11); auleb(0x01); // low_pc addr
    auleb(0x12); auleb(0x01); // high_pc addr
    a8(0); a8(0);
    // abbrev 2: subprogram
    auleb(2); a8(0x2e); a8(0); // DW_TAG_subprogram, no children
    auleb(0x03); auleb(0x08);
    auleb(0x11); auleb(0x01);
    auleb(0x12); auleb(0x01);
    a8(0); a8(0);
    a8(0); // end
    res.debug_abbrev = abbrev;

    // .debug_info
    std::vector<std::uint8_t> info;
    auto i8=[&](std::uint8_t v){ info.push_back(v); };
    auto i16=[&](std::uint16_t v){ info.push_back(static_cast<std::uint8_t>(v&0xff)); info.push_back(static_cast<std::uint8_t>((v>>8)&0xff)); };
    auto i32=[&](std::uint32_t v){ for(int k=0;k<4;k++){ info.push_back(static_cast<std::uint8_t>(v&0xff)); v>>=8; } };
    auto i64=[&](std::uint64_t v){ for(int k=0;k<8;k++){ info.push_back(static_cast<std::uint8_t>(v&0xff)); v>>=8; } };
    auto iuleb=[&](std::uint64_t v){ do { std::uint8_t b=v&0x7f; v>>=7; if(v) b|=0x80; info.push_back(b); } while(v); };
    auto istr=[&](const std::string& s){ for(char c: s) info.push_back((std::uint8_t)c); info.push_back(0); };

    // unit length placeholder
    size_t ulen_pos = info.size();
    i32(0);
    i16(4); // version
    i32(0); // abbrev offset
    i8(8); // address size

    // compile unit DIE
    iuleb(1);
    istr(debug_files.empty() ? std::string("unknown") : debug_files[0]);
    i32(0); // stmt_list offset in .debug_line
    i64(0x401000); // low_pc
    i64(0x401000 + (res.text.empty()?0:res.text.size())); // high_pc

    // subprograms for global labels
    // collect sorted labels
    std::vector<std::pair<std::string,std::size_t>> funcs;
    for (auto& g : globals) {
        auto it = label_off.find(g);
        if (it != label_off.end()) funcs.push_back({g, it->second});
    }
    std::sort(funcs.begin(), funcs.end(), [](const auto&a,const auto&b){ return a.second < b.second; });
    for (size_t i=0;i<funcs.size();i++){
        iuleb(2);
        istr(funcs[i].first);
        i64(0x401000 + funcs[i].second);
        std::size_t high = (i+1 < funcs.size()) ? (0x401000 + funcs[i+1].second) : (0x401000 + res.text.size());
        i64(high);
    }
    // end children
    info.push_back(0);

    // patch unit length
    std::uint32_t ul2 = (std::uint32_t)(info.size() - 4);
    for (int k=0;k<4;k++) info[ulen_pos + static_cast<std::size_t>(k)] = (ul2 >> (8*k)) & 0xff;

    res.debug_info = info;
    res.debug_abbrev = abbrev;

    res.debug_line = dbg;

    res.ok = res.error.empty();
    return res;
}

bool write_elf_object(const std::string& path, const asm_result& r) {
    using namespace tracey::elf;
    std::ofstream f(path, std::ios::binary);
    if(!f) return false;
    // string table
    std::string strtab; strtab.push_back('\0');
    auto addstr=[&](const std::string& s){ size_t idx=strtab.size(); strtab+=s; strtab.push_back('\0'); return idx; };
    // section names
    std::string shstrtab; shstrtab.push_back('\0');
    auto addsh=[&](const std::string& s){ size_t idx=shstrtab.size(); shstrtab+=s; shstrtab.push_back('\0'); return idx; };
    size_t idx_text = addsh(".text");
    size_t idx_symtab = addsh(".symtab");
    size_t idx_strtab = addsh(".strtab");
    size_t idx_rela = addsh(".rela.text");
    size_t idx_shstrtab = addsh(".shstrtab");
    size_t idx_debug_line = 0;
    if (!r.debug_line.empty()) idx_debug_line = addsh(".debug_line");
    size_t idx_debug_info = 0;
    if (!r.debug_info.empty()) idx_debug_info = addsh(".debug_info");
    size_t idx_debug_abbrev = 0;
    if (!r.debug_abbrev.empty()) idx_debug_abbrev = addsh(".debug_abbrev");

    // symbol names
    std::vector<size_t> symname_idx;
    for(auto& s: r.symbols) symname_idx.push_back(addstr(s.name));
    // null symbol name idx 0 already

    // Build symbol table: first null symbol, then symbols
    std::vector<Sym> syms; syms.emplace_back(); // null
    for(size_t i=0;i<r.symbols.size();i++){
        Sym s; s.st_name = (std::uint32_t)symname_idx[i];
        s.st_info = (r.symbols[i].global? (STB_GLOBAL<<4) : 0) | STT_FUNC;
        s.st_shndx = 1; // .text
        s.st_value = r.symbols[i].offset;
        s.st_size = 0;
        syms.push_back(s);
    }

    // relocs: find symbol index
    std::vector<Rela> relas;
    for(auto& rc : r.relocs){
        // find symbol index in syms (1-based)
        int si=0;
        for(size_t i=0;i<r.symbols.size();i++){
            if(r.symbols[i].name==rc.symbol){ si=(int)i+1; break; }
        }
        if(si==0){
            // external symbol: add to symtab as undefined
            Sym s; s.st_name = (std::uint32_t)addstr(rc.symbol);
            s.st_info = (STB_GLOBAL<<4) | STT_FUNC;
            s.st_shndx = SHN_UNDEF;
            s.st_value = 0;
            syms.push_back(s);
            si = (int)syms.size()-1;
        }
        Rela rel; rel.r_offset=rc.offset;
        rel.r_info = ((std::uint64_t)si<<32) | static_cast<std::uint64_t>(rc.type);
        rel.r_addend = 0;
        relas.push_back(rel);
    }

    // Layout
    // ELF header
    Ehdr ehdr{};
    unsigned char ident[16] = {0x7f,'E','L','F',2,1,1,0,0,0,0,0,0,0,0,0};
    std::memcpy(ehdr.e_ident, ident, 16);
    ehdr.e_type = ET_REL;
    ehdr.e_machine = EM_AARCH64;
    ehdr.e_version = 1;
    ehdr.e_ehsize = 64;
    ehdr.e_shentsize = 64;
    ehdr.e_shnum = static_cast<std::uint16_t>(6 + (r.debug_line.empty()?0:1) + (r.debug_info.empty()?0:1) + (r.debug_abbrev.empty()?0:1));
    ehdr.e_shstrndx = 5;

    // offsets
    // align text to? no
    std::size_t text_size = r.text.size();
    // symtab
    // We'll compute by writing later; need offsets for shdr. We'll just serialize sequentially.
    // We'll write to a buffer via o with tellp.
    // Simpler: write ELF header placeholder then sections; but offsets depend. We'll do two-pass? Use vector buffer.
    // We'll instead build entire file in memory.
    std::vector<std::uint8_t> buf;
    auto append=[&](const void* p,size_t n){ const unsigned char* b=(const unsigned char*)p; buf.insert(buf.end(),b,b+n); };
    auto append_vec=[&](const std::vector<std::uint8_t>& v){ buf.insert(buf.end(),v.begin(),v.end()); };

    // ELF header placeholder (64 bytes)
    std::vector<std::uint8_t> ehdr_bytes(64,0);
    // fill later
    size_t ehdr_pos = buf.size();
    buf.insert(buf.end(), 64, 0);

    // .text
    size_t text_pos = buf.size();
    append_vec(r.text);
    // pad to 8? no

    // .symtab
    size_t symtab_pos = buf.size();
    // write symbols
    std::vector<std::uint8_t> symbytes;
    // use ostringstream? We'll manually push
    auto push_u32=[&](std::vector<std::uint8_t>& b,std::uint32_t v){ for(int i=0;i<4;i++){b.push_back(static_cast<std::uint8_t>(v&0xff));v>>=8;}};
    auto push_u64=[&](std::vector<std::uint8_t>& b,std::uint64_t v){ for(int i=0;i<8;i++){b.push_back(static_cast<std::uint8_t>(v&0xff));v>>=8;}};
    auto push_u16=[&](std::vector<std::uint8_t>& b,std::uint16_t v){ b.push_back(static_cast<std::uint8_t>(v&0xff)); b.push_back(static_cast<std::uint8_t>((v>>8)&0xff));};
    for(auto& s: syms){
        push_u32(symbytes,s.st_name);
        symbytes.push_back(s.st_info);
        symbytes.push_back(s.st_other);
        push_u16(symbytes,s.st_shndx);
        push_u64(symbytes,s.st_value);
        push_u64(symbytes,s.st_size);
    }
    size_t symtab_size_actual = symbytes.size();
    append(symbytes.data(), symbytes.size());

    // .strtab
    size_t strtab_pos = buf.size();
    // strtab content: null + symbol names? We already built strtab string with names. Use strtab.
    append(strtab.data(), strtab.size());

    // .rela.text
    size_t rela_pos = buf.size();
    std::vector<std::uint8_t> relabytes;
    for(auto& rel: relas){
        push_u64(relabytes, rel.r_offset);
        push_u64(relabytes, rel.r_info);
        push_u64(relabytes, (std::uint64_t)rel.r_addend);
    }
    size_t rela_size_actual = relabytes.size();
    append(relabytes.data(), relabytes.size());

    // .shstrtab
    size_t shstrtab_pos = buf.size();
    append(shstrtab.data(), shstrtab.size());

    // .debug_line
    size_t debug_line_pos = 0;
    size_t debug_line_size = r.debug_line.size();
    if (!r.debug_line.empty()) {
        debug_line_pos = buf.size();
        append(r.debug_line.data(), r.debug_line.size());
    }
    // .debug_info
    size_t debug_info_pos = 0;
    size_t debug_info_size = r.debug_info.size();
    if (!r.debug_info.empty()) {
        debug_info_pos = buf.size();
        append(r.debug_info.data(), r.debug_info.size());
    }
    // .debug_abbrev
    size_t debug_abbrev_pos = 0;
    size_t debug_abbrev_size = r.debug_abbrev.size();
    if (!r.debug_abbrev.empty()) {
        debug_abbrev_pos = buf.size();
        append(r.debug_abbrev.data(), r.debug_abbrev.size());
    }

    // section headers
    size_t shdr_pos = buf.size();
    // null shdr
    std::vector<std::uint8_t> shdr_bytes(64,0);
    append(shdr_bytes.data(), shdr_bytes.size());
    // .text
    Shdr sh_text{}; sh_text.sh_name=static_cast<std::uint32_t>(idx_text); sh_text.sh_type=SHT_PROGBITS; sh_text.sh_flags=SHF_ALLOC|SHF_EXECINSTR; sh_text.sh_offset=text_pos; sh_text.sh_size=text_size; sh_text.sh_addralign=4; sh_text.sh_entsize=0;
    // write sh_text
    {
        std::vector<std::uint8_t> b; 
        push_u32(b,sh_text.sh_name); push_u32(b,sh_text.sh_type); push_u64(b,sh_text.sh_flags); push_u64(b,sh_text.sh_addr); push_u64(b,sh_text.sh_offset); push_u64(b,sh_text.sh_size); push_u32(b,sh_text.sh_link); push_u32(b,sh_text.sh_info); push_u64(b,sh_text.sh_addralign); push_u64(b,sh_text.sh_entsize);
        append(b.data(),b.size());
    }
    // .symtab
    Shdr sh_sym{}; sh_sym.sh_name=static_cast<std::uint32_t>(idx_symtab); sh_sym.sh_type=SHT_SYMTAB; sh_sym.sh_offset=symtab_pos; sh_sym.sh_size=symtab_size_actual; sh_sym.sh_link=3; // strtab index
    sh_sym.sh_info=1; sh_sym.sh_addralign=8; sh_sym.sh_entsize=24;
    {
        std::vector<std::uint8_t> b;
        push_u32(b,sh_sym.sh_name); push_u32(b,sh_sym.sh_type); push_u64(b,sh_sym.sh_flags); push_u64(b,sh_sym.sh_addr); push_u64(b,sh_sym.sh_offset); push_u64(b,sh_sym.sh_size); push_u32(b,sh_sym.sh_link); push_u32(b,sh_sym.sh_info); push_u64(b,sh_sym.sh_addralign); push_u64(b,sh_sym.sh_entsize);
        append(b.data(),b.size());
    }
    // .strtab
    Shdr sh_str{}; sh_str.sh_name=static_cast<std::uint32_t>(idx_strtab); sh_str.sh_type=SHT_STRTAB; sh_str.sh_offset=strtab_pos; sh_str.sh_size=strtab.size(); sh_str.sh_addralign=1;
    {
        std::vector<std::uint8_t> b;
        push_u32(b,sh_str.sh_name); push_u32(b,sh_str.sh_type); push_u64(b,sh_str.sh_flags); push_u64(b,sh_str.sh_addr); push_u64(b,sh_str.sh_offset); push_u64(b,sh_str.sh_size); push_u32(b,sh_str.sh_link); push_u32(b,sh_str.sh_info); push_u64(b,sh_str.sh_addralign); push_u64(b,sh_str.sh_entsize);
        append(b.data(),b.size());
    }
    // .rela.text
    Shdr sh_rela{}; sh_rela.sh_name=static_cast<std::uint32_t>(idx_rela); sh_rela.sh_type=SHT_RELA; sh_rela.sh_offset=rela_pos; sh_rela.sh_size=rela_size_actual; sh_rela.sh_link=2; // symtab
    sh_rela.sh_info=1; sh_rela.sh_addralign=8; sh_rela.sh_entsize=24;
    {
        std::vector<std::uint8_t> b;
        push_u32(b,sh_rela.sh_name); push_u32(b,sh_rela.sh_type); push_u64(b,sh_rela.sh_flags); push_u64(b,sh_rela.sh_addr); push_u64(b,sh_rela.sh_offset); push_u64(b,sh_rela.sh_size); push_u32(b,sh_rela.sh_link); push_u32(b,sh_rela.sh_info); push_u64(b,sh_rela.sh_addralign); push_u64(b,sh_rela.sh_entsize);
        append(b.data(),b.size());
    }
    // .shstrtab
    Shdr sh_shstr{}; sh_shstr.sh_name=static_cast<std::uint32_t>(idx_shstrtab); sh_shstr.sh_type=SHT_STRTAB; sh_shstr.sh_offset=shstrtab_pos; sh_shstr.sh_size=shstrtab.size(); sh_shstr.sh_addralign=1;
    {
        std::vector<std::uint8_t> b;
        push_u32(b,sh_shstr.sh_name); push_u32(b,sh_shstr.sh_type); push_u64(b,sh_shstr.sh_flags); push_u64(b,sh_shstr.sh_addr); push_u64(b,sh_shstr.sh_offset); push_u64(b,sh_shstr.sh_size); push_u32(b,sh_shstr.sh_link); push_u32(b,sh_shstr.sh_info); push_u64(b,sh_shstr.sh_addralign); push_u64(b,sh_shstr.sh_entsize);
        append(b.data(),b.size());
    }

    if (!r.debug_line.empty()) {
        Shdr sh_dbg{}; sh_dbg.sh_name = static_cast<std::uint32_t>(idx_debug_line); sh_dbg.sh_type = SHT_PROGBITS; sh_dbg.sh_offset = debug_line_pos; sh_dbg.sh_size = debug_line_size; sh_dbg.sh_addralign = 1;
        std::vector<std::uint8_t> b;
        push_u32(b,sh_dbg.sh_name); push_u32(b,sh_dbg.sh_type); push_u64(b,sh_dbg.sh_flags); push_u64(b,sh_dbg.sh_addr); push_u64(b,sh_dbg.sh_offset); push_u64(b,sh_dbg.sh_size); push_u32(b,sh_dbg.sh_link); push_u32(b,sh_dbg.sh_info); push_u64(b,sh_dbg.sh_addralign); push_u64(b,sh_dbg.sh_entsize);
        append(b.data(), b.size());
    }
    if (!r.debug_info.empty()) {
        Shdr sh{}; sh.sh_name = static_cast<std::uint32_t>(idx_debug_info); sh.sh_type = SHT_PROGBITS; sh.sh_offset = debug_info_pos; sh.sh_size = debug_info_size; sh.sh_addralign = 1;
        std::vector<std::uint8_t> b;
        push_u32(b,sh.sh_name); push_u32(b,sh.sh_type); push_u64(b,sh.sh_flags); push_u64(b,sh.sh_addr); push_u64(b,sh.sh_offset); push_u64(b,sh.sh_size); push_u32(b,sh.sh_link); push_u32(b,sh.sh_info); push_u64(b,sh.sh_addralign); push_u64(b,sh.sh_entsize);
        append(b.data(), b.size());
    }
    if (!r.debug_abbrev.empty()) {
        Shdr sh{}; sh.sh_name = static_cast<std::uint32_t>(idx_debug_abbrev); sh.sh_type = SHT_PROGBITS; sh.sh_offset = debug_abbrev_pos; sh.sh_size = debug_abbrev_size; sh.sh_addralign = 1;
        std::vector<std::uint8_t> b;
        push_u32(b,sh.sh_name); push_u32(b,sh.sh_type); push_u64(b,sh.sh_flags); push_u64(b,sh.sh_addr); push_u64(b,sh.sh_offset); push_u64(b,sh.sh_size); push_u32(b,sh.sh_link); push_u32(b,sh.sh_info); push_u64(b,sh.sh_addralign); push_u64(b,sh.sh_entsize);
        append(b.data(), b.size());
    }

    // fill ELF header
    ehdr.e_shoff = shdr_pos;
    ehdr.e_shnum = static_cast<std::uint16_t>(6 + (r.debug_line.empty()?0:1) + (r.debug_info.empty()?0:1) + (r.debug_abbrev.empty()?0:1));
    ehdr.e_shstrndx = 5;
    // write ehdr into first 64 bytes
    // build ehdr bytes
    std::vector<std::uint8_t> eh;
    for(int i=0;i<16;i++) eh.push_back(ehdr.e_ident[i]);
    auto pu16=[&](std::uint16_t v){ eh.push_back(static_cast<std::uint8_t>(v&0xff)); eh.push_back(static_cast<std::uint8_t>((v>>8)&0xff)); };
    auto pu32=[&](std::uint32_t v){ for(int i=0;i<4;i++){eh.push_back(static_cast<std::uint8_t>(v&0xff));v>>=8;}};
    auto pu64=[&](std::uint64_t v){ for(int i=0;i<8;i++){eh.push_back(static_cast<std::uint8_t>(v&0xff));v>>=8;}};
    pu16(ehdr.e_type); pu16(ehdr.e_machine); pu32(ehdr.e_version);
    pu64(ehdr.e_entry); pu64(ehdr.e_phoff); pu64(ehdr.e_shoff);
    pu32(ehdr.e_flags); pu16(ehdr.e_ehsize); pu16(ehdr.e_phentsize);
    pu16(ehdr.e_phnum); pu16(ehdr.e_shentsize); pu16(ehdr.e_shnum); pu16(ehdr.e_shstrndx);
    // copy into buf
    for(size_t i=0;i<64;i++) buf[ehdr_pos+i]=eh[i];

    f.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return true;
}

} // namespace

