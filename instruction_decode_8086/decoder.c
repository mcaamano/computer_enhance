#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

#define LOG(...) {                  \
        if (verbose) {              \
            printf(__VA_ARGS__);    \
        }                           \
    }

#define ERROR(...) {                    \
        fprintf(stderr, __VA_ARGS__);   \
        exit(1);                        \
    }

#define OUTPUT(...) {               \
    printf(__VA_ARGS__);            \
    fprintf(out_fp, __VA_ARGS__);   \
}

#define BUFFER_SIZE 4096

// mov 100010DW => 10 0010
#define MOV_REGMEM_REG_INSTRUCTION          0x22
// mov 1100011W => 110 0011
#define MOV_IMMEDIATE_TO_REGMEM_INSTRUCTION 0x63
// mov 1011WREG => 1011
#define MOV_IMMEDIATE_TO_REG_INSTRUCTION    0x0b
// mov 1010000W => 101 0000
#define MOV_MEMORY_TO_ACCUMULATOR           0x50
// mov 1010001W => 101 0001
#define MOV_ACCUMULATOR_TO_MEMORY           0x51

// add 000000DW => 00 0000
#define ADD_REGMEM_WITH_REG                 0x00
// add 100000SW => 10 0000
#define ADD_IMMEDIATE_TO_REGMEM             0x20
// add 0000010W => 000 0010
#define ADD_IMMEDIATE_TO_ACCUMULATOR        0x02

// sub 001010DW => 00 1010
#define SUB_REGMEM_WITH_REG                 0x0a
// sub 100000SW => 10 0000
#define SUB_IMMEDIATE_TO_REGMEM             ADD_IMMEDIATE_TO_REGMEM
// sub 0010110W =>  001 0110
#define SUB_IMMEDIATE_FROM_ACCUMULATOR      0x16

// cmp 001110DW => 00 1110
#define CMP_REGMEM_WITH_REG                 0x0e
// cmp 100000SW => 10 0000
#define CMP_IMMEDIATE_TO_REGMEM             ADD_IMMEDIATE_TO_REGMEM
// cmp 0011110W => 001 1110
#define CMP_IMMEDIATE_WITH_ACCUMULATOR      0x1e

bool verbose = false;
FILE *in_fp = NULL;
FILE *out_fp = NULL;

char *register_map[8][2] = {
    { "al", "ax" }, 
    { "cl", "cx" }, 
    { "dl", "dx" }, 
    { "bl", "bx" }, 
    { "ah", "sp" }, 
    { "ch", "bp" }, 
    { "dh", "si" }, 
    { "bh", "di" }
};

char *mov_source_effective_address[] = {
    "bx + si",
    "bx + di",
    "bp + si",
    "bp + di",
    "si",
    "di",
    "bp",
    "bx"
};

char *byte_count_str[] = {
    "",
    "1st",
    "2nd",
    "3rd",
    "4th",
    "5th",
    "6th"
};

void usage(void) {
    fprintf(stderr, "8086 Instruction Decoder Usage:\n");
    fprintf(stderr, "-h         This help dialog.\n");
    fprintf(stderr, "-i <file>  Path to file to parse.\n");
    fprintf(stderr, "-o <file>  Path to file to generate output.\n");
    fprintf(stderr, "-v         Enable verbose output.\n");
}

