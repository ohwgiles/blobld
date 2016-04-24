#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <elf.h>

void print_usage() {
    fprintf(stderr, "Usage: $0 [-s symbol_prefix] [-o output_file] input_file\n");
}

int main(int argc, char** argv) {
    // populated by commandline arguments
    char* input_filename = NULL;
    char* output_filename = NULL;
    char* symbol_prefix = NULL;

    // parse arguments
    for(int i = 1; i < argc; ++i) {
        if(argv[i][0] == '-') {
            if(strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                output_filename = argv[++i];
            } else if(strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
                symbol_prefix = argv[++i];
            } else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                return print_usage(), 0;
            } else {
                fprintf(stderr, "Unknown option %s\n", argv[i]);
                return print_usage(), 1;
            }
        } else if(input_filename == NULL) {
            input_filename = argv[i];
        } else {
            fprintf(stderr, "Too many arguments\n");
            return print_usage(), 1;
        }
    }
    if(input_filename == NULL) {
        fprintf(stderr, "No input file\n");
        return print_usage(), 1;
    }

    // default arguments if not set
    if(output_filename == NULL) {
        output_filename = malloc(strlen(input_filename) + 3);
        sprintf(output_filename, "%s.o", input_filename);
    }
    if(symbol_prefix == NULL) {
        symbol_prefix = strdup(input_filename);
    }

    // sanitize symbol prefix so it can be used as a C identifier
    int sp_len = strlen(symbol_prefix);
    for(int i = 0; i < sp_len; ++i) {
        if(!(  symbol_prefix[i] >= 'a' && symbol_prefix[i] <= 'z'
            || symbol_prefix[i] >= 'A' && symbol_prefix[i] <= 'Z'
            || symbol_prefix[i] == '_'
            || i > 0 && symbol_prefix[i] >= '0' && symbol_prefix[i] <= '9'))
        {
            // replace invalid characters with _
            symbol_prefix[i] = '_';
        }
    }

    // open input file
    FILE* fp = fopen(input_filename, "rb");
    if(!fp) {
        return perror("Could not open input file"), 2;
    }

    // get length of file
    fseek(fp, 0L, SEEK_END);
    long data_len = ftell(fp);
    rewind(fp);

    // create symbol names
    long strtab_len = 1;
    char* sn_start = malloc(sp_len + strlen("_start") + 1);
    strtab_len += sprintf(sn_start, "%s_start", symbol_prefix);
    char* sn_end = malloc(sp_len + strlen("_end") + 1);
    strtab_len += sprintf(sn_end, "%s_end", symbol_prefix);
    strtab_len += 1;

    //long strtab_len = 1 + strlen(sn_start) + 1 + strlen(sn_end) + 1;

    // allocate output structure
    long payload_len = data_len + strtab_len;
    long payload_len_aligned = (payload_len + 7) & ~0x7;
    long output_size = sizeof(Elf64_Ehdr) + payload_len_aligned + 2 * sizeof(Elf64_Sym) + 4 * sizeof(Elf64_Shdr);
    char* output = calloc(output_size, 1);

    Elf64_Ehdr* ehdr = (Elf64_Ehdr*) output;
    char* data_out = output + sizeof(Elf64_Ehdr);
    char* strtab = data_out + data_len;
    Elf64_Sym* sym_start = (Elf64_Sym*) (output + sizeof(Elf64_Ehdr) + payload_len_aligned);
    Elf64_Sym* sym_end = (Elf64_Sym*) (output + sizeof(Elf64_Ehdr) + payload_len_aligned + sizeof(Elf64_Sym));
    Elf64_Shdr* sections = (Elf64_Shdr*) (output + sizeof(Elf64_Ehdr) + payload_len_aligned + 2 * sizeof(Elf64_Sym));

    // populate elf header
    memcpy(&ehdr->e_ident, ELFMAG, SELFMAG);
    ehdr->e_ident[EI_CLASS] = 2; //64bit
    ehdr->e_ident[EI_DATA] = 1; //little-endian
    ehdr->e_ident[EI_VERSION] = 1; //elf version
    ehdr->e_type = ET_REL; // relocatable
    ehdr->e_machine = 0x3e; // x86-64
    ehdr->e_version = 1; //elf version
    ehdr->e_shoff = ((char*)sections - output); // section header offset
    ehdr->e_ehsize = sizeof(Elf64_Ehdr);
    ehdr->e_shentsize = sizeof(Elf64_Shdr);
    ehdr->e_shnum = 4; // number of sections
    ehdr->e_shstrndx = 1; // where to find string table in sections

    // populate payload data
    fread(data_out, 1, payload_len, fp);
    fclose(fp);

    // populate string table
    memcpy(strtab + 1, sn_start, strlen(sn_start));
    memcpy(strtab + 1 + strlen(sn_start) + 1, sn_end, strlen(sn_end));

    // populate symbol table
    sym_start->st_name = 1; // index into strtab
    sym_start->st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    sym_start->st_shndx = 2; // section index: .rodata
    
    sym_end->st_name = strlen(sn_start) + 2; // index into strtab
    sym_end->st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE);
    sym_end->st_shndx = 2; // section index: .rodata
    sym_end->st_value = data_len;

    // populate section headers
    // sections[0] is NULL section
    sections[1].sh_type = SHT_STRTAB;
    sections[1].sh_offset = (strtab - output);
    sections[1].sh_size = strtab_len;
    sections[1].sh_addralign = 1;

    sections[2].sh_type = SHT_PROGBITS;
    sections[2].sh_flags = SHF_ALLOC;
    sections[2].sh_offset = (data_out - output);
    sections[2].sh_size = data_len;
    sections[2].sh_addralign = 1;

    sections[3].sh_type = SHT_SYMTAB;
    sections[3].sh_offset = ((char*)sym_start - output);
    sections[3].sh_size = 2 * sizeof(Elf64_Sym);
    sections[3].sh_link = 1; // section index of string table
    sections[3].sh_info = 0; // number of local symbols
    sections[3].sh_addralign = 8;
    sections[3].sh_entsize = sizeof(Elf64_Sym);

    fp = fopen(output_filename, "wb");
    if(fp == NULL) {
        return perror("Could not open output file for writing"), 3;
    }
    if(fwrite(output, 1, output_size, fp) != output_size) {
        return perror("Short write to output file"), 4;
    }
    fclose(fp);
    return 0;
}
