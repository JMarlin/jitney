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

jnc_obj* jnc_new_numberobj(int number) {

    return jnc_new_atom(OT_NUMBER, number);
}

jnc_obj* jnc_new_errorobj(int error_code) {

    return jnc_new_atom(OT_ERROR, error_code);
}

jnc_obj* jnc_new_operatorobj(char op) {

    jnc_operatorobj* out_op = (jnc_operatorobj*)malloc(sizeof(jnc_operatorobj));

    int operation = op == '+' ? OP_ADD :
                    op == '-' ? OP_SUB :
                    op == '*' ? OP_MLT :
                    op == '/' ? OP_DIV :
                    op == '%' ? OP_MOD :
                    0x0;

    if(op)
        return jnc_new_atom(OT_OPERATOR, op);
    else
        return jnc_new_errorobj(ERR_UNKOWN_OP_SYM);
}

typedef int (*translated_function)(void);

int jnc_jumpInto(unsigned char* start_address, jnc_obj** return_obj) {

    int retval = 0xFFFFFFFF;
    
    translated_function target_code = (translated_function)mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    memcpy((void*)target_code, (void*)start_address, 1024);
    printf("Jumping into translated code...");

    retval = target_code();

    printf("Returned\n");

    munmap((void*)target_code, 4096);
    (*return_obj) = jnc_new_numberobj(retval);

    return 1;
}

jnc_cons* parse_list_obj(scan_wrapper* scan);

jnc_cons* parse_decimal_obj(scan_wrapper* scan) {

    int number = 0;
    int is_negative = 0;
    jnc_numberobj* out_number;
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
        return jnc_new_errorobj("Invalid character at the end of decimal literal");

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

    return jnc_new_errorobj("Failed to detect type of next value");
}

//THIS NEEDS TO BE TWEAKED
//WE SHOULD JUST BE RETURNING A STANDARD CONS CELL WITH THE 
//FIRST OBJECT VALUE AND POINTING TO THE REST OF THE LIST UNLESS
//THE NEXT OBJET WE PARSED -- ASDLKJASKJDfh I don't know, this needs review to handle nested lists properly in the CONS way
jnc_cons* parse_list_obj(scan_wrapper* scan) {
    
    jnc_cons* out_list = jnc_new_atom(OT_LIST, 0);
    jnc_cons* new_child;
    jnc_cons* prev_child;
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
            return jnc_new_errorobj("Failure trying to allocate a new object");
        }

        if(new_child->type == OT_ERROR) {

            jnc_freeObj((jnc_obj*)out_list);
            return new_child;
        }

        if(prev_child) 
            jnc_do_cons(prev_child, new_child);            
        else
            out_list->value = (int)new_child;

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

    if(source_tree->type != OT_LIST) {

        printf("Parse error: Root object MUST be a list\n");
        return -3;
    }

    (*out_object) = source_tree;

    return 1;
}

void jnc_freeObj(jnc_cons* object) {

    if(!object)
        return;

    if(object->type == OT_LIST) {
    
        jnc_freeObj((jnc_cons*)object->value);
    }

    jnc_freeObj(objet->next);
    free(object);
}

void jnc_printObj(jnc_obj* object) {
 
    if(!object) {

        printf("]");
        return;
    }

    switch(object->type) {

        case OT_LIST:

            printf("[");
            
            jnc_printObj((jnc_cons*)object->value);
        break;

        case OT_NUMBER:

            printf("%i", ((jnc_numberobj*)object)->value);
        break;

        case OT_OPERATOR:            

            printf(obj_op->operation == OP_ADD ? "+" :
                   obj_op->operation == OP_SUB ? "-" :
                   obj_op->operation == OP_MLT ? "*" :
                   obj_op->operation == OP_DIV ? "/" :
                   obj_op->operation == OP_MOD ? "%%" :
                   "|UNKNOWN_OPERATION|");

        break;

        default:

            printf("|UNKNOWN_TYPE:'0x%X'|", (unsigned int)object->type);
        break;
    }

    jnc_printObj(object->next);    
}