/*
 * Extract Displacement from bitstream
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int extract_displacement(uint8_t *ptr, int bit_w, int16_t *displacement, int prev_consumed_bytes) {
    int consumed_bytes = 0;
    uint8_t displacement_low = 0;
    uint8_t displacement_high = 0;

    // consume displacement_low byte
    ptr++;
    consumed_bytes++;
    prev_consumed_bytes++;
    displacement_low = *ptr;
    

    if (bit_w == 0x0) {
        // 8 bit displacement check if we need to do 8bit to 16bit signed extension
        if ((*ptr>>7) == 0x1 ) {
            // MSB bit is set, then do signed extension
            *displacement = (0xFF<<8) | *ptr;
            LOG("; [0x%02x] %s Byte - Displacement[0x%x] signed extended to [%d]\n", *ptr, byte_count_str[prev_consumed_bytes], *ptr, *displacement);
        } else {
            *displacement = *ptr;
            LOG("; [0x%02x] %s Byte - Displacement[0x%x][%d]\n", *ptr,  byte_count_str[prev_consumed_bytes], *displacement, *displacement);
        }
    } else {
        // 16bit displacement
        LOG("; [0x%02x] %s Byte - Displacement Low[0x%02x] \n", *ptr, byte_count_str[prev_consumed_bytes], displacement_low);
        // consume displacement_high byte
        ptr++;
        consumed_bytes++;
        prev_consumed_bytes++;
        displacement_high = *ptr;
        LOG("; [0x%02x] %s Byte - Displacement High[0x%02x]\n", *ptr, byte_count_str[prev_consumed_bytes], displacement_high);
        *displacement = (displacement_high << 8) | displacement_low;
        LOG(";        Displacement [0x%04x][%d]\n", *displacement, *displacement);
    }

    return consumed_bytes;
}

/*
 * Extract Displacement from bitstream
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int extract_data(uint8_t *ptr, int bit_w, int16_t *data, int prev_consumed_bytes) {
    int consumed_bytes = 0;
    uint8_t data_low = 0;
    uint8_t data_high = 0;

    // consume data_low byte
    ptr++;
    consumed_bytes++;
    prev_consumed_bytes++;
    data_low = *ptr;

    if (bit_w == 0x0) {
        // 8 bit data check if we need to do 8bit to 16bit signed extension
        if ((*ptr>>7) == 0x1 ) {
            // MSB bit is set, then do signed extension
            *data = (0xFF<<8) | *ptr;
            LOG("; [0x%02x] %s Byte - Data[0x%x] signed extended to [%d]\n", *ptr, byte_count_str[prev_consumed_bytes], *ptr, *data);
        } else {
            *data = *ptr;
            LOG("; [0x%02x] %s Byte - Data[0x%x][%d]\n", *ptr,  byte_count_str[prev_consumed_bytes], *data, *data);
        }
    } else {
        // 16 bit data
        LOG("; [0x%02x] %s Byte - data_low[0x%02x] \n", *ptr, byte_count_str[prev_consumed_bytes], data_low);
        // consume data_high byte
        ptr++;
        consumed_bytes++;
        prev_consumed_bytes++;
        data_high = *ptr;
        LOG("; [0x%02x] %s Byte - data_high[0x%02x]\n", *ptr, byte_count_str[prev_consumed_bytes], data_high);
        *data = (data_high << 8) | data_low;
        LOG(";        Data [0x%04x][%d]\n", *data, *data);
    }

    return consumed_bytes;
}


/*
 * Extract Displacement from bitstream
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int explicit_extract_data(uint8_t *ptr, int bit_s, int bit_w, int16_t *data, int prev_consumed_bytes) {
    int consumed_bytes = 0;
    uint8_t op = (bit_s<<1) | bit_w;
    uint8_t data_low = 0;
    uint8_t data_high = 0;

    // consume data_low byte
    ptr++;
    consumed_bytes++;
    prev_consumed_bytes++;
    data_low = *ptr;

    // Explicit Extact of Data according to bits S and W
    // s w
    // 0 0  8 bit no sign extension
    // 0 1  16 bit data
    // 1 0  sign extend to 16 bit
    // 1 1  sign extend to 16 bit
    switch (op) {
        case 0x0:
            // 8 bit no sign extension
            *data = *ptr;
            LOG("; [0x%02x] %s Byte - Data[0x%x][%d]\n", *ptr,  byte_count_str[prev_consumed_bytes], *data, *data);
            break;
        case 0x1:
            // 16 bit data
            LOG("; [0x%02x] %s Byte - data_low[0x%02x] \n", *ptr, byte_count_str[prev_consumed_bytes], data_low);
            // consume data_high byte
            ptr++;
            consumed_bytes++;
            prev_consumed_bytes++;
            data_high = *ptr;
            LOG("; [0x%02x] %s Byte - data_high[0x%02x]\n", *ptr, byte_count_str[prev_consumed_bytes], data_high);
            *data = (data_high << 8) | data_low;
            LOG(";        Data [0x%04x][%d]\n", *data, *data);
            break;
        case 0x2:
        case 0x3:
            // sign extend to 16 bit
            // 8 bit data check if we need to do 8bit to 16bit signed extension
            if ((*ptr>>7) == 0x1 ) {
                // MSB bit is set, then do signed extension
                *data = (0xFF<<8) | *ptr;
                LOG("; [0x%02x] %s Byte - Data[0x%x] signed extended to [%d]\n", *ptr, byte_count_str[prev_consumed_bytes], *ptr, *data);
            } else {
                *data = *ptr;
                LOG("; [0x%02x] %s Byte - Data[0x%x][%d]\n", *ptr,  byte_count_str[prev_consumed_bytes], *data, *data);
            }
            break;
    }
    return consumed_bytes;
}

/*
 * Process a Accumulator MOV
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int process_mov_accumulator_inst(uint8_t *ptr, int op_type) {
    uint8_t address_low = 0;
    uint8_t address_high = 0;
    int16_t address = 0;
    int consumed_bytes = 1;
    uint8_t bit_w = (*ptr & 0x1);

    LOG("; [0x%02x] %s Byte - Found ACCUMULATOR MOV bitstream | W[%d]\n", *ptr, byte_count_str[consumed_bytes], bit_w);

    // consume address_low byte
    ptr++;
    consumed_bytes++;
    address_low = *ptr;
    LOG("; [0x%02x] %s Byte - address_low[0x%02x]\n", *ptr, byte_count_str[consumed_bytes], address_low);
    if (bit_w == 1) {
        // consume address_high byte
        ptr++;
        consumed_bytes++;
        address_high = *ptr;
        LOG("; [0x%02x] %s Byte - address_high[0x%02x]\n", *ptr, byte_count_str[consumed_bytes], address_high);
    }
    address = (address_high << 8) | address_low;
    LOG("; Address [0x%04x][%d]\n", address, address);

    if (op_type == MOV_MEMORY_TO_ACCUMULATOR) {
        printf("mov ax, [ %d ]\n", address);
        fprintf(out_fp, "mov ax, [ %d ]\n", address);
    } else {
        // MOV_ACCUMULATOR_TO_MEMORY
        printf("mov [ %d ], ax\n", address);
        fprintf(out_fp, "mov [ %d ], ax\n", address);
    }

    return consumed_bytes;
}

/*
 * Process a MOV immediate to register
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int process_mov_immediate_to_register_memory(uint8_t *ptr) {
    int16_t data = 0;
    int16_t displacement = 0;
    int consumed = 0;
    int consumed_bytes = 1;
    uint8_t bit_w = *ptr & 0x1;
    char *data_type = "byte";
    char *destination = NULL;

    LOG("; [0x%02x] %s Byte - Found IMMEDIATE TO REG/MEM MOV bitstream | W[%d]\n", *ptr, byte_count_str[consumed_bytes], bit_w);

    // consume 2nd byte
    ptr++;
    consumed_bytes++;
    uint8_t mod = (*ptr >> 6) & 0x3;
    uint8_t r_m = (*ptr >> 0) & 0x7;
    
    if (mod == 0x3) {
        destination = register_map[r_m][bit_w];
    } else {
        destination = mov_source_effective_address[r_m];
    }
    LOG("; [0x%02x] %s Byte - MOD[0x%x] R/M[0x%x] destination[%s]\n", *ptr, byte_count_str[consumed_bytes], mod, r_m, destination);

    switch (mod) {
        case 0x0:
            // Memory Mode, No Displacement
            // * except when R/M is 110 then 16bit displacement follows
            if (r_m == 0x6) {
                // special case R/M is 110
                // 16 bit displacement follows
                consumed = extract_displacement(ptr, bit_w, &displacement, consumed_bytes);
                consumed_bytes += consumed;
                ptr += consumed;
            }

            // extract data
            consumed = extract_data(ptr, bit_w, &data, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            if (displacement == 0) {
                printf("mov [%s], %s %d\n", destination, data_type, data);
                fprintf(out_fp, "mov [%s], %s %d\n", destination, data_type, data);
            } else {
                printf("mov [%s + %d], %s %d\n", destination, displacement, data_type, data);
                fprintf(out_fp, "mov [%s + %d], %s %d\n", destination, displacement, data_type, data);
            }
            break;

        case 0x1:
            // Memory Mode, 8bit displacement follows

            // extract displacement
            consumed = extract_displacement(ptr, bit_w, &displacement, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            // extract data
            consumed = extract_data(ptr, bit_w, &data, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            if (displacement == 0) {
                printf("mov [%s], %s %d\n", destination, data_type, data);
                fprintf(out_fp, "mov [%s], %s %d\n", destination, data_type, data);
            } else {
                printf("mov [%s + %d], %s %d\n", destination, displacement, data_type, data);
                fprintf(out_fp, "mov [%s + %d], %s %d\n", destination, displacement, data_type, data);
            }
            break;

        case 0x2:
            // Memory Mode, 16bit displacement follows

            // extract displacement
            consumed = extract_displacement(ptr, bit_w, &displacement, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            // extract data
            consumed = extract_data(ptr, bit_w, &data, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            if (bit_w == 0x1) {
                // 16 bit data
                data_type = "word";
            }

            if (displacement == 0) {
                printf("mov [%s], %s %d\n", destination, data_type, data);
                fprintf(out_fp, "mov [%s], %s %d\n", destination, data_type, data);
            } else {
                printf("mov [%s + %d], %s %d\n", destination, displacement, data_type, data);
                fprintf(out_fp, "mov [%s + %d], %s %d\n", destination, displacement, data_type, data);
            }
            break;

        case 0x3:
            // Register Mode, No Displacement

            // extract data
            consumed = extract_data(ptr, bit_w, &data, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            if (bit_w == 0x1) {
                // 16 bit data
                data_type = "word";
            }

            if (bit_w == 0x1) {
                // 16 bit data
                data_type = "word";
            }

            printf("mov [%s], %s %d\n", destination, data_type, data);
            fprintf(out_fp, "mov [%s], %s %d\n", destination, data_type, data);
            break;
    }

    return consumed_bytes;
}

/*
 * Process a MOV immediate to register
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int process_mov_immediate_to_register(uint8_t *ptr) {
    int16_t data = 0;
    int consumed = 0;
    int consumed_bytes = 1;
    uint8_t bit_w = (*ptr>>3) & 0x1;
    uint8_t reg = (*ptr & 0x7);
    char *reg_str = register_map[reg][bit_w];

    LOG("; [0x%02x] %s Byte - Found IMMEDIATE TO REG MOV bitstream | W[%d] REG[0x%x][%s]\n", *ptr, byte_count_str[consumed_bytes], bit_w, reg, reg_str);

    // extract data
    consumed = extract_data(ptr, bit_w, &data, consumed_bytes);
    consumed_bytes += consumed;
    ptr += consumed;

    printf("mov %s,%d\n", reg_str, data);
    fprintf(out_fp, "mov %s,%d\n", reg_str, data);
    return consumed_bytes;
}

/*
 * Process a MOV register/memory to/from register
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int process_regmem_tpo_reg_op_inst(uint8_t *ptr, int op_type) {
    uint8_t bit_d = (*ptr & 0x2)>>1;
    uint8_t bit_w = *ptr & 0x1;
    char *destination = NULL;
    char *source = NULL;
    int consumed = 0;
    int consumed_bytes = 1;
    int16_t displacement = 0;
    char *op_type_str = NULL;
    char *op = NULL;

    switch (op_type) {
        case MOV_REGMEM_REG_INSTRUCTION:
            op_type_str = "MOV_REGMEM_REG_INSTRUCTION";
            op = "mov";
            break;
        case ADD_REGMEM_WITH_REG:
            op_type_str = "ADD_REGMEM_WITH_REG";
            op = "add";
            break;
        case SUB_REGMEM_WITH_REG:
            op_type_str = "SUB_REGMEM_WITH_REG";
            op = "sub";
            break;
        case CMP_REGMEM_WITH_REG:
            op_type_str = "CMP_REGMEM_WITH_REG";
            op = "cmp";
            break;
        default:
            ERROR("[%s:%d] ERROR: Bad OP[0x%x]\n", __FUNCTION__, __LINE__, op_type);
    }

    LOG("; [0x%02x] %s Byte - Found %s bitstream | D[%d] W[%d]\n", *ptr, byte_count_str[consumed_bytes], op_type_str, bit_d, bit_w);

    // consume 2nd byte
    ptr++;
    uint8_t mod = (*ptr >> 6) & 0x3;
    uint8_t reg = (*ptr >> 3) & 0x7;
    uint8_t r_m = (*ptr >> 0) & 0x7;
    consumed_bytes++;
    LOG("; [0x%02x] 2nd Byte - MOD[0x%x] REG[0x%x] R/M[0x%x]\n", *ptr, mod, reg, r_m);

    switch (mod) {
        case 0x0:
            // Memory Mode, No Displacement
            // * except when R/M is 110 then 16bit displacement follows
            if (r_m == 0x6) {
                // special case R/M is 110
                // 16 bit displacement follows
                // extract displacement
                consumed = extract_displacement(ptr, bit_w, &displacement, consumed_bytes);
                consumed_bytes += consumed;
                ptr += consumed;
                // destination specificed in REG field
                destination = register_map[reg][bit_w];
                // source is the direct address
                printf("%s %s, [%d]\n", op, destination, displacement);
                fprintf(out_fp, "%s %s, [%d]\n", op, destination, displacement);
            } else {
                if (bit_d == 0x1) {
                    // destination specificed in REG field
                    destination = register_map[reg][bit_w];
                    // source specified in effective address table
                    source = mov_source_effective_address[r_m];
                    printf("%s %s, [%s]\n", op, destination, source);
                    fprintf(out_fp, "%s %s, [%s]\n", op, destination, source);
                } else {
                    // destination specificed in R/M field
                    destination = mov_source_effective_address[r_m];
                    // source specified in REG field
                    source = register_map[reg][bit_w];
                    printf("%s [%s], %s\n", op, destination, source);
                    fprintf(out_fp, "%s [%s], %s\n", op, destination, source);
                }
            }
            break;
        case 0x1:
            // Memory Mode, 8bit displacement follows
            // extract displacement
            // force bit_w to extract displacement as it is 8bits
            consumed = extract_displacement(ptr, 0, &displacement, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;
            if (bit_d == 0x1) {
                // destination specificed in REG field
                destination = register_map[reg][bit_w];
                // source specified in effective address table
                source = mov_source_effective_address[r_m];
                if (displacement==0) {
                    // don't output " + 0" in the effective address calculation
                    printf("%s %s, [%s]\n", op, destination, source);
                    fprintf(out_fp, "%s %s, [%s]\n", op, destination, source);
                } else {
                    printf("%s %s, [%s + %d]\n", op, destination, source, displacement);
                    fprintf(out_fp, "%s %s, [%s + %d]\n", op, destination, source, displacement);
                }
            } else {
                // destination specificed in R/M field
                destination = mov_source_effective_address[r_m];
                // source specified in REG field
                source = register_map[reg][bit_w];
                if (displacement==0) {
                    // don't output " + 0" in the effective address calculation
                    printf("%s [%s], %s\n", op, destination, source);
                    fprintf(out_fp, "%s [%s], %s\n", op, destination, source);
                } else {
                    printf("%s [%s + %d], %s\n", op, destination, displacement, source);
                    fprintf(out_fp, "%s [%s + %d], %s\n", op, destination,  displacement, source);
                }
            }
            break;
        case 0x2:
            // Memory Mode, 16bit displacement follows
            // force extract 16bit displacement
            consumed = extract_displacement(ptr, 1, &displacement, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            if (bit_d == 0x1) {
                // destination specificed in REG field
                destination = register_map[reg][bit_w];
                // source specified in effective address table
                source = mov_source_effective_address[r_m];
                if (displacement==0) {
                    // don't output " + 0" in the effective address calculation
                    printf("%s %s, [%s]\n", op, destination, source);
                    fprintf(out_fp, "%s %s, [%s]\n", op, destination, source);
                } else {
                    printf("%s %s, [%s + %d]\n", op, destination, source, displacement);
                    fprintf(out_fp, "%s %s, [%s + %d]\n", op, destination, source, displacement);
                }
            } else {
                // destination specificed in R/M field
                destination = mov_source_effective_address[r_m];
                // source specified in REG field
                source = register_map[reg][bit_w];
                if (displacement==0) {
                    // don't output " + 0" in the effective address calculation
                    printf("%s [%s], %s\n", op, destination, source);
                    fprintf(out_fp, "%s [%s], %s\n", op, destination, source);
                } else {
                    printf("%s [%s + %d], %s\n", op, destination, displacement, source);
                    fprintf(out_fp, "%s [%s + %d], %s\n", op, destination,  displacement, source);
                }
            }
            break;
        case 0x3:
            // Register Mode, No Displacement
            // get destination/source
            if (bit_d == 0x1) {
                // destination specificed in REG field
                destination = register_map[reg][bit_w];
                // source specificed in R/M field
                source = register_map[r_m][bit_w];
            } else {
                // destination specificed in R/M field
                destination = register_map[r_m][bit_w];
                // source specificed in REG field
                source = register_map[reg][bit_w];
            }
            printf("%s %s,%s\n", op, destination, source);
            fprintf(out_fp, "%s %s,%s\n", op, destination, source);
            break;
    }
    return consumed_bytes;
}


/*
 * Process an immediate to register/mem op
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int process_immediate_to_regmem_op_inst(uint8_t *ptr, int op_type) {
    int16_t data = 0;
    int16_t displacement = 0;
    int consumed = 0;
    int consumed_bytes = 1;
    uint8_t bit_s = (*ptr>>1) & 0x1;
    uint8_t bit_w = *ptr & 0x1;
    char *destination = NULL;
    char *op = NULL;
    char *data_type = "byte";

    // OP codes for 
    // ADD_IMMEDIATE_TO_REGMEM
    // SUB_IMMEDIATE_TO_REGMEM
    // CMP_IMMEDIATE_TO_REGMEM
    // are re the same, must differentiate using bits 5-3 from 2nd byte

    LOG("; [0x%02x] %s Byte - Found ADD_IMMEDIATE_TO_REGMEM / SUB_IMMEDIATE_TO_REGMEM / CMP_IMMEDIATE_TO_REGMEM bitstream | S[%d] W[%d]\n", *ptr, byte_count_str[consumed_bytes], bit_s, bit_w);

    // consume 2nd byte
    ptr++;
    consumed_bytes++;
    uint8_t mod = (*ptr >> 6) & 0x3;
    uint8_t op_sel = (*ptr >> 3) & 0x7;
    uint8_t r_m = (*ptr >> 0) & 0x7;
    LOG("; data[0x%x] MOD[0x%x] OP_SEL[0x%x] R/M[0x%x]\n", *ptr, mod, op_sel, r_m);

    if (op_sel == 0x0) {
        // ADD_IMMEDIATE_TO_REGMEM
        op = "add";
    } else if (op_sel == 0x5) {
        // SUB_IMMEDIATE_TO_REGMEM
        op = "sub";
    } else if (op_sel == 0x7) {
        // CMP_IMMEDIATE_TO_REGMEM
        op = "cmp";
    } else {
        ERROR("; [%s:%d] ERROR: Bad OP Sel encoding found[0x%x]\n", __FUNCTION__, __LINE__, op_sel);
    }
    
    if (mod == 0x3) {
        destination = register_map[r_m][bit_w];
    } else {
        destination = mov_source_effective_address[r_m];
    }
    LOG("; [0x%02x] %s Byte - MOD[0x%x] R/M[0x%x] destination[%s]\n", *ptr, byte_count_str[consumed_bytes], mod, r_m, destination);

    if (bit_w == 0x1) {
        data_type = "word";
    }

    // s w
    // 0 0  8bit no sign extension
    // 0 1  16bit data
    // 1 0  sign extend 8 bit
    // 1 1  sign extend to 16 bit

    switch (mod) {
        case 0x0:
            // Memory Mode, No Displacement
            // * except when R/M is 110 then 16bit displacement follows
            if (r_m == 0x6) {
                // special case R/M is 110
                // 16 bit displacement follows
                consumed = extract_displacement(ptr, bit_w, &displacement, consumed_bytes);
                consumed_bytes += consumed;
                ptr += consumed;
            }

            // extract data
            consumed = explicit_extract_data(ptr, bit_s, bit_w, &data, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            if (r_m == 0x6) {
                // Direct Address
                OUTPUT("%s %s [%d], %d\n", op, data_type, displacement, data);
            } else {
                OUTPUT("%s %s [%s], %d\n", op, data_type, destination, data);
            }
            break;

        case 0x1:
            // Memory Mode, 8bit displacement follows

            // extract displacement
            consumed = extract_displacement(ptr, bit_w, &displacement, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            // extract data
            consumed = explicit_extract_data(ptr, bit_s, bit_w, &data, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            if (displacement == 0) {
                OUTPUT("%s %s [%s], %d\n", op, data_type, destination, data);
            } else {
                OUTPUT("%s %s [%s + %d], %d\n", op, data_type, destination, displacement, data);
            }
            break;

        case 0x2:
            // Memory Mode, 16bit displacement follows

            // extract displacement
            consumed = extract_displacement(ptr, bit_w, &displacement, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            // extract data
            consumed = explicit_extract_data(ptr, bit_s, bit_w, &data, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            if (displacement == 0) {
                OUTPUT("%s %s [%s], %d\n", op, data_type, destination, data);
            } else {
                OUTPUT("%s %s [%s + %d], %d\n", op, data_type, destination, displacement, data);
            }
            break;

        case 0x3:
            // Register Mode, No Displacement

            // extract data
            consumed = explicit_extract_data(ptr, bit_s, bit_w, &data, consumed_bytes);
            consumed_bytes += consumed;
            ptr += consumed;

            OUTPUT("%s %s, %d\n", op, destination, data);
            break;
    }

    return consumed_bytes;
}

/*
 * Process a Immediate to Accumulator OP
 *
 * Returns number of bytes consumed, so the caller
 * can skip ahead to process the next unprocessed bytes
 * in the buffer
 */
