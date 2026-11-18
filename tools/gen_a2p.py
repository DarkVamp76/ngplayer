#!/usr/bin/env python3
# (Kept for reference: this is how src/core/a2m_replay.c was derived from AdPlug's a2m-v2.cpp. The generated file was committed and is maintained by hand from now on.)
"""Transliterates the player core of AdPlug's a2m-v2.cpp (a C-like C++ class) into a C file. Usage: gen_a2p.py a2m-v2.cpp a2m-v2.h out.c"""
import re,sys
cpp=open(sys.argv[1]).read(); hdr=open(sys.argv[2]).read()
EXCL=set('''factory Ca2mv2Player ~Ca2mv2Player update rewind getrefresh gettype load instruments_free instruments_allocate fmdata_fill_from_raw fmreg_infer_length_from_cells fmreg_table_allocate
disabled_fmregs_import arpvib_tables_free arpvib_tables_allocate patterns_free patterns_allocate a2t_play a2t_depack a2t_read_varheader instrument_import a2t_read_instruments a2t_read_fmregtable
a2t_read_arpvibtable a2t_read_disabled_fmregs a2t_read_order convert_v1234_effects a2_read_patterns a2t_read_patterns a2t_import a2m_read_varheader a2m_read_songdata a2m_read_patterns a2m_import a2_import
init_songdata init_irq done_irq'''.split())
MEMBER_VARS='''current_order current_pattern current_line tempo speed macro_speedup irq_mode IRQ_freq IRQ_freq_shift irq_initialized timer_fix pattern_break pattern_delay next_line playback_speed_shift
play_status overall_volume global_volume fade_out_volume vibtrem_speed_factor vibtrem_table_size vibtrem_table misc_register current_tremolo_depth current_vibrato_depth speed_update lockvol panlock lockVP
tremolo_depth vibrato_depth volume_scaling percussion_mode editor_mode songinfo instrinfo arpvib_count vibrato_table arpeggio_table eventsinfo ch ticks tick0 tickD tickXF ticklooper macro_ticklooper
type ffver len adsr_carrier songend chip'''.split()
lines=cpp.split('\n')
# find member function definitions
defre=re.compile(r'^(?:(?:inline|static)\s+)*(?:([A-Za-z_][\w\s\*:<>]*?)\s*)?Ca2mv2Player::(~?\w+)\(([^)]*)\)(.*)$')
funcs=[];i=0;segments=[]
other=[]
while i<len(lines):
    m=defre.match(lines[i]) if 'Ca2mv2Player::' in lines[i] and not lines[i].startswith(' ') else None
    if m:
        rest=m.group(4); k=i
        if '{' in rest: start=i+1
        else:
            k=i+1
            while lines[k].strip()!='{' and '{' not in lines[k]: k+=1
            start=k+1 if lines[k].strip()=='{' or lines[k].rstrip().endswith('{') else k+1
        j=start
        while lines[j] not in ('}','};'): j+=1
        if other: segments.append(('other','\n'.join(other))); other=[]
        segments.append(('func',m.group(2),(m.group(1) or '').strip(),m.group(3).strip(),'\n'.join(lines[start:j])))
        i=j+1; continue
    other.append(lines[i]); i+=1
if other: segments.append(('other','\n'.join(other)))
allfuncs=[s[1] for s in segments if s[0]=='func']
member_funcs=sorted(set(f for f in allfuncs if f not in EXCL),key=len,reverse=True)
def strip_calls(text,name):
    out=[];k=0
    pat=re.compile(r'\b'+name+r'\s*\(')
    while True:
        m=pat.search(text,k)
        if not m: out.append(text[k:]); break
        out.append(text[k:m.start()]); d=0;q=m.end()-1
        while True:
            if text[q]=='(': d+=1
            elif text[q]==')':
                d-=1
                if d==0: break
            q+=1
        k=q+1
        # swallow a following ';'
        if text[k:k+1]==';': k+=1
    return ''.join(out)
TYPES=r'(?:uint8_t|uint16_t|uint32_t|int8_t|int16_t|int32_t|int|bool|char|unsigned|float|double|size_t|tFM_INST_DATA|tINSTR_DATA|tINSTR_DATA_EXT|tFMREG_TABLE|tARPEGGIO_TABLE|tVIBRATO_TABLE|tADTRACK2_EVENT|tREGISTER_TABLE_DEF|t4OP_DATA|tPORTA_TABLE|tCH_MACRO_TABLE)'
def convert(name,ret,args,body):
    body=strip_calls(body,'AdPlug_LogWrite')
    # local names that shadow members: parameters and declared locals
    params=[a.strip().split()[-1].lstrip('*&') for a in args.split(',') if a.strip() and a.strip()!='void']
    locs=set(params)
    for m in re.finditer(r'\b'+TYPES+r'\s*\**\s*(\w+)',body): locs.add(m.group(1))
    vars_=[v for v in MEMBER_VARS if v not in locs]
    def sub_var(t):
        for v in vars_:
            t=re.sub(r'(?<![\w.>])'+v+r'\b(?!\s*\()' if False else r'(?<![\w.])(?<!->)'+v+r'\b','p->'+v,t)
        return t
    # protect strings
    body=sub_var(body)
    for f in member_funcs:
        body=re.sub(r'(?<![\w.>:])'+f+r'\s*\(\s*(\)?)',lambda m,f=f:(f+'(p)') if m.group(1) else (f+'(p, '),body)
    body=body.replace('opl->setchip(','opl_setchip(p, ').replace('opl->write(','opl_write(p, ')
    sig=ret+' '+name+'(A2P *p'+(', '+args if args and args!='void' else '')+')'
    return sig,body
