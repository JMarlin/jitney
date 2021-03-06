#include <stdio.h>
#include <stdlib.h>
#include <jnc.h>
#include <sys/mman.h>
#include <memory.h>

#define ERR_UNKOWN_OP_SYM 0x01
#define ERR_ZERO_LENGTH_NUM 0x02
#define ERR_INV_CHAR_AFTER_OP 0x03
#define ERR_LIST_EXPECTED 0x04
#define ERR_CLOSING_BRACKET_EXPECTED 0x05
#define ERR_INV_CHAR_IN_DECIMAL_LIT 0x06
#define ERR_COULDNT_PARSE_NEXT 0x7
#define ERR_OBJ_ALLOCATION 0x8

const char* errval[] = {
    "Unkown error",
    "Encountered an unrecognized operator symbol",
    "Encountered a number object of zero length",
    "Encountered an invalid character following operator symbol",
    "Was expecting a list (opening bracket character)",
    "Was expecting a closing bracket",
    "Invalid character found in decimal literal",
    "Was unable to parse next symbol",
    "Failed to allocate memory for new atom"
};

#define VMI(i, m) (unsigned char)((((i) << 4) & 0xF0)|((m) & 0xF))

#define VIP_ADD 0x1
#define VIP_SUB 0x2
#define VIP_MLT 0x3
#define VIP_DIV 0x4
#define VIP_MOD 0x5
#define VIP_PUSH 0x6
#define VIP_DROP 0x7
#define VIP_LDA 0x8

#define VAM_IM 0x1
#define VAM_RI 0x2

#define VRN_SP 0x2

typedef struct scan_wrapper_struct {
    char* c;
    int idx;
    int undo_point;
    int undo_row;
    int undo_col;
    int row;
    int col;
    unsigned char eof;
} scan_wrapper;

void scanner_set_undo_point(scan_wrapper* scan) {

    scan->undo_point = scan->idx;
    scan->undo_row = scan->row;
    scan->undo_col = scan->col;
}

void scanner_undo(scan_wrapper* scan) {

    scan->idx = scan->undo_point;
    scan->row = scan->undo_row;
    scan->col = scan->undo_col;
}

void scanner_next(scan_wrapper* scan) {

    if(scan->eof)
        return;

    scan->idx++;
    scan->col++;

    if(scan->c[scan->idx] == '\n') {

        scan->col = 0;
        scan->row++;
    }

    if(!scan->c[scan->idx])
        scan->eof = 1;
}

char scanner_value(scan_wrapper* scan) {

    return scan->c[scan->idx];
}

int scanner_iswhite(scan_wrapper* scan) {

    char val = scanner_value(scan);

    return (val == '\n' || val == '\t' || val == ' ');
}

void scanner_skipwhite(scan_wrapper* scan) {

    while(scanner_iswhite(scan) && !scan->eof)
        scanner_next(scan);
}

jnc_cons* jnc_new_atom(unsigned char type, int value) {

    jnc_cons* out_cons = (jnc_cons*)malloc(sizeof(jnc_cons));

    if(!out_cons)
        return (jnc_cons*)0;

    out_cons->type = type;
    out_cons->value = value;
    out_cons->next = 0;

    return out_cons;
}

void jnc_do_cons(jnc_cons* car, jnc_cons* cdr) {

    car->next = cdr;
}

jnc_cons* jnc_new_numberobj(int number) {

    return jnc_new_atom(OT_NUMBER, number);
}

jnc_cons* jnc_new_errorobj(int error_code) {

    return jnc_new_atom(OT_ERROR, error_code);
}

jnc_cons* jnc_new_operatorobj(char op) {

    int operation = op == '+' ? OP_ADD :
                    op == '-' ? OP_SUB :
                    op == '*' ? OP_MLT :
                    op == '/' ? OP_DIV :
                    op == '%' ? OP_MOD :
                    0x0;

    if(op)
        return jnc_new_atom(OT_OPERATOR, operation);
    else
        return jnc_new_errorobj(ERR_UNKOWN_OP_SYM);
}

typedef jnc_cons* (*translated_function)(void);

int jnc_jumpInto(unsigned char* start_address, jnc_cons** return_obj) {

    jnc_cons* retval = (jnc_cons*)0xFFFFFFFF;
    
    translated_function target_code = (translated_function)mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    memcpy((void*)target_code, (void*)start_address, 1024);
    printf("Jumping into translated code...");

    retval = target_code();

    printf("Returned\n");

    munmap((void*)target_code, 4096);
    (*return_obj) = retval;

    return 1;
}