int process_immediate_accumulator_op_inst(uint8_t *ptr, int op_type) {
    int16_t data = 0;
    int consumed = 0;
    int consumed_bytes = 1;
    uint8_t bit_w = (*ptr & 0x1);
    char *op_type_str = NULL;
    char *op = NULL;
    char *reg = "AX";

    switch (op_type) {
        case ADD_IMMEDIATE_TO_ACCUMULATOR:
            op_type_str = "ADD_IMMEDIATE_TO_ACCUMULATOR";
            op = "add";
            break;
        case SUB_IMMEDIATE_FROM_ACCUMULATOR:
            op_type_str = "SUB_IMMEDIATE_FROM_ACCUMULATOR";
            op = "sub";
            break;
        case CMP_IMMEDIATE_WITH_ACCUMULATOR:
            op_type_str = "CMP_IMMEDIATE_WITH_ACCUMULATOR";
            op = "cmp";
            break;
        default:
            ERROR("[%s:%d] ERROR: Bad OP[0x%x]\n", __FUNCTION__, __LINE__, op_type);
    }
    LOG("; [0x%02x] %s Byte - Found %s bitstream | W[%d]\n", *ptr, byte_count_str[consumed_bytes], op_type_str, bit_w);

    // extract data
    consumed = extract_data(ptr, bit_w, &data, consumed_bytes);
    consumed_bytes += consumed;
    ptr += consumed;

    if (bit_w == 0x0) {
        reg = "AL";
    }

    printf("%s %s, %d\n", op, reg, data);
    fprintf(out_fp, "%s %s, %d\n", op, reg, data);

    return consumed_bytes;
}