out=[]
out.append('''/* Adlib Tracker 2 replayer for the A2M/A2T formats, versions 9-14. Transliterated to C from the player core of AdPlug's a2m-v2.cpp (Adlib Tracker II Player by Dmitry Smagin,
 * originally by Stanislav Baranec; Copyright (C) 1999-2008 Simon Peter et al.; LGPL-2.1-or-later), which is a port of the FreePascal sources of Adlib Tracker II.
 * Changes: C instead of C++, the OPL chip is reached through two callbacks, the loader lives in a2m_load.c. This file stays LGPL-2.1-or-later. */
''')
out.append('#include "a2m_replay.h"\n#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n#include <assert.h>\n\n')
# header macros
hs=hdr.index('#define keyoff_flag'); he=hdr.index('/* Helpers for importing A2T')
hm='\n'.join(l for l in hdr[hs:he].split('\n') if not l.startswith('#include'))
out.append(hm+'\n')
ts=hdr.index('typedef struct {\n    uint16_t freq;\n    uint8_t speed;\n} tPORTA_TABLE;'); te=hdr.index('} tCHDATA;')+len('} tCHDATA;')
chd=hdr[ts:te]
# remove the tADTRACK2_EVENT duplicate (the loader header has it) and the commented block
chd=re.sub(r'typedef struct \{\n    uint8_t note;\n    uint8_t instr_def;.*?\} tADTRACK2_EVENT;','',chd,flags=re.S)
chd=re.sub(r'/\*typedef struct \{.*?\*/','',chd,flags=re.S)
out.append(chd+'\n')
ps=hdr.index('typedef struct _4op_data'); pe=hdr.index('} t4OP_DATA;')+len('} t4OP_DATA;')
out.append(hdr[ps:pe]+'\n')
out.append('#define def_vibtrem_speed_factor 1\n#define def_vibtrem_table_size 32\n')
out.append('''
struct A2P {
    const A2mSong *song; A2pWrite write; void *user; int curchip;
    uint8_t current_order, current_pattern, current_line, tempo, speed;
    uint16_t macro_speedup; bool irq_mode; int16_t IRQ_freq; int IRQ_freq_shift; bool irq_initialized, timer_fix, pattern_break, pattern_delay; uint8_t next_line;
    int playback_speed_shift; tPLAY_STATUS play_status; uint8_t overall_volume, global_volume, fade_out_volume;
    uint8_t vibtrem_speed_factor, vibtrem_table_size, vibtrem_table[256], misc_register, current_tremolo_depth, current_vibrato_depth;
    bool speed_update, lockvol, panlock, lockVP; uint8_t tremolo_depth, vibrato_depth; bool volume_scaling, percussion_mode, editor_mode;
    tSONGINFO *songinfo; tINSTR_INFO *instrinfo; unsigned arpvib_count; tVIBRATO_TABLE **vibrato_table; tARPEGGIO_TABLE **arpeggio_table; tEVENTS_INFO *eventsinfo; tCHDATA *ch;
    int ticks, tick0, tickD, tickXF, ticklooper, macro_ticklooper; int type, ffver; unsigned len[21]; bool adsr_carrier[9]; bool songend; int chip;
};
static void opl_setchip(A2P *p, int c) { p->curchip = c; }
static void opl_write(A2P *p, int reg, int val) { p->write(p->user, p->curchip, reg, val); }

''')
# prototypes of all included functions
protos=[]
conv=[]
for s in segments:
    if s[0]=='func' and s[1] not in EXCL:
        sig,body=convert(s[1],s[2],s[3],s[4]); conv.append((sig,body)); protos.append('static '+sig+';')
out.append('\n'.join(protos)+'\n\n')
for s in segments:
    if s[0]=='other':
        t=s[1]
        # keep file-scope helpers (tables, macros, static functions) but drop obviously loader-related blocks
        if 'binistream' in t or 'CFileProvider' in t or '#include' in t and 'a2m-v2.h' in t: 
            t='\n'.join(l for l in t.split('\n') if not l.startswith('#include'))
        out.append(t+'\n')
    elif s[1] not in EXCL:
        sig,body=[c for c in conv if c[0].split('(')[0].split()[-1]==s[1]][0]
        out.append('static '+sig+'\n{\n'+body+'\n}\n')
open(sys.argv[3],'w').write(''.join(out))
print('segments',len(segments),'converted functions',len(conv))