//THIS CAN BASICALLY BE REMOVED SINCE OUR TREES AND OUR BYTECODE ARE BOTH THE
//SAME BINARY S-EXPRESSION FORMAT
int jnc_tree_to_bytecode(jnc_obj* tree, unsigned char* dest_buf, int* buf_loc) {

    jnc_listobj* obj_list;
    jnc_numberobj* obj_number;

    //If we're a literal, we just need to emit a load immediate
    if(tree->type == OT_NUMBER) {

        obj_number = (jnc_numberobj*)tree;

        dest_buf[(*buf_loc)++] = VMI(VIP_LDA, VAM_IM);
        dest_buf[(*buf_loc)++] = (unsigned char)(obj_number->value & 0xFF);
        dest_buf[(*buf_loc)++] = (unsigned char)((obj_number->value >> 8) & 0xFF);
        dest_buf[(*buf_loc)++] = (unsigned char)((obj_number->value >> 16) & 0xFF);
        dest_buf[(*buf_loc)++] = (unsigned char)((obj_number->value >> 24) & 0xFF);
        
        return 1;
    }

    //If we're a tree then we evaluate as an expression
    if(tree->type == OT_LIST) {
            
        obj_list = (jnc_listobj*)tree;

        if(obj_list->member_count == 0) {

            printf("Compile Error: Encountered an empty list where an expression was expected.\n");
            return -2;
        }

        if(obj_list->member[0]->type != OT_OPERATOR) {

            printf("Compile Error: Expressions must begin with an operator /*or function*/\n");
            return -3;
        }

        if(obj_list->member_count < 3) {

            printf("Compile Error: Too few arguments to operation. All current operations are binary.\n");
            return -4;
        }

        int i;

        //Insert argument value calculations
        for(i = obj_list->member_count - 1; i > 0; i--) {

            if(jnc_tree_to_bytecode(obj_list->member[i], dest_buf, buf_loc) < 0)
                return -1;

            //Push the accumulator value onto the stack if not the leading argument
            if(i != 1)
                dest_buf[(*buf_loc)++] = VMI(VIP_PUSH, 0);
        }

        //Apply operator to values
        for(i = 0; i < obj_list->member_count - 2; i++) {

            switch(((jnc_operatorobj*)(obj_list->member[0]))->operation) {

                case OP_ADD:
                    dest_buf[(*buf_loc)++] = VMI(VIP_ADD, VAM_RI);
                break;

                case OP_SUB:
                    dest_buf[(*buf_loc)++] = VMI(VIP_SUB, VAM_RI);
                break;

                case OP_MLT:
                    dest_buf[(*buf_loc)++] = VMI(VIP_MLT, VAM_RI);
                break;

                case OP_DIV:
                    dest_buf[(*buf_loc)++] = VMI(VIP_DIV, VAM_RI);
                break;

                case OP_MOD:
                    dest_buf[(*buf_loc)++] = VMI(VIP_MOD, VAM_RI);
                break;

                default:

                    printf("Compile Error: Encountered unknown operator of type 0x%X\n", ((jnc_operatorobj*)(obj_list->member[0]))->operation);
                    return -1;
                break;
            }

            dest_buf[(*buf_loc)++] = VRN_SP;
            dest_buf[(*buf_loc)++] = VMI(VIP_DROP, 0);
        }

        return 1;
    }

    printf("Compile Error: Node was neither a list or a literal\n");
    return -1;
}

//THIS CAN ALSO BE REMOVED SINCE REALLY THIS IS THE SAME THING AS PRINTING THE TREE NOW
void jnc_disassemble(unsigned char* code, int size) {

    int immval;

    while(size) {

        printf("0x%02X: ", *code);

        switch(((*code) & 0xF0) >> 4) {

            case VIP_ADD:
                printf("ADD");
            break;

            case VIP_SUB:
                printf("SUB");
            break;

            case VIP_MLT:
                printf("MLT");
            break;

            case VIP_DIV:
                printf("DIV");
            break;

            case VIP_MOD:
                printf("MOD");
            break;

            case VIP_LDA:
                printf("LDA");
            break;

            case VIP_PUSH:
                printf("PUSH\n");
                code++; size--;
                continue;
            break;

            case VIP_DROP:
                printf("DROP\n");
                code++; size--;
                continue;
            break;

            default:
                printf("???\n");
                code++; size--;
                continue;
            break; 
        }

        switch((*code) & 0xF) {

            case VAM_IM:
                immval = 0;
                immval |= *(++code);
                immval |= *(++code) << 8;
                immval |= *(++code) << 16;
                immval |= *(++code) << 24;
                size -= 4;
                printf("I 0x%X\n", immval);
            break;

            case VAM_RI:

                size -= 1;

                if(*(++code) == VRN_SP) {
                    printf(" [SP]\n");
                } else {
                    printf(" [?]\n");
                }
            break;

            default:
                printf("?\n");
            break;
        }

        code++; size--;
        continue;
    }
}

int jnc_compile(char* source, unsigned char* dest_buf) {

    jnc_obj* source_tree;
    int byte_count = 0;    

    if(jnc_objectify(source, &source_tree) < 0)
        return -1;

    //DEBUG
    printf("Compiling: ");
    jnc_printObj(source_tree);
    printf("\n");

    //THIS NEEDS TO BE REPLACED WITH A FUNCTION THAT TAKES THE FRAGMENTED TREE WE PRODUCED
    //AND CONSOLIDATES IT DOWN INTO A LINEAR BUFFER WITH ADJUSTED POINTER VALUES 
    if(jnc_tree_to_bytecode(source_tree, dest_buf, &byte_count) < 0)
        return -2;

    //DEBUG
    //REPLACE THIS WITH A PRINT OF THE CONSOLIDATED TREE IN THE BUFFER 
    jnc_disassemble(dest_buf, byte_count);

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