int main (int argc, char *argv[]) {
    int opt;
    char *input_file = NULL;
    char *output_file = NULL;    
    uint8_t *buffer = NULL;
    size_t bytes_available;

    while( (opt = getopt(argc, argv, "hi:o:v")) != -1) {
        switch (opt) {
            case 'h':
                usage();
                exit(0);
                break;
            
            case 'i':
                input_file = strdup(optarg);
                break;

            case 'o':
                output_file = strdup(optarg);
                break;

            case 'v':
                verbose = true;
                break;

            default:
                fprintf(stderr, "ERROR Invalid command line option\n");
                usage();
                exit(1);
                break;
        }
    }

    LOG("========================\n");
    LOG("8086 Instruction Decoder\n");
    LOG("========================\n");

    if (!input_file) {
        fprintf(stderr, "ERROR Missing Input File\n");
        usage();
        exit(1);
    }
    if (!output_file) {
        fprintf(stderr, "ERROR Missing Output File\n");
        usage();
        exit(1);
    }

    LOG("Using Input Filename     [%s]\n", input_file);
    LOG("Using Output Filename    [%s]\n", output_file);
    LOG("\n\n");

    buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        fprintf(stderr, "ERROR malloc failed\n");
        exit(1);
    }
    
    in_fp = fopen(input_file, "r");
    if (!in_fp) {
        fprintf(stderr, "ERROR input_file fopen failed [%d][%s]\n", errno, strerror(errno));
        exit(1);
    }
    LOG("Opened Input file[%s] OK\n", input_file);

    out_fp = fopen(output_file, "w");
    if (!in_fp) {
        fprintf(stderr, "ERROR output_file fopen failed [%d][%s]\n", errno, strerror(errno));
        exit(1);
    }
    LOG("Opened Output file[%s] OK\n\n", output_file);

    LOG("Decoding Results:\n");
    LOG("-----------------\n");


    printf("; 8086 Instruction Decoder Result Output\n\n");
    fprintf(out_fp, "; 8086 Instruction Decoder Result Output\n\n");
    printf("bits 16\n\n");
    fprintf(out_fp, "bits 16\n\n");

    bytes_available = -1;
    while ( (bytes_available = fread(buffer, 1, BUFFER_SIZE, in_fp)) != 0) {
        uint8_t *ptr = buffer;
        LOG("; Read [%zu] bytes from file\n\n", bytes_available);

        // 8086 instructions can be encoded from 1 to 6 bytes
        // so we inspect on a per byte basis
        while (bytes_available>0) {
            int consumed_bytes = 0;
            if ((*ptr>>1) == MOV_IMMEDIATE_TO_REGMEM_INSTRUCTION) {
                // found immediate to register/memory
                consumed_bytes = process_mov_immediate_to_register_memory(ptr);
            } else if ((*ptr>>4) == MOV_IMMEDIATE_TO_REG_INSTRUCTION) {
                // found immediate to register move
                consumed_bytes = process_mov_immediate_to_register(ptr);
            } else if ( (*ptr>>2) == MOV_REGMEM_REG_INSTRUCTION) {
                // found move register/memory to/from register
                consumed_bytes = process_regmem_tpo_reg_op_inst(ptr, MOV_REGMEM_REG_INSTRUCTION);
            } else if ((*ptr>>1) == MOV_MEMORY_TO_ACCUMULATOR) {
                // found memory to accumulator move
                consumed_bytes = process_mov_accumulator_inst(ptr, MOV_MEMORY_TO_ACCUMULATOR);
            } else if ((*ptr>>1) == MOV_ACCUMULATOR_TO_MEMORY) {
                // found memory to accumulator move
                consumed_bytes = process_mov_accumulator_inst(ptr, MOV_ACCUMULATOR_TO_MEMORY);
            } else if ((*ptr>>2) == ADD_REGMEM_WITH_REG) {
                // found reg/memory add with register add
                consumed_bytes = process_regmem_tpo_reg_op_inst(ptr, ADD_REGMEM_WITH_REG);
            } else if ((*ptr>>2) == ADD_IMMEDIATE_TO_REGMEM) {
                // found immediate to register/memory add and sub
                // op codes for 
                //      ADD_IMMEDIATE_TO_REGMEM 
                //      SUB_IMMEDIATE_TO_REGMEM
                //      CMP_IMMEDIATE_TO_REGMEM
                // are the same
                consumed_bytes = process_immediate_to_regmem_op_inst(ptr, ADD_IMMEDIATE_TO_REGMEM);
            } else if ((*ptr>>1) == ADD_IMMEDIATE_TO_ACCUMULATOR) {
                // found immediate to accumulator add
                consumed_bytes = process_immediate_accumulator_op_inst(ptr, ADD_IMMEDIATE_TO_ACCUMULATOR);
            } else if ((*ptr>>2) == SUB_REGMEM_WITH_REG) {
                // found reg/memory with register sub
                consumed_bytes = process_regmem_tpo_reg_op_inst(ptr, SUB_REGMEM_WITH_REG);
            } else if ((*ptr>>1) == SUB_IMMEDIATE_FROM_ACCUMULATOR) {
                // found reg/memory with register sub
                consumed_bytes = process_immediate_accumulator_op_inst(ptr, SUB_IMMEDIATE_FROM_ACCUMULATOR);
            } else if ((*ptr>>2) == CMP_REGMEM_WITH_REG) {
                // found reg/memory with register sub
                consumed_bytes = process_regmem_tpo_reg_op_inst(ptr, CMP_REGMEM_WITH_REG);
            } else if ((*ptr>>1) == CMP_IMMEDIATE_WITH_ACCUMULATOR) {
                // found reg/memory with register sub
                consumed_bytes = process_immediate_accumulator_op_inst(ptr, CMP_IMMEDIATE_WITH_ACCUMULATOR);
            } else {
                ERROR("; [0x%02x] not a recognized instruction, aborting...\n", *ptr);
            }
            LOG("\n");
            ptr+=consumed_bytes;
            bytes_available -= consumed_bytes;
        }
    }
    LOG("; Read [%zu] bytes from file\n", bytes_available);

    fclose(in_fp);
    fclose(out_fp);

    return 0;
}