jnc_cons* parse_list_obj(scan_wrapper* scan);

jnc_cons* parse_decimal_obj(scan_wrapper* scan) {

    int number = 0;
    int is_negative = 0;
    char val;
    int digits = 0;

    scanner_skipwhite(scan);

    //Check for sign
    if(scanner_value(scan) == '-') {

        is_negative = 1;
        scanner_next(scan);
    } else if(scanner_value(scan) == '+') {

        scanner_next(scan);
    }

    val = scanner_value(scan);

    while(val >= '0' && val <= '9') {

        number *= 10;
        number += val - '0';
        digits++;

        scanner_next(scan);
        val = scanner_value(scan);
    }

    if(!digits)
        return jnc_new_errorobj(ERR_ZERO_LENGTH_NUM);

    if(is_negative)
        number *= -1;

    //Make sure the number is terminated by a delimiter
    //or a list close bracket
    if(!scanner_iswhite(scan) && scanner_value(scan) != ']')
        return jnc_new_errorobj(ERR_INV_CHAR_IN_DECIMAL_LIT);

    return jnc_new_numberobj(number);
}

jnc_cons* parse_operator_obj(scan_wrapper* scan) {

    char op = 0;

    scanner_skipwhite(scan);

    switch(op = scanner_value(scan)) {

        case '+': case '-': case '*': case '/': case '%': 
        break;

        default:
            op = 0;
        break;
    }

    if(!op)
        return jnc_new_errorobj(ERR_UNKOWN_OP_SYM);

    scanner_next(scan);
    if(!scanner_iswhite(scan) && scanner_value(scan) != ']')
        return jnc_new_errorobj(ERR_INV_CHAR_AFTER_OP);

    return jnc_new_operatorobj(op);
}

jnc_cons* jnc_parse_object(scan_wrapper* scan) {

    jnc_cons* ret_obj;

    //Try parsing as list 
    scanner_set_undo_point(scan);
    ret_obj = parse_list_obj(scan);
    
    if(ret_obj->type != OT_ERROR)
        return ret_obj;

    jnc_freeObj(ret_obj);
    scanner_undo(scan);
    ret_obj = parse_decimal_obj(scan);

    if(ret_obj->type != OT_ERROR)
        return ret_obj;

    jnc_freeObj(ret_obj);
    scanner_undo(scan);
    ret_obj = parse_operator_obj(scan);

    if(ret_obj->type != OT_ERROR)
        return ret_obj;

    //TODO: Add other type checks here
    //note: would probably work better if
    //we put all of the parsers in a list
    //and iterated them

    //Finally, error on unknown type
    if(ret_obj)
        jnc_freeObj(ret_obj);

    scanner_undo(scan);

    return jnc_new_errorobj(ERR_COULDNT_PARSE_NEXT);
}

jnc_cons* parse_list_obj(scan_wrapper* scan) {
    
    jnc_cons* out_list = jnc_new_atom(OT_CONS, 0);
    jnc_cons* new_child;
    jnc_cons* prev_child = 0;
    int found_first = 0;

    scanner_skipwhite(scan);    

    if(scanner_value(scan) != '[')
        return jnc_new_errorobj(ERR_LIST_EXPECTED);
    
    scanner_next(scan); //Consume the leading bracket
    scanner_skipwhite(scan);

    while(scanner_value(scan) != ']') {

        if(scan->eof) {

            jnc_freeObj(out_list);
            return jnc_new_errorobj(ERR_CLOSING_BRACKET_EXPECTED);
        }

        new_child = jnc_parse_object(scan);

        if(!new_child) {

            jnc_freeObj(out_list);
            return jnc_new_errorobj(ERR_OBJ_ALLOCATION);
        }

        if(new_child->type == OT_ERROR) {

            jnc_freeObj(out_list);
            return new_child;
        }

        if(prev_child) 
            jnc_do_cons(prev_child, new_child);            
        else
            out_list->value = (uint64_t)new_child;

        prev_child = new_child;

        scanner_skipwhite(scan);
    }

    //Consume the closing bracket
    scanner_next(scan);

    return out_list;
}

int jnc_objectify(char* source, jnc_cons** out_object) {
    
    scan_wrapper scan = {source, 0, 0, 0, 0, 0, 0, 0}; 
    jnc_cons* source_tree;

    source_tree = parse_list_obj(&scan);
    
    if(!source_tree) {

        printf("Error allocating a source list.\n");
        return -1;
    }

    if(source_tree->type == OT_ERROR) {

        printf("Parse error [%i, %i]: %s\n", scan.row, scan.col, errval[source_tree->value]);
        jnc_freeObj(source_tree);
        return -2;
    }

    if(source_tree->type != OT_CONS) {

        printf("Parse error: Root object MUST be a list\n");
        return -3;
    }

    (*out_object) = source_tree;

    return 1;
}

