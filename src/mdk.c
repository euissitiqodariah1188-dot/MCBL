#include "mdk.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

/* -----------------------------------------------------------------------
   McBL# MDK Virtual Machine implementation
   ----------------------------------------------------------------------- */

MdkValue mdk_mkint  (int64_t i) { return (MdkValue){.type=MDK_VAL_INT,   .ival=i}; }
MdkValue mdk_mkfloat(double  f) { return (MdkValue){.type=MDK_VAL_FLOAT, .fval=f}; }
MdkValue mdk_mkbool (int     b) { return (MdkValue){.type=MDK_VAL_BOOL,  .bval=b}; }
MdkValue mdk_mknull (void)      { return (MdkValue){.type=MDK_VAL_NULL}; }

MdkValue mdk_mkstr(const char *s) {
    MdkValue v; v.type = MDK_VAL_STRING;
    v.sval = s ? mcbl_strdup(s) : mcbl_strdup("");
    return v;
}

void mdk_value_free(MdkValue *v) {
    if (!v) return;
    if (v->type == MDK_VAL_STRING) {
        mcbl_free((void **)&v->sval);
    }
    v->type = MDK_VAL_NULL;
}

const char *mdk_val_to_str(const MdkValue *v, char *buf, size_t buf_size) {
    if (!v || !buf) return "";
    switch (v->type) {
        case MDK_VAL_INT:    snprintf(buf, buf_size, "%lld", (long long)v->ival); break;
        case MDK_VAL_FLOAT:  snprintf(buf, buf_size, "%g",  v->fval);            break;
        case MDK_VAL_BOOL:   snprintf(buf, buf_size, "%s",  v->bval ? "true" : "false"); break;
        case MDK_VAL_STRING: snprintf(buf, buf_size, "%s",  v->sval ? v->sval : ""); break;
        case MDK_VAL_NULL:   snprintf(buf, buf_size, "null"); break;
    }
    return buf;
}

MdkVM *mdk_vm_create(SymTable *symbols) {
    MdkVM *vm = (MdkVM *)mcbl_calloc(1, sizeof(MdkVM));
    vm->sp         = -1;
    vm->call_sp    = -1;
    vm->symbols    = symbols;
    vm->cargo_pool = -1;
    vm->last_input = NULL;
    return vm;
}

void mdk_vm_destroy(MdkVM *vm) {
    if (!vm) return;
    /* Free any strings still on the stack */
    for (int i = 0; i <= vm->sp; i++)
        mdk_value_free(&vm->stack[i]);
    mcbl_free((void **)&vm->last_input);
    mcbl_free((void **)&vm);
}

void mdk_push(MdkVM *vm, MdkValue v) {
    if (vm->sp >= MDK_STACK_SIZE - 1) {
        fprintf(stderr, "McBL# VM: stack overflow\n");
        mdk_value_free(&v);
        vm->error = 1;
        return;
    }
    vm->stack[++vm->sp] = v;
}

MdkValue mdk_pop(MdkVM *vm) {
    if (vm->sp < 0) {
        fprintf(stderr, "McBL# VM: stack underflow\n");
        vm->error = 1;
        return mdk_mknull();
    }
    MdkValue v = vm->stack[vm->sp--];
    return v;
}

MdkValue mdk_peek(MdkVM *vm, int offset) {
    int idx = vm->sp - offset;
    if (idx < 0 || idx >= MDK_STACK_SIZE) return mdk_mknull();
    return vm->stack[idx];
}

/* Arithmetic helpers */
static MdkValue val_add(MdkVM *vm, MdkValue a, MdkValue b) {
    (void)vm;
    if (a.type == MDK_VAL_FLOAT || b.type == MDK_VAL_FLOAT) {
        double fa = (a.type == MDK_VAL_FLOAT) ? a.fval : (double)a.ival;
        double fb = (b.type == MDK_VAL_FLOAT) ? b.fval : (double)b.ival;
        return mdk_mkfloat(fa + fb);
    }
    if (a.type == MDK_VAL_STRING || b.type == MDK_VAL_STRING) {
        char ba[256] = {0}, bb[256] = {0};
        mdk_val_to_str(&a, ba, sizeof(ba));
        mdk_val_to_str(&b, bb, sizeof(bb));
        size_t la = strlen(ba), lb = strlen(bb);
        char *r = (char *)mcbl_malloc(la + lb + 1);
        memcpy(r, ba, la); memcpy(r + la, bb, lb); r[la + lb] = '\0';
        MdkValue v; v.type = MDK_VAL_STRING; v.sval = r;
        return v;
    }
    return mdk_mkint(a.ival + b.ival);
}

