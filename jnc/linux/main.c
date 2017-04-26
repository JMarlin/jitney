#include <stdio.h>
#include <jnc.h>

//Main entry point for the linux JITney compiler front-end
int main(int argc, char* argv[]) {

    char inbuf[255] = {0};
    unsigned char bc_buf[1024] = {0};
    unsigned char exec_buf[1024] = {0};
    jnc_obj* return_obj;
    int count;

    //This is the REPL loop
    while(1) {
        
        printf("\n>");
        fgets(inbuf, 255, stdin);

        if((count = jnc_compile(inbuf, bc_buf)) < 1) {

            printf("Compilation terminated.\n");
            continue;
        }

        if(jnc_translate(bc_buf, exec_buf, count) < 1) {

            printf("Translation terminated.\n");
            continue;
        }

        if(jnc_jumpInto(exec_buf, &return_obj) < 1) {

            printf("Execution terminated.\n");
            continue;
        }

        printf("Return value: ");
        jnc_printObj(return_obj);
        printf("\n");
        jnc_freeObj(return_obj);
    }
}