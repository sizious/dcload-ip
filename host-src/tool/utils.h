#ifndef __UTILS_H__
#define __UTILS_H__

void log_error(const char *prefix);
void cleanup_ip_address(char *hostname);
char *exception_code_to_string(unsigned int expevt);

// Exception struct
struct _exception_struct_t {
    unsigned char id[4];    // EXPT
    unsigned int expt_code; // Exception code
    unsigned int pc;
    unsigned int pr;
    unsigned int sr;
    unsigned int gbr;
    unsigned int vbr;
    unsigned int dbr;
    unsigned int mach;
    unsigned int macl;
    unsigned int r0b0;
    unsigned int r1b0;
    unsigned int r2b0;
    unsigned int r3b0;
    unsigned int r4b0;
    unsigned int r5b0;
    unsigned int r6b0;
    unsigned int r7b0;
    unsigned int r0b1;
    unsigned int r1b1;
    unsigned int r2b1;
    unsigned int r3b1;
    unsigned int r4b1;
    unsigned int r5b1;
    unsigned int r6b1;
    unsigned int r7b1;
    unsigned int r8;
    unsigned int r9;
    unsigned int r10;
    unsigned int r11;
    unsigned int r12;
    unsigned int r13;
    unsigned int r14;
    unsigned int r15; // saved from SGR
    unsigned int fpscr;
    unsigned int fr0;
    unsigned int fr1;
    unsigned int fr2;
    unsigned int fr3;
    unsigned int fr4;
    unsigned int fr5;
    unsigned int fr6;
    unsigned int fr7;
    unsigned int fr8;
    unsigned int fr9;
    unsigned int fr10;
    unsigned int fr11;
    unsigned int fr12;
    unsigned int fr13;
    unsigned int fr14;
    unsigned int fr15;
    unsigned int fpul;
    unsigned int xf0;
    unsigned int xf1;
    unsigned int xf2;
    unsigned int xf3;
    unsigned int xf4;
    unsigned int xf5;
    unsigned int xf6;
    unsigned int xf7;
    unsigned int xf8;
    unsigned int xf9;
    unsigned int xf10;
    unsigned int xf11;
    unsigned int xf12;
    unsigned int xf13;
    unsigned int xf14;
    unsigned int xf15;
} __attribute__((__packed__));

typedef struct _exception_struct_t exception_struct_t;

extern const char *const exception_label_array[66];

#endif /* __UTILS_H__ */