void jnc_freeObj(jnc_cons* object) {

    if(!object)
        return;

    if(object->type == OT_CONS) {
    
        jnc_freeObj((jnc_cons*)object->value);
    }

    jnc_freeObj(object->next);
    free(object);
}

void jnc_printInner(jnc_cons* object, int nesting, int list_index) {
   
    if(list_index > 0)
        printf(" ");

    switch(object->type) {

        case OT_CONS:

            if(nesting > 0) {

                printf("\n");
                
                int i;
                for(i = 0; i < nesting; i++)
                    printf("    ");
            }

            printf("[");
            
            jnc_printInner((jnc_cons*)object->value, nesting + 1, 0);
        break;

        case OT_NUMBER:

            printf("%i", (int)object->value);
        break;

        case OT_OPERATOR:                       

            printf(object->value == OP_ADD ? "+" :
                   object->value == OP_SUB ? "-" :
                   object->value == OP_MLT ? "*" :
                   object->value == OP_DIV ? "/" :
                   object->value == OP_MOD ? "%%" :
                   "|UNKNOWN_OPERATION|");

        break;

        default:

            printf("|UNKNOWN_TYPE:'0x%X'|", (int)object->type);
        break;
    }

    if(!object->next) {

        if(nesting != 0) printf("]");
        return;
    } else {

        jnc_printInner(object->next, nesting, list_index + 1);    
    }
}

void jnc_printObj(jnc_cons* object) {

    jnc_printInner(object, 0, 0);
}

int jnc_condense_cons(jnc_cons* root_cons, unsigned char* out_buf, uint64_t* count, uint64_t base) {

    unsigned char* cdr_ptr;
    jnc_cons* next_ptr;

    //Start by shoving the type into the buffer 
    *(out_buf++) = root_cons->type;
    
    //Denote that we're shoving another cons into the buf 
    (*count) += 17;

    if(root_cons->type == OT_CONS) {           

        //Insert the 'next' value into the value slot
        *(out_buf++) = (unsigned char)(((*count)+base) & 0xFF);
        *(out_buf++) = (unsigned char)((((*count)+base) >> 8) & 0xFF);
        *(out_buf++) = (unsigned char)((((*count)+base) >> 16) & 0xFF);
        *(out_buf++) = (unsigned char)((((*count)+base) >> 24) & 0xFF);
        *(out_buf++) = (unsigned char)((((*count)+base) >> 32) & 0xFF);
        *(out_buf++) = (unsigned char)((((*count)+base) >> 40) & 0xFF);
        *(out_buf++) = (unsigned char)((((*count)+base) >> 48) & 0xFF);
        *(out_buf++) = (unsigned char)((((*count)+base) >> 56) & 0xFF);

        //Save the location that we should store the CDR value at 
        cdr_ptr = out_buf;

        //Increment past the cdr location
        out_buf += 8; 

        //Process and insert the left branch
        if(jnc_condense_cons((jnc_cons*)root_cons->value, out_buf, count, base) < 0)
            return -1;

    } else {

        //Insert the raw value 
        *(out_buf++) = (unsigned char)(root_cons->value & 0xFF);
        *(out_buf++) = (unsigned char)((root_cons->value >> 8) & 0xFF);
        *(out_buf++) = (unsigned char)((root_cons->value >> 16) & 0xFF);
        *(out_buf++) = (unsigned char)((root_cons->value >> 24) & 0xFF);
        *(out_buf++) = (unsigned char)((root_cons->value >> 32) & 0xFF);
        *(out_buf++) = (unsigned char)((root_cons->value >> 40) & 0xFF);
        *(out_buf++) = (unsigned char)((root_cons->value >> 48) & 0xFF);
        *(out_buf++) = (unsigned char)((root_cons->value >> 56) & 0xFF);

        cdr_ptr = out_buf;
        out_buf += 8; 
    }

    next_ptr = root_cons->next == 0 ? 0 : (jnc_cons*)((*count)+base);

    *(cdr_ptr++) = (unsigned char)((uint64_t)next_ptr & 0xFF);
    *(cdr_ptr++) = (unsigned char)(((uint64_t)next_ptr >> 8) & 0xFF);
    *(cdr_ptr++) = (unsigned char)(((uint64_t)next_ptr >> 16) & 0xFF);
    *(cdr_ptr++) = (unsigned char)(((uint64_t)next_ptr >> 24) & 0xFF);
    *(cdr_ptr++) = (unsigned char)(((uint64_t)next_ptr >> 32) & 0xFF);
    *(cdr_ptr++) = (unsigned char)(((uint64_t)next_ptr >> 40) & 0xFF);
    *(cdr_ptr++) = (unsigned char)(((uint64_t)next_ptr >> 48) & 0xFF);
    *(cdr_ptr++) = (unsigned char)(((uint64_t)next_ptr >> 56) & 0xFF);

    if(!root_cons->next)
        return 1;

    if(jnc_condense_cons(root_cons->next, out_buf, count, base) < 0)
        return -1;
}

