'''
Embeds binaries to C code with possibility to compress it. 
Outputs to stdout as a header file.
Define DATA_IMPLEMENTATION to treat it as an include file.

If one file has lz4 as encoder, will use the tinylz4.h library, which requires tiny_malloc

'''

import sys
import re
import os.path
import argparse
import subprocess

LINELEN = 120 # line len in the file
EXAMPLE= './test.data embed.py:listing test.txt:lz4 --prefix=_mydata'

# - TEST
#sys.argv+=EXAMPLE.split()

parser = argparse.ArgumentParser(description=__doc__, epilog="example: embed.py "+EXAMPLE)
parser.add_argument('files', nargs='+', metavar='file[:type]',help='binary files to embed')
parser.add_argument('--prefix', default="data_", help='prefix for file names in C')
parser.add_argument('--raw', help='disables encoding, only use/expose raw (ignores encoding)',action='store_true')

args=parser.parse_args()

# known extensions types ? 
PREFIX = args.prefix.upper()
# embed files

def printable(c) : 
    n=ord(c)
    return n>=32 and n<127 and c not in "\\\"0123456789*" # 0-9 to avoid octal clash, * to avoid /*

def gen_lines(data) :   
    "generator of quoted lines from big string"
    s=""
    for c in data : 
        s+= c if printable(c) else "\%o"%ord(c)
        if len(s)>=LINELEN : 
            yield s
            s=""
    yield s

print "/* \n  file autogenerated by %s, do no edit."%os.path.basename(sys.argv[0])
print "  define %sIMPLEMENTATION to include the real data, once.\n*/\n"%args.prefix.upper()
all_files=[] 
for file in args.files : 
    f,encoding = file.split(':',1) if ':' in file else (file,'raw')
    # only keep basename, quote special chars
    quoted = re.sub(r'(^[^a-zA-Z])|[^0-9a-zA-Z_]','_',os.path.basename(f))
    # get original file size
    decoded_size = os.path.getsize(f)

    # encode it
    if encoding=='raw' or args.raw :
        s = open(f).read()
    elif encoding=='lz4':
        s = subprocess.check_output(['lz4','-9','--no-frame-crc','--no-sparse',f])
        assert s[:4]=='\x04"M\x18'
        s = s[11:]
    else:
        print "unknown encoding"
        sys.exit(1)

    all_files.append(dict(file=f,quoted=quoted,encoding=encoding,data=s,decoded=decoded_size))


if args.raw : 
    for f in all_files :
        print "extern const void *%s%s;"%(args.prefix, f['quoted'])
    print "\n#ifdef %sIMPLEMENTATION"%PREFIX + "  // "+"-"*80+"\n"

    for f in all_files : 
        print "const void *%s%s = " % (args.prefix,f['quoted'])  
        print "\n".join(" \"%s\""%line for line in gen_lines(f['data']))+";\n"

    print "\n#endif // %sIMPLEMENTATION"%PREFIX

else : 
    print "#ifndef _%sDEFINITION"%PREFIX
    print "#define _%sDEFINITION"%PREFIX
    print "enum %senum {"%args.prefix
    for f in all_files :
        print "   %s%s,"%(args.prefix,f['quoted'])
    print "};"
    print "void *load_resource(int id);"
    print "#endif //_%sDEFINITION"%PREFIX

    print "\n#ifdef %sIMPLEMENTATION"%PREFIX + "  // "+"-"*80+"\n"


    for f in all_files :
        print "static const char _%s%s[];"%(args.prefix, f['quoted'])
    print '''// please implement those somewhere in your code
#include "lib/resources/tinylz4.h" 
#include "lib/resources/tinymalloc.h" 
'''

    ENCO = {'raw':0,'lz4':1} # 4 values

    print """static const struct { 
    const char *data;
    uint32_t data_sz;
    unsigned format:2;
    unsigned decoded_sz:30; 
} table[] = {"""
    for f in all_files :
        print "    {{_{prefix}{quoted},{enc},{fmt},{dec} }},".format(
            prefix=args.prefix,
            quoted=f['quoted'],
            enc=len(f['data']),
            fmt=ENCO[f['encoding']],
            dec=f['decoded']
            )
    print "};\n"

    print """void *load_resource(int id) {
    if (table[id].format==0) {
        return (void*) table[id].data;
    } else {
        void *data = t_malloc(table[id].decoded_sz );  // reserve memory
        lz4_block_decompress((uint8_t *)table[id].data, table[id].data_sz, data);
        return data;
    }
}
    """

    for f in all_files : 
        print "static const char _%s%s[] = " % (args.prefix,f['quoted'])  
        print "\n".join(" \"%s\""%line for line in gen_lines(f['data']))+";\n"

    print "\n#endif // %s_IMPLEMENTATION"%PREFIX