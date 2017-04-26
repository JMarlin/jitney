#include <stdio.h>
#include <stdlib.h>
#include <jnc.h>

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

jnc_obj* jnc_new_listobj(void) {

    jnc_listobj* out_list = (jnc_listobj*)malloc(sizeof(jnc_listobj));

    if(!out_list)
        return (jnc_obj*)0;

    out_list->obj.type = OT_LIST;
    out_list->member_count = 0;

    return (jnc_obj*)out_list;
}

jnc_obj* jnc_new_numberobj(int number) {

    jnc_numberobj* out_number = (jnc_numberobj*)malloc(sizeof(jnc_numberobj));

    if(!out_number)
        return (jnc_obj*)0;

    out_number->obj.type = OT_NUMBER;
    out_number->value = number;

    return (jnc_obj*)out_number;
}

jnc_obj* jnc_new_errorobj(char* message) {

    jnc_errorobj* out_error = (jnc_errorobj*)malloc(sizeof(jnc_errorobj));

    if(!out_error)
        return (jnc_obj*)0;

    out_error->obj.type = OT_ERROR;
    out_error->message = message;

    return (jnc_obj*)out_error;
}

jnc_obj* jnc_new_operatorobj(char op) {

    jnc_operatorobj* out_op = (jnc_operatorobj*)malloc(sizeof(jnc_operatorobj));

    if(!out_op)
        return (jnc_obj*)0;

    out_op->obj.type = OT_OPERATOR;
    out_op->operation = op == '+' ? OP_ADD :
                           op == '-' ? OP_SUB :
                           op == '*' ? OP_MLT :
                           op == '/' ? OP_DIV :
                           op == '%' ? OP_MOD :
                           0x0;

    return (jnc_obj*)out_op;
}

int jnc_jumpInto(unsigned char* start_address, jnc_obj** return_obj) {

    printf("Function jnc_jumpInto() not yet implemented.\n");

    (*return_obj) = 0;

    return 1;
}

int jnc_insert_member(jnc_listobj** list_obj, jnc_obj* new_obj) {

    (*list_obj)->member_count++;
    (*list_obj) = (jnc_listobj*)realloc((*list_obj), sizeof(jnc_obj*) * (*list_obj)->member_count + sizeof(jnc_listobj));
    
    if(!(*list_obj))
        return -1;

    (*list_obj)->member[(*list_obj)->member_count - 1] = new_obj;

    return 1;
}

jnc_obj* parse_list_obj(scan_wrapper* scan);

jnc_obj* parse_decimal_obj(scan_wrapper* scan) {

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
        return jnc_new_errorobj("Encountered a zero-length number");

    if(is_negative)
        number *= -1;

    //Make sure the number is terminated by a delimiter
    //or a list close bracket
    if(!scanner_iswhite(scan) && scanner_value(scan) != ']')
        return jnc_new_errorobj("Invalid character at the end of decimal literal");

    return (jnc_obj*)jnc_new_numberobj(number);
}

jnc_obj* parse_operator_obj(scan_wrapper* scan) {

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
        return jnc_new_errorobj("Unrecognized operator type");

    scanner_next(scan);
    if(!scanner_iswhite(scan) && scanner_value(scan) != ']')
        return jnc_new_errorobj("Invalid character following operator");

    return jnc_new_operatorobj(op);
}

jnc_obj* jnc_parse_object(scan_wrapper* scan) {

    jnc_obj* ret_obj;

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

jnc_obj* parse_list_obj(scan_wrapper* scan) {
    
    jnc_listobj* out_list;
    jnc_obj* new_child;

    scanner_skipwhite(scan);    

    if(scanner_value(scan) != '[')
        return jnc_new_errorobj("Expected list beginning with '['");
    
    if(!(out_list = (jnc_listobj*)jnc_new_listobj()))
        return (jnc_obj*)0;

    scanner_next(scan); //Consume the leading bracket
    scanner_skipwhite(scan);

    while(scanner_value(scan) != ']') {

        if(scan->eof) {

            jnc_freeObj((jnc_obj*)out_list);
            return jnc_new_errorobj("Expected closing ']' at end of list");
        }

        new_child = jnc_parse_object(scan);

        if(!new_child) {

            jnc_freeObj((jnc_obj*)out_list);
            return jnc_new_errorobj("Failure trying to allocate a new object");
        }

        if(new_child->type == OT_ERROR) {

            jnc_freeObj((jnc_obj*)out_list);
            return new_child;
        }

        if(jnc_insert_member(&out_list, new_child) < 1) {

            jnc_freeObj((jnc_obj*)out_list); 
            jnc_freeObj(new_child);
            return jnc_new_errorobj("Failure trying to insert member into list object");
        }

        scanner_skipwhite(scan);
    }

    //Consume the closing bracket
    scanner_next(scan);

    return (jnc_obj*)out_list;
}

int jnc_objectify(char* source, jnc_obj** out_object) {
    
    scan_wrapper scan = {source, 0, 0, 0, 0, 0, 0, 0}; 
    jnc_obj* source_tree;

    source_tree = parse_list_obj(&scan);
    
    if(!source_tree) {

        printf("Error allocating a source list.\n");
        return -1;
    }

    if(source_tree->type == OT_ERROR) {

        printf("Parse error [%i, %i]: %s\n", scan.row, scan.col, ((jnc_errorobj*)source_tree)->message);
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

void jnc_freeObj(jnc_obj* object) {

    jnc_listobj* obj_list;
    int i;

    if(!object)
        return;

    if(object->type == OT_LIST) {

        obj_list = (jnc_listobj*)object;

        for(i = 0; i < obj_list->member_count; i++)
            jnc_freeObj(obj_list->member[i]);
    }

    free(object);
}

void jnc_printObj(jnc_obj* object) {

    jnc_operatorobj* obj_op = (jnc_operatorobj*)object;

    if(!object) {

        printf("|NULL_PTR|");
        return;
    }

    switch(object->type) {

        case OT_LIST:

            printf("[");

            int i;

            for(i = 0; i < ((jnc_listobj*)object)->member_count; i++) {

                if(i > 0)
                    printf(" ");

                jnc_printObj(((jnc_listobj*)object)->member[i]);
            }
        
            printf("]");
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
}

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
                printf("I 0x%X\n", immval);
            break;

            case VAM_RI:

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

    if(jnc_tree_to_bytecode(source_tree, dest_buf, &byte_count) < 0)
        return -2;

    //DEBUG
    jnc_disassemble(dest_buf, byte_count);

    return 1;
}

int jnc_translate(unsigned char* byte_code, unsigned char* machine_code) {

    printf("Function jnc_translate() not yet implemented.\n");
    
    return 1;
}