int jnc_compile(char* source, unsigned char* dest_buf) {

    jnc_cons* source_tree;
    uint64_t byte_count = 0;    

    if(jnc_objectify(source, &source_tree) < 0)
        return -1;

    //DEBUG
    printf("Compiling: ");
    jnc_printObj(source_tree);
    printf("\n");

    if(jnc_condense_cons(source_tree, dest_buf, &byte_count, (uint64_t)dest_buf) < 0)
        return -2;

    printf("%016" PRIx64 " : ", (uint64_t)dest_buf);

    int i;
    for(i = 0; i < 256; i++)
        printf("%02X ", ((unsigned char*)dest_buf)[i]);

    //DEBUG
    //REPLACE THIS WITH A PRINT OF THE CONSOLIDATED TREE IN THE BUFFER 
    printf("Condensed binary result:\n");
    jnc_printObj((jnc_cons*)dest_buf);
    printf("\n");

    return byte_count;
}

//THIS NEEDS TO BE SERIOUSLY UPDATED TO CONVERT OUR CONSOLIDATED TREE INTO X86 CODE
int jnc_translate(unsigned char* byte_code, unsigned char* machine_code, int count) {

    int in_count = 0;
    int out_count = 0;
    int i;

    //Write preamble
    //machine_code[out_count++] = 0x50; //Push RAX //This would clobber our out value. Plus C returns in EAX
    machine_code[out_count++] = 0x53; //Push RBX //Used in DROP 
 
    while(in_count < count) {

        switch(byte_code[in_count++]) {

            case VMI(VIP_LDA, VAM_IM):
                machine_code[out_count++] = 0x48;
                machine_code[out_count++] = 0xB8; //Mov RAX immediate
                //Our VM is also little endian
                machine_code[out_count++] = byte_code[in_count++];
                machine_code[out_count++] = byte_code[in_count++];
                machine_code[out_count++] = byte_code[in_count++];
                machine_code[out_count++] = byte_code[in_count++];
                //Pad to 64 bit with zeros
                machine_code[out_count++] = 0;
                machine_code[out_count++] = 0;
                machine_code[out_count++] = 0;
                machine_code[out_count++] = 0;
            break;

            case VMI(VIP_PUSH, 0):
                machine_code[out_count++] = 0x50; //Push RAX
            break;

            case VMI(VIP_ADD, VAM_RI):
                //This should really only handle the specific case of [SP], but it assumes they're all [SP]
                in_count++; //Automatically consume the register spec
                machine_code[out_count++] = 0x48; //ADD
                machine_code[out_count++] = 0x03; //RAX,
                machine_code[out_count++] = 0x04; //[RSP] 
                machine_code[out_count++] = 0x24;
            break;

            case VMI(VIP_SUB, VAM_RI):
                //This should really only handle the specific case of [SP], but it assumes they're all [SP]
                in_count++; //Automatically consume the register spec
                machine_code[out_count++] = 0x48; //SUB
                machine_code[out_count++] = 0x2B; //RAX,
                machine_code[out_count++] = 0x04; //[RSP] 
                machine_code[out_count++] = 0x24;
            break;

            case VMI(VIP_DROP, 0):
                machine_code[out_count++] = 0x5B; //Pop RBX //Discard into RBX
            break;

            default:
                
                printf("Unhandled instrution, dumping progress:\n");

                for(i = 0; i < out_count; i++)
                    printf("%02X ", machine_code[i]);
                printf("\n");

                return -1;
            break;
        }
    }

    //Write postscript:
    machine_code[out_count++] = 0x5B; //Pop EBX //Restore EBX
    machine_code[out_count++] = 0xC3; //RET

    //DEBUG
    printf("Translation finished. Result:\n");

    for(i = 0; i < out_count; i++)
        printf("%02X ", machine_code[i]);
    printf("\n");

    return 1;
}