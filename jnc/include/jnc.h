#include <inttypes.h>

#define OT_CONS 0x01
#define OT_NUMBER 0x02
#define OT_ERROR 0x03
#define OT_OPERATOR 0x04

#define OP_ADD 0x01
#define OP_SUB 0x02
#define OP_MLT 0x03
#define OP_DIV 0x04
#define OP_MOD 0x05

typedef struct __attribute__((packed)) jnc_cons_struct {
    unsigned char type;
    uint64_t value;
    struct jnc_cons_struct* next;
} jnc_cons;

int jnc_compile(char* source, unsigned char* dest_buf);
int jnc_translate(unsigned char* byte_code, unsigned char* machine_code, int count);
int jnc_jumpInto(unsigned char* start_address, jnc_cons** return_obj);
void jnc_printObj(jnc_cons* object);
void jnc_freeObj(jnc_cons* object);