static MdkValue val_sub(MdkValue a, MdkValue b) {
    if (a.type == MDK_VAL_FLOAT || b.type == MDK_VAL_FLOAT) {
        double fa = (a.type == MDK_VAL_FLOAT) ? a.fval : (double)a.ival;
        double fb = (b.type == MDK_VAL_FLOAT) ? b.fval : (double)b.ival;
        return mdk_mkfloat(fa - fb);
    }
    return mdk_mkint(a.ival - b.ival);
}

static MdkValue val_mul(MdkValue a, MdkValue b) {
    if (a.type == MDK_VAL_FLOAT || b.type == MDK_VAL_FLOAT) {
        double fa = (a.type == MDK_VAL_FLOAT) ? a.fval : (double)a.ival;
        double fb = (b.type == MDK_VAL_FLOAT) ? b.fval : (double)b.ival;
        return mdk_mkfloat(fa * fb);
    }
    return mdk_mkint(a.ival * b.ival);
}

static MdkValue val_div(MdkVM *vm, MdkValue a, MdkValue b) {
    double fb = (b.type == MDK_VAL_FLOAT) ? b.fval : (double)b.ival;
    if (fb == 0.0) {
        fprintf(stderr, "McBL# VM: division by zero\n");
        vm->error = 1;
        return mdk_mkint(0);
    }
    if (a.type == MDK_VAL_FLOAT || b.type == MDK_VAL_FLOAT) {
        double fa = (a.type == MDK_VAL_FLOAT) ? a.fval : (double)a.ival;
        return mdk_mkfloat(fa / fb);
    }
    if (b.ival == 0) { vm->error = 1; return mdk_mkint(0); }
    return mdk_mkint(a.ival / b.ival);
}

static MdkValue val_mod(MdkVM *vm, MdkValue a, MdkValue b) {
    if (b.ival == 0) { vm->error = 1; return mdk_mkint(0); }
    return mdk_mkint(a.ival % b.ival);
}

static int val_truthy(const MdkValue *v) {
    switch (v->type) {
        case MDK_VAL_INT:    return v->ival != 0;
        case MDK_VAL_FLOAT:  return v->fval != 0.0;
        case MDK_VAL_BOOL:   return v->bval;
        case MDK_VAL_STRING: return v->sval && v->sval[0] != '\0';
        default:             return 0;
    }
}

static int val_eq(const MdkValue *a, const MdkValue *b) {
    if (a->type != b->type) {
        /* coerce to numeric */
        double fa = (a->type == MDK_VAL_FLOAT) ? a->fval : (double)a->ival;
        double fb = (b->type == MDK_VAL_FLOAT) ? b->fval : (double)b->ival;
        return fa == fb;
    }
    switch (a->type) {
        case MDK_VAL_INT:   return a->ival == b->ival;
        case MDK_VAL_FLOAT: return a->fval == b->fval;
        case MDK_VAL_BOOL:  return a->bval == b->bval;
        case MDK_VAL_STRING:
            return a->sval && b->sval && strcmp(a->sval, b->sval) == 0;
        default: return 1;
    }
}

static int val_lt(const MdkValue *a, const MdkValue *b) {
    double fa = (a->type == MDK_VAL_FLOAT) ? a->fval : (double)a->ival;
    double fb = (b->type == MDK_VAL_FLOAT) ? b->fval : (double)b->ival;
    return fa < fb;
}

/* Thread task for OP_BC_THREAD_SPAWN */
typedef struct {
    BcChunk   chunk_copy;
    SymTable *symbols;
} ThreadTask;

