#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

#include "libdecoder.h"

/* 8086 can address up to 1MB of Memory
 * We allocate a 1MB buffer and load the instructions 
 * to the start of memory and then start executing from
 * there
 */
#define MEMORY_SIZE 1024*1024


#define OUTPUT(...) {               \
    printf(__VA_ARGS__);            \
}

#define DUMP_SEGMENT_REGS() {               \
    if (verbose && trace) {                 \
        for (int i=0; i<4; i++) {           \
            printf("; \t%s: 0x%04x (%d)\n", \
                segment_register_map[i],    \
                segment_registers[i],       \
                segment_registers[i]);      \
        }                                   \
    }                                       \
}

bool verbose = false;
bool trace = false;

uint8_t memory[MEMORY_SIZE] = {0};
uint8_t instruction_bitstream[MEMORY_SIZE] = {0};

uint16_t registers[MAX_REG] = { 0 };

uint16_t segment_registers[MAX_SEG_REG] = { 0 };


struct reg_definition_s {
    enum register_e reg;
    char *name;
    uint16_t mask;
    uint8_t shift;
};

#define REG_DEF_AX { REG_AX, "ax", 0xFFFF, 0 }
#define REG_DEF_AL { REG_AX, "al", 0x00FF, 0 }
#define REG_DEF_AH { REG_AX, "ah", 0xFF00, 8 }
#define REG_DEF_BX { REG_BX, "bx", 0xFFFF, 0 }
#define REG_DEF_BL { REG_BX, "bl", 0x00FF, 0 }
#define REG_DEF_BH { REG_BX, "bh", 0xFF00, 8 }
#define REG_DEF_CX { REG_CX, "cx", 0xFFFF, 0 }
#define REG_DEF_CL { REG_CX, "cl", 0x00FF, 0 }
#define REG_DEF_CH { REG_CX, "ch", 0xFF00, 8 }
#define REG_DEF_DX { REG_DX, "dx", 0xFFFF, 0 }
#define REG_DEF_DL { REG_DX, "dl", 0x00FF, 0 }
#define REG_DEF_DH { REG_DX, "dh", 0xFF00, 8 }
#define REG_DEF_SP { REG_SP, "sp", 0xFFFF, 0 }
#define REG_DEF_BP { REG_BP, "bp", 0xFFFF, 0 }
#define REG_DEF_SI { REG_SI, "si", 0xFFFF, 0 }
#define REG_DEF_DI { REG_DI, "di", 0xFFFF, 0 }

struct reg_definition_s reg_item_map[MAX_REG] = {
    REG_DEF_AX,
    REG_DEF_AL,
    REG_DEF_AH,
    REG_DEF_BX,
    REG_DEF_BL,
    REG_DEF_BH,
    REG_DEF_CX,
    REG_DEF_CL,
    REG_DEF_CH,
    REG_DEF_DX,
    REG_DEF_DL,
    REG_DEF_DH,
    REG_DEF_SP,
    REG_DEF_BP,
    REG_DEF_SI,
    REG_DEF_DI,
};


uint16_t read_register(struct reg_definition_s item) {
    uint16_t mask = item.mask;
    uint8_t shift = item.shift;
    return (registers[item.reg] & mask) >> shift;
}

uint16_t write_register(struct reg_definition_s dst_item, uint16_t value) {
    uint16_t mask = dst_item.mask;
    uint8_t shift = dst_item.shift;
    uint16_t data = registers[dst_item.reg] & ~mask;
    // printf("\t - mask 0x%x | shift %d | register[%s][0x%x] | data 0x%x\n", mask, shift, item.name, registers[item.reg], data);
    data |= (value<<shift) & mask;
    registers[dst_item.reg] = data;
    return registers[dst_item.reg];
}

bool parse_mov_inst(struct decoded_instruction_s *inst) {
    bool failed = false;
    uint16_t dst_val;
    uint16_t value;

    switch (inst->dst_type) {
        case TYPE_REGISTER:
            struct reg_definition_s dst_reg = reg_item_map[inst->dst_register];
            switch (inst->src_type) {
                case TYPE_REGISTER:
                    struct reg_definition_s src_reg = reg_item_map[inst->src_register];
                    dst_val = read_register(dst_reg);
                    value = read_register(src_reg);
                    write_register(dst_reg, value);
                    printf("%s %s, %s \t; %s:0x%04x -> 0x%04x\n", inst->op_name, 
                        get_register_name(inst->dst_register), 
                        get_register_name(inst->src_register), 
                        get_register_name(inst->dst_register), 
                        dst_val, value);
                    break;
                case TYPE_SEGMENT_REGISTER:
                    dst_val = read_register(dst_reg);
                    value = segment_registers[inst->src_seg_register];
                    write_register(dst_reg, value);
                    printf("%s %s, %s \t; %s:0x%04x -> 0x%04x\n", inst->op_name, 
                        get_register_name(inst->dst_register), 
                        get_segment_register_name(inst->src_seg_register), 
                        get_register_name(inst->dst_register), 
                        dst_val, value);
                    break;
                case TYPE_DATA:
                    dst_val = read_register(dst_reg);
                    write_register(dst_reg, inst->src_data);
                    printf("%s %s, %d \t; %s:0x%04x -> 0x%04x\n", inst->op_name, 
                        get_register_name(inst->dst_register), 
                        inst->src_data, 
                        get_register_name(inst->dst_register), 
                        dst_val, inst->src_data&0xFFFF);
                    break;
                default:
                    ERROR("Unhandled src_type[%d]\n", inst->src_type);
                    break;
            }
            break;
        case TYPE_SEGMENT_REGISTER:
            switch (inst->src_type) {
                case TYPE_REGISTER:
                    struct reg_definition_s src_reg = reg_item_map[inst->src_register];
                    segment_registers[inst->dst_seg_register] = read_register(src_reg);
                    break;
                case TYPE_SEGMENT_REGISTER:
                    segment_registers[inst->dst_seg_register] = segment_registers[inst->src_seg_register];
                    break;
                case TYPE_DATA:
                    segment_registers[inst->dst_seg_register] = inst->src_data;
                    break;
                default:
                    ERROR("Unhandled src_type[%d]\n", inst->src_type);
                    break;
            }
            break;
        default:
            ERROR("Unhandled dst_type[%d]\n", inst->dst_type);
            break;
    }


    return failed;
}

void usage(void) {
    fprintf(stderr, "8086 Instruction Simulator Usage:\n");
    fprintf(stderr, "-h         This help dialog.\n");
    fprintf(stderr, "-i <file>  Path to file to parse.\n");
    fprintf(stderr, "-v         Enable verbose output.\n");
    fprintf(stderr, "-t         Enable trace output. Add line numbers to verbose logs.\n");
}

int main (int argc, char *argv[]) {
    int opt;
    char *input_file = NULL;
    FILE *in_fp = NULL;
    size_t bytes_available;
    struct decoded_instruction_s instruction = {};

    while( (opt = getopt(argc, argv, "hi:vt")) != -1) {
        switch (opt) {
            case 'h':
                usage();
                exit(0);
                break;
            
            case 'i':
                input_file = strdup(optarg);
                break;

            case 'v':
                verbose = true;
                break;

            case 't':
                trace = true;
                break;

            default:
                fprintf(stderr, "ERROR Invalid command line option\n");
                usage();
                exit(1);
                break;
        }
    }

    LOG("==========================\n");
    LOG("8086 Instruction Simulator\n");
    LOG("==========================\n");

    if (!input_file) {
        fprintf(stderr, "ERROR Missing Input File\n");
        usage();
        exit(1);
    }

    LOG("Using Input Filename     [%s]\n", input_file);
    LOG("\n\n");

    in_fp = fopen(input_file, "r");
    if (!in_fp) {
        fprintf(stderr, "ERROR input_file fopen failed [%d][%s]\n", errno, strerror(errno));
        exit(1);
    }

    LOG("Opened Input file[%s] OK\n", input_file);

    bytes_available = fread((uint8_t *)&instruction_bitstream, 1, MEMORY_SIZE, in_fp);
    fclose(in_fp);

    LOG("Read [%zu] bytes from file\n\n", bytes_available);

    LOG("Starting Simulation:\n");
    LOG("--------------------\n");

    uint8_t *ptr = (uint8_t *)&instruction_bitstream;

    while (bytes_available > 0) {
        size_t consumed = decode_bitstream(ptr, bytes_available, verbose, &instruction);
        if (consumed == 0) {
            ERROR("Failed to decode instruction at byte[%zu]->[0x%02x]\n", ptr-instruction_bitstream, *ptr);
        }
        bytes_available -= consumed;
        ptr += consumed;

        // dump instruction internals
        dump_instruction(&instruction, verbose);

        // print the instruction
        // print_decoded_instruction(&instruction);
        LOG("; consumed [%zu]bytes | bytes_available [%zi]bytes\n", consumed, bytes_available);
        if (bytes_available<0) {
            ERROR("; ERROR: Decoder OVERRUN.\n; Decoder consumed past end of buffer\n");
        }

        switch (instruction.op) {
            case MOV_INST:
                LOG("Found %s MOV Instruction, parsing...\n", instruction.name);
                bool failed = parse_mov_inst(&instruction);
                if (failed) {
                    ERROR("Failed to parse MOV instruction\n");
                }
                break;
            default:
                ERROR("Unsupported instruction [%s]\n", instruction.name);
        }

    }
    
    LOG("\ndecode_bitstream consumed all %zu bytes from file\n", bytes_available);

    printf("\n");
    printf("Registers:\n");
    printf("----------\n");
    printf("\tax: 0x%04x (%d)\n", registers[REG_AX], registers[REG_AX]);
    printf("\tbx: 0x%04x (%d)\n", registers[REG_BX], registers[REG_BX]);
    printf("\tcx: 0x%04x (%d)\n", registers[REG_CX], registers[REG_CX]);
    printf("\tdx: 0x%04x (%d)\n", registers[REG_DX], registers[REG_DX]);
    printf("\tsp: 0x%04x (%d)\n", registers[REG_SP], registers[REG_SP]);
    printf("\tbp: 0x%04x (%d)\n", registers[REG_BP], registers[REG_BP]);
    printf("\tsi: 0x%04x (%d)\n", registers[REG_SI], registers[REG_SI]);
    printf("\tdi: 0x%04x (%d)\n", registers[REG_DI], registers[REG_DI]);
    printf("\n");
    printf("Segment Registers:\n");
    printf("------------------\n");
    for (enum segment_register_e seg_reg=SEG_REG_ES; seg_reg<MAX_SEG_REG; seg_reg++) {
        printf("\t%s: 0x%04x (%d)\n", get_segment_register_name(seg_reg), segment_registers[seg_reg], segment_registers[seg_reg]);
    }

    printf("\n\n");

    return 0;
}