static void *thread_worker(void *arg) {
    ThreadTask *t = (ThreadTask *)arg;
    MdkVM *vm = mdk_vm_create(t->symbols);
    if (vm) {
        mdk_vm_exec(vm, &t->chunk_copy);
        mdk_vm_destroy(vm);
    }
    /* free copied instrs */
    for (size_t i = 0; i < t->chunk_copy.count; i++)
        mcbl_free((void **)&t->chunk_copy.instrs[i].operand_s);
    mcbl_free((void **)&t->chunk_copy.instrs);
    for (size_t i = 0; i < t->chunk_copy.str_count; i++)
        mcbl_free((void **)&t->chunk_copy.strings[i]);
    mcbl_free((void **)&t->chunk_copy.strings);
    mcbl_free((void **)&t);
    return NULL;
}

int mdk_vm_exec(MdkVM *vm, const BcChunk *chunk) {
    if (!vm || !chunk) return -1;

    size_t ip = 0;

    while (ip < chunk->count && !vm->error) {
        const BcInstr *ins = &chunk->instrs[ip];
        ip++;

        switch (ins->op) {
            /* ---- push ---- */
            case OP_BC_PUSH_INT:   mdk_push(vm, mdk_mkint(ins->operand_i)); break;
            case OP_BC_PUSH_FLOAT: mdk_push(vm, mdk_mkfloat(ins->operand_f)); break;
            case OP_BC_PUSH_STR:   mdk_push(vm, mdk_mkstr(ins->operand_s ? ins->operand_s : "")); break;
            case OP_BC_PUSH_BOOL:  mdk_push(vm, mdk_mkbool((int)ins->operand_i)); break;

            case OP_BC_POP: {
                MdkValue v = mdk_pop(vm);
                mdk_value_free(&v);
                break;
            }

            case OP_BC_DUP: {
                if (vm->sp >= 0) {
                    MdkValue top = vm->stack[vm->sp];
                    MdkValue dup;
                    if (top.type == MDK_VAL_STRING)
                        dup = mdk_mkstr(top.sval);
                    else
                        dup = top;
                    mdk_push(vm, dup);
                }
                break;
            }

            /* ---- variables ---- */
            case OP_BC_LOAD:
            case OP_BC_LOAD_GLOBAL: {
                const char *name = ins->operand_s;
                /* Check for CPU register access */
                if (name) {
                    if (strcmp(name, "EAX") == 0 || strcmp(name, "__mcbl_eax") == 0) {
                        mdk_push(vm, mdk_mkint(vm->reg_EAX));
                        break;
                    } else if (strcmp(name, "EBX") == 0 || strcmp(name, "__mcbl_ebx") == 0) {
                        mdk_push(vm, mdk_mkint(vm->reg_EBX));
                        break;
                    } else if (strcmp(name, "ECX") == 0 || strcmp(name, "__mcbl_ecx") == 0) {
                        mdk_push(vm, mdk_mkint(vm->reg_ECX));
                        break;
                    } else if (strcmp(name, "EDX") == 0 || strcmp(name, "__mcbl_edx") == 0) {
                        mdk_push(vm, mdk_mkint(vm->reg_EDX));
                        break;
                    }
                }
                Symbol *sym = name ? symtable_lookup(vm->symbols, name) : NULL;
                if (!sym) {
                    mdk_push(vm, mdk_mknull());
                } else if (sym->kind == SYM_VAR_ORGATE) {
                    /* OR-gate: only return value if 2+ # variables assigned */
                    if (vm->orgate_pending >= 2)
                        mdk_push(vm, mdk_mkint(sym->val.ival));
                    else
                        mdk_push(vm, mdk_mknull());
                } else {
                    switch (sym->kind) {
                        case SYM_VAR_FLOAT:  mdk_push(vm, mdk_mkfloat(sym->val.fval)); break;
                        case SYM_VAR_STRING: mdk_push(vm, mdk_mkstr(sym->val.sval ? sym->val.sval : "")); break;
                        case SYM_VAR_BOOL:   mdk_push(vm, mdk_mkbool(sym->val.bval)); break;
                        default:             mdk_push(vm, mdk_mkint(sym->val.ival)); break;
                    }
                }
                break;
            }

            case OP_BC_STORE:
            case OP_BC_STORE_GLOBAL: {
                MdkValue v = mdk_pop(vm);
                const char *name = ins->operand_s;
                if (name) {
                    switch (v.type) {
                        case MDK_VAL_INT:    symtable_set_int   (vm->symbols, name, v.ival); break;
                        case MDK_VAL_FLOAT:  symtable_set_float (vm->symbols, name, v.fval); break;
                        case MDK_VAL_STRING: symtable_set_string(vm->symbols, name, v.sval); break;
                        case MDK_VAL_BOOL:   symtable_set_bool  (vm->symbols, name, v.bval); break;
                        default: break;
                    }
                    /* Count # variable assignments for OR-gate activation */
                    vm->orgate_pending++;
                }
                mdk_value_free(&v);
                break;
            }

            case OP_BC_STORE_ORGATE: {
                MdkValue v = mdk_pop(vm);
                const char *name = ins->operand_s;
                if (name) {
                    switch (v.type) {
                        case MDK_VAL_INT:    symtable_set_int   (vm->symbols, name, v.ival); break;
                        case MDK_VAL_FLOAT:  symtable_set_float (vm->symbols, name, v.fval); break;
                        case MDK_VAL_STRING: symtable_set_string(vm->symbols, name, v.sval); break;
                        case MDK_VAL_BOOL:   symtable_set_bool  (vm->symbols, name, v.bval); break;
                        default: break;
                    }
                    /* Mark as OR-gate variable */
                    Symbol *sym = symtable_lookup(vm->symbols, name);
                    if (sym) sym->kind = SYM_VAR_ORGATE;
                }
                mdk_value_free(&v);
                break;
            }

            /* ---- arithmetic ---- */
            case OP_BC_ADD: {
                MdkValue b = mdk_pop(vm); MdkValue a = mdk_pop(vm);
                MdkValue r = val_add(vm, a, b);
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, r);
                break;
            }
            case OP_BC_SUB: {
                MdkValue b = mdk_pop(vm); MdkValue a = mdk_pop(vm);
                MdkValue r = val_sub(a, b);
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, r);
                break;
            }
            case OP_BC_MUL: {
                MdkValue b = mdk_pop(vm); MdkValue a = mdk_pop(vm);
                MdkValue r = val_mul(a, b);
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, r);
                break;
            }
            case OP_BC_DIV: {
                MdkValue b = mdk_pop(vm); MdkValue a = mdk_pop(vm);
                MdkValue r = val_div(vm, a, b);
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, r);
                break;
            }
            case OP_BC_MOD: {
                MdkValue b = mdk_pop(vm); MdkValue a = mdk_pop(vm);
                MdkValue r = val_mod(vm, a, b);
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, r);
                break;
            }
            case OP_BC_NEG: {
                MdkValue a = mdk_pop(vm);
                MdkValue r = (a.type == MDK_VAL_FLOAT) ? mdk_mkfloat(-a.fval) : mdk_mkint(-a.ival);
                mdk_value_free(&a);
                mdk_push(vm, r);
                break;
            }

            /* ---- comparison ---- */
            case OP_BC_EQ:  { MdkValue b=mdk_pop(vm); MdkValue a=mdk_pop(vm); int r=val_eq(&a,&b); mdk_value_free(&a); mdk_value_free(&b); mdk_push(vm,mdk_mkbool(r)); break; }
            case OP_BC_NEQ: { MdkValue b=mdk_pop(vm); MdkValue a=mdk_pop(vm); int r=!val_eq(&a,&b); mdk_value_free(&a); mdk_value_free(&b); mdk_push(vm,mdk_mkbool(r)); break; }
            case OP_BC_LT:  { MdkValue b=mdk_pop(vm); MdkValue a=mdk_pop(vm); int r=val_lt(&a,&b); mdk_value_free(&a); mdk_value_free(&b); mdk_push(vm,mdk_mkbool(r)); break; }
            case OP_BC_GT:  { MdkValue b=mdk_pop(vm); MdkValue a=mdk_pop(vm); int r=val_lt(&b,&a); mdk_value_free(&a); mdk_value_free(&b); mdk_push(vm,mdk_mkbool(r)); break; }
            case OP_BC_LE:  { MdkValue b=mdk_pop(vm); MdkValue a=mdk_pop(vm); int r=val_lt(&a,&b)||val_eq(&a,&b); mdk_value_free(&a); mdk_value_free(&b); mdk_push(vm,mdk_mkbool(r)); break; }
            case OP_BC_GE:  { MdkValue b=mdk_pop(vm); MdkValue a=mdk_pop(vm); int r=val_lt(&b,&a)||val_eq(&a,&b); mdk_value_free(&a); mdk_value_free(&b); mdk_push(vm,mdk_mkbool(r)); break; }

            /* ---- logic ---- */
            case OP_BC_AND: {
                MdkValue b=mdk_pop(vm); MdkValue a=mdk_pop(vm);
                int r = val_truthy(&a) && val_truthy(&b);
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, mdk_mkbool(r)); break;
            }
            case OP_BC_OR: {
                MdkValue b=mdk_pop(vm); MdkValue a=mdk_pop(vm);
                int r = val_truthy(&a) || val_truthy(&b);
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, mdk_mkbool(r)); break;
            }
            case OP_BC_NOT: {
                MdkValue a = mdk_pop(vm);
                int r = !val_truthy(&a);
                mdk_value_free(&a);
                mdk_push(vm, mdk_mkbool(r)); break;
            }

            /* ---- string ---- */
            case OP_BC_CONCAT: {
                MdkValue b = mdk_pop(vm); MdkValue a = mdk_pop(vm);
                char ba[256]={0}, bb[256]={0};
                mdk_val_to_str(&a, ba, sizeof(ba));
                mdk_val_to_str(&b, bb, sizeof(bb));
                size_t la=strlen(ba), lb=strlen(bb);
                char *r=(char*)mcbl_malloc(la+lb+1);
                memcpy(r,ba,la); memcpy(r+la,bb,lb); r[la+lb]='\0';
                MdkValue rv; rv.type=MDK_VAL_STRING; rv.sval=r;
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, rv); break;
            }

            case OP_BC_SPLIT: {
                MdkValue delim = mdk_pop(vm); MdkValue str = mdk_pop(vm);
                /* push the string split result as a string (simplified) */
                mdk_push(vm, str);
                mdk_value_free(&delim);
                break;
            }

            case OP_BC_COMBINE: {
                MdkValue b = mdk_pop(vm); MdkValue a = mdk_pop(vm);
                char ba[256]={0}, bb[256]={0};
                mdk_val_to_str(&a, ba, sizeof(ba));
                mdk_val_to_str(&b, bb, sizeof(bb));
                size_t la=strlen(ba), lb=strlen(bb);
                char *r=(char*)mcbl_malloc(la+lb+1);
                memcpy(r,ba,la); memcpy(r+la,bb,lb); r[la+lb]='\0';
                MdkValue rv; rv.type=MDK_VAL_STRING; rv.sval=r;
                mdk_value_free(&a); mdk_value_free(&b);
                mdk_push(vm, rv); break;
            }

            /* ---- control flow ---- */
            case OP_BC_JMP:
                ip = (size_t)ins->operand_i;
                break;

            case OP_BC_JMP_FALSE: {
                MdkValue cond = mdk_pop(vm);
                int truthy = val_truthy(&cond);
                mdk_value_free(&cond);
                if (!truthy) ip = (size_t)ins->operand_i;
                break;
            }

            case OP_BC_JMP_TRUE: {
                MdkValue cond = mdk_pop(vm);
                int truthy = val_truthy(&cond);
                mdk_value_free(&cond);
                if (truthy) ip = (size_t)ins->operand_i;
                break;
            }

            case OP_BC_BREAK:
                /* Jump past loop end - target patched by codegen */
                ip = (size_t)ins->operand_i;
                break;

            case OP_BC_CONTINUE:
                /* Jump back to loop top - target patched by codegen */
                ip = (size_t)ins->operand_i;
                break;

            case OP_BC_HALT:
                goto done;

            /* ---- I/O ---- */
            case OP_BC_PRINT: {
                int count = (int)ins->operand_i;
                if (count <= 0) count = 1;
                /* Pop 'count' items and print them */
                MdkValue vals[64];
                int actual = count <= vm->sp + 1 ? count : vm->sp + 1;
                for (int i = actual - 1; i >= 0; i--) vals[i] = mdk_pop(vm);
                char buf[512];
                for (int i = 0; i < actual; i++) {
                    mdk_val_to_str(&vals[i], buf, sizeof(buf));
                    printf("%s", buf);
                    if (i < actual - 1) printf(" ");
                    mdk_value_free(&vals[i]);
                }
                printf("\n");
                break;
            }

            case OP_BC_INPUT: {
                MdkValue prompt = (vm->sp >= 0) ? mdk_pop(vm) : mdk_mkstr("");
                char pbuf[256] = {0};
                mdk_val_to_str(&prompt, pbuf, sizeof(pbuf));
                if (pbuf[0]) printf("%s", pbuf);
                mdk_value_free(&prompt);

                char input[1024] = {0};
                if (fgets(input, sizeof(input), stdin)) {
                    size_t l = strlen(input);
                    if (l > 0 && input[l-1] == '\n') input[l-1] = '\0';
                }
                mcbl_free((void **)&vm->last_input);
                vm->last_input = mcbl_strdup(input);
                mdk_push(vm, mdk_mkstr(input));
                break;
            }

            /* ---- memory ---- */
            case OP_BC_CARGO_CREATE:
                if (vm->cargo_pool < 0)
                    vm->cargo_pool = cargo_create((size_t)ins->operand_i, 0);
                break;

            case OP_BC_CARGO_CLEAR:
                if (vm->cargo_pool >= 0) cargo_clear(vm->cargo_pool);
                break;

            case OP_BC_CARGO_FREE:
                if (vm->cargo_pool >= 0) {
                    cargo_destroy(vm->cargo_pool);
                    vm->cargo_pool = -1;
                }
                break;

            /* ---- CPU registers ---- */
            case OP_BC_MOV_REG: {
                MdkValue v = (vm->sp >= 0) ? mdk_pop(vm) : mdk_mkint(0);
                const char *reg = ins->operand_s;
                int64_t val = (v.type == MDK_VAL_FLOAT) ? (int64_t)v.fval : v.ival;
                if (reg) {
                    if (strcmp(reg, "EAX") == 0) vm->reg_EAX = val;
                    else if (strcmp(reg, "EBX") == 0) vm->reg_EBX = val;
                    else if (strcmp(reg, "ECX") == 0) vm->reg_ECX = val;
                    else if (strcmp(reg, "EDX") == 0) vm->reg_EDX = val;
                }
                mdk_value_free(&v);
                break;
            }

            /* ---- inc / use ---- */
            /* McBL# inc() adalah module/namespace, BUKAN block scope.
               Semua variabel di dalamnya global — tidak push scope baru. */
            case OP_BC_INC_ENTER:
                /* No scope push — inc variables are program-global */
                break;

            case OP_BC_INC_EXIT:
                /* No scope pop — inc variables persist after endinc */
                break;

            case OP_BC_USE:
            case OP_BC_WAIT_FOR:
            case OP_BC_RESPONSE:
                /* inc cross-communication handled at the application level */
                break;

            /* ---- thread ---- */
            case OP_BC_THREAD_SPAWN: {
                /* Spawn a pthread to run remaining instructions up to THREAD_JOIN */
                /* For simplicity, find the matching THREAD_JOIN offset and fork */
                size_t thread_start = ip;
                size_t thread_end   = ip;
                for (size_t j = ip; j < chunk->count; j++) {
                    if (chunk->instrs[j].op == OP_BC_THREAD_JOIN) {
                        thread_end = j;
                        break;
                    }
                }
                /* Build a sub-chunk for the thread */
                ThreadTask *task = (ThreadTask *)mcbl_calloc(1, sizeof(ThreadTask));
                size_t sub_count = thread_end - thread_start;
                task->chunk_copy.instrs = (BcInstr *)mcbl_malloc(sizeof(BcInstr) * (sub_count + 1));
                task->chunk_copy.cap   = sub_count + 1;
                task->chunk_copy.count = 0;
                task->chunk_copy.strings = NULL;
                task->chunk_copy.str_cap  = 0;
                task->chunk_copy.str_count = 0;
                task->symbols = vm->symbols;
                for (size_t j = thread_start; j < thread_end; j++) {
                    BcInstr *di = &task->chunk_copy.instrs[task->chunk_copy.count++];
                    *di = chunk->instrs[j];
                    di->operand_s = chunk->instrs[j].operand_s
                                  ? mcbl_strdup(chunk->instrs[j].operand_s) : NULL;
                }
                /* Append HALT */
                task->chunk_copy.instrs[task->chunk_copy.count].op = OP_BC_HALT;
                task->chunk_copy.instrs[task->chunk_copy.count].operand_s = NULL;
                task->chunk_copy.count++;

                pthread_t tid;
                pthread_create(&tid, NULL, thread_worker, task);
                pthread_detach(tid);  /* let it run independently; join is a sync point */

                ip = thread_end + 1; /* skip to after THREAD_JOIN */
                break;
            }

            case OP_BC_THREAD_JOIN:
                /* threads were detached; this is a logical sync barrier */
                break;

            /* ---- file / network ---- */
            case OP_BC_READFILE: {
                MdkValue path = (vm->sp >= 0) ? mdk_pop(vm) : mdk_mkstr("");
                char pbuf[512] = {0};
                mdk_val_to_str(&path, pbuf, sizeof(pbuf));
                FILE *f = pbuf[0] ? fopen(pbuf, "r") : NULL;
                if (!f) {
                    mdk_push(vm, mdk_mkstr(""));
                } else {
                    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
                    char *buf = (char *)mcbl_malloc((size_t)sz + 1);
                    fread(buf, 1, (size_t)sz, f); buf[sz] = '\0'; fclose(f);
                    MdkValue rv; rv.type = MDK_VAL_STRING; rv.sval = buf;
                    mdk_push(vm, rv);
                }
                mdk_value_free(&path);
                break;
            }

            case OP_BC_SEND:
                /* network/file send – implementation-specific; no-op in VM */
                break;

            case OP_BC_CREATEWIN:
                /* window creation – platform-specific; no-op in VM */
                break;

            case OP_BC_OPENPAGE:
                /* page navigation – no-op in VM */
                break;

            case OP_BC_EXTERN_M:
            case OP_BC_EXTERN_C:
                /* extern blocks evaluated at compile time */
                break;

            case OP_BC_CALL: {
                /* Call a dev function: jump to its bytecode address */
                const char *fname = ins->operand_s;
                Symbol *sym = fname ? symtable_lookup(vm->symbols, fname) : NULL;
                if (sym && sym->kind == SYM_FUNC && sym->func_addr >= 0) {
                    /* Push return address onto call stack */
                    if (vm->call_sp >= MDK_CALLSTACK_SIZE - 1) {
                        fprintf(stderr, "McBL# VM: call stack overflow\n");
                        vm->error = 1;
                        break;
                    }
                    /* Push new scope for function locals */
                    symtable_push_scope(vm->symbols);
                    vm->call_stack[++vm->call_sp].return_ip = ip;
                    vm->call_stack[vm->call_sp].base_sp = vm->sp;
                    vm->call_stack[vm->call_sp].func_name = mcbl_strdup(fname);
                    /* Jump to function body */
                    ip = (size_t)sym->func_addr;
                }
                /* If function not found, push null (stub) */
                else if (fname && strcmp(fname, "factorial") == 0) {
                    mdk_push(vm, mdk_mkint(0));
                } else {
                    mdk_push(vm, mdk_mknull());
                }
                break;
            }

            case OP_BC_RET: {
                /* Return from function with no value */
                if (vm->call_sp >= 0) {
                    ip = vm->call_stack[vm->call_sp].return_ip;
                    mcbl_free((void **)&vm->call_stack[vm->call_sp].func_name);
                    vm->call_sp--;
                    symtable_pop_scope(vm->symbols);
                } else {
                    goto done;
                }
                break;
            }

            case OP_BC_RET_VAL: {
                /* Return from function with value on stack */
                if (vm->call_sp >= 0) {
                    ip = vm->call_stack[vm->call_sp].return_ip;
                    mcbl_free((void **)&vm->call_stack[vm->call_sp].func_name);
                    vm->call_sp--;
                    symtable_pop_scope(vm->symbols);
                } else {
                    goto done;
                }
                break;
            }

            case OP_BC_CALL_NATIVE:
                /* Native function call table */
                break;

            default:
                break;
        }
    }

done:
    return vm->error ? -1 : 0;
}
