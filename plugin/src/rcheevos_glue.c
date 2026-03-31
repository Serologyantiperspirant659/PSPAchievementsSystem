#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <string.h>
#include "rcheevos_glue.h"
#include "memory.h"

#define RC_LOG_PATH "ms0:/PSP/ACH/pach_log.txt"
static void rc_log(const char *msg) {
    SceUID fd = sceIoOpen(RC_LOG_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, msg, strlen(msg)); sceIoWrite(fd, "\n", 1); sceIoClose(fd); }
}
static void rc_log_int(const char *label, int val) {
    char buf[48]; char tmp[12]; int i=0, j, tlen=0; int v=val;
    while (*label) buf[i++] = *label++;
    buf[i++] = '=';
    if (v < 0) { buf[i++] = '-'; v = -v; }
    if (v == 0) { tmp[tlen++] = '0'; }
    else { while (v > 0) { tmp[tlen++] = '0' + (v % 10); v /= 10; } }
    for (j = tlen - 1; j >= 0; j--) buf[i++] = tmp[j];
    buf[i] = '\0';
    rc_log(buf);
}

/* ================================================================== */
/* STRING HELPERS                                                      */
/* ================================================================== */

static int str_starts(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

static float pach_strtof(const char *s, const char **endptr)
{
    float result   = 0.0f;
    float fraction = 1.0f;
    int   sign     = 1;
    if      (*s == '-') { sign = -1; s++; }
    else if (*s == '+') {             s++; }
    while (*s >= '0' && *s <= '9') { result = result * 10.0f + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            fraction *= 0.1f;
            result   += (*s - '0') * fraction;
            s++;
        }
    }
    if (endptr) *endptr = s;
    return result * (float)sign;
}

static unsigned int pach_strtoul(const char *s, const char **endptr, int base)
{
    unsigned int res = 0;
    if (base == 16) {
        while (*s) {
            if      (*s >= '0' && *s <= '9') res = res * 16 + (unsigned int)(*s - '0');
            else if (*s >= 'a' && *s <= 'f') res = res * 16 + (unsigned int)(*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') res = res * 16 + (unsigned int)(*s - 'A' + 10);
            else break;
            s++;
        }
    } else {
        while (*s >= '0' && *s <= '9') { res = res * 10 + (unsigned int)(*s - '0'); s++; }
    }
    if (endptr) *endptr = s;
    return res;
}

/* ================================================================== */
/* OPERAND PARSER                                                      */
/* ================================================================== */

static int parse_operand(const char *s, RC_Operand *op, const char **end)
{
    const char *p = s;
    memset(op, 0, sizeof(*op));
    if      (*p == 'd') { op->is_delta = 1; p++; }
    else if (*p == 'p') { op->is_prior = 1; p++; }

    if (str_starts(p, "0xX")) {
        op->size    = RC_MEMSIZE_32BIT;
        op->address = pach_strtoul(p + 3, &p, 16);
        if (*p == '&') { p++; op->mask = pach_strtoul(p, &p, 10); op->has_mask = 1; }
        if (end) *end = p;
        return 1;
    }
    if (str_starts(p, "0xH")) { op->size = RC_MEMSIZE_8BIT;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xW")) { op->size = RC_MEMSIZE_24BIT;    op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xM")) { op->size = RC_MEMSIZE_BIT0;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xN")) { op->size = RC_MEMSIZE_BIT1;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xO")) { op->size = RC_MEMSIZE_BIT2;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xP")) { op->size = RC_MEMSIZE_BIT3;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xQ")) { op->size = RC_MEMSIZE_BIT4;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xR")) { op->size = RC_MEMSIZE_BIT5;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xS")) { op->size = RC_MEMSIZE_BIT6;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0xT")) { op->size = RC_MEMSIZE_BIT7;     op->address = pach_strtoul(p+3, end, 16); return 1; }
    if (str_starts(p, "0x"))  {
        /* RA format: "0x " (with space) = 16-bit read. Skip the space. */
        const char *a = p + 2;
        while (*a == ' ') a++;
        op->size = RC_MEMSIZE_16BIT;
        op->address = pach_strtoul(a, end, 16);
        return 1;
    }
    if (str_starts(p, "fF"))  { op->size = RC_MEMSIZE_FLOAT;    op->address = pach_strtoul(p+2, end, 16); return 1; }
    if (str_starts(p, "fB"))  { op->size = RC_MEMSIZE_FLOAT_BE; op->address = pach_strtoul(p+2, end, 16); return 1; }
    if (*p == 'f' && !op->is_delta && !op->is_prior) {
        op->size = RC_MEMSIZE_CONST_FLOAT; op->const_float = pach_strtof(p+1, end); return 1;
    }
    op->size = RC_MEMSIZE_CONST_UINT;
    op->const_uint = pach_strtoul(p, end, 10);
    return 1;
}

/* ================================================================== */
/* OPERATOR / PREFIX PARSERS                                           */
/* ================================================================== */

static RC_CompOp parse_comp_op(const char **ps)
{
    const char *s = *ps;
    if (s[0]=='>'&&s[1]=='=') { *ps=s+2; return RC_OP_GE; }
    if (s[0]=='<'&&s[1]=='=') { *ps=s+2; return RC_OP_LE; }
    if (s[0]=='!'&&s[1]=='=') { *ps=s+2; return RC_OP_NE; }
    if (s[0]=='=')             { *ps=s+1; return RC_OP_EQ; }
    if (s[0]=='>')             { *ps=s+1; return RC_OP_GT; }
    if (s[0]=='<')             { *ps=s+1; return RC_OP_LT; }
    return RC_OP_NONE;
}

static RC_CondType parse_cond_prefix(const char **ps)
{
    const char *s = *ps;
    if (s[0] && s[1] == ':') {
        RC_CondType type = RC_COND_STANDARD;
        switch (s[0]) {
            case 'R': type = RC_COND_RESET_IF;      break;
            case 'P': type = RC_COND_PAUSE_IF;      break;
            case 'N': type = RC_COND_AND_NEXT;      break;
            case 'O': type = RC_COND_OR_NEXT;       break;
            case 'T': type = RC_COND_TRIGGER;       break;
            case 'M': type = RC_COND_MEASURED;      break;
            case 'Q': type = RC_COND_MEASURED_IF;   break;
            case 'A': type = RC_COND_ADD_SOURCE;    break;
            case 'I': type = RC_COND_ADD_ADDRESS;   break;
            case 'Z': type = RC_COND_RESET_NEXT_IF; break;
            case 'B': type = RC_COND_SUB_SOURCE;    break;
            default:  return RC_COND_STANDARD;
        }
        *ps = s + 2;
        return type;
    }
    return RC_COND_STANDARD;
}

static const char *find_comp_op(const char *s)
{
    while (*s) {
        if (*s=='='||*s=='>'||*s=='<') return s;
        if (*s=='!'&&s[1]=='=')        return s;
        s++;
    }
    return NULL;
}

/* ================================================================== */
/* CONDITION PARSER                                                    */
/* ================================================================== */

static int parse_single_condition(const char *s, int len, RC_Condition *out)
{
    char        buf[512];
    const char *p, *op_pos, *after_op, *end_ptr;
    RC_CondType ctype;
    RC_CompOp   cop;

    memset(out, 0, sizeof(*out));
    if (len <= 0 || len >= (int)sizeof(buf)) return 0;
    memcpy(buf, s, (size_t)len);
    buf[len] = '\0';
    p = buf;

    ctype = parse_cond_prefix(&p);
    out->type = (unsigned char)ctype;

    if (ctype == RC_COND_ADD_SOURCE  ||
        ctype == RC_COND_SUB_SOURCE  ||
        ctype == RC_COND_ADD_ADDRESS) {
        if (!parse_operand(p, &out->left, &end_ptr)) return 0;
        out->op = (unsigned char)RC_OP_NONE;
        return 1;
    }

    op_pos = find_comp_op(p);
    if (!op_pos) return 0;

    {
        char leftbuf[256];
        int  leftlen = (int)(op_pos - p);
        if (leftlen <= 0 || leftlen >= (int)sizeof(leftbuf)) return 0;
        memcpy(leftbuf, p, (size_t)leftlen);
        leftbuf[leftlen] = '\0';
        if (!parse_operand(leftbuf, &out->left, &end_ptr)) return 0;
    }

    after_op = op_pos;
    cop      = parse_comp_op(&after_op);
    out->op  = (unsigned char)cop;
    if (cop == RC_OP_NONE) return 0;

    {
        char rightbuf[256];
        int  rlen = (int)strlen(after_op);
        if (rlen <= 0 || rlen >= (int)sizeof(rightbuf)) return 0;
        memcpy(rightbuf, after_op, (size_t)rlen);
        rightbuf[rlen] = '\0';
        out->required_hits = 0;

        {
            char *last_dot = NULL, *second_dot = NULL;
            int   i;
            for (i=rlen-1; i>=0; i--) { if (rightbuf[i]=='.') { last_dot=&rightbuf[i]; break; } }
            if (last_dot && last_dot == &rightbuf[rlen-1]) {
                for (i=(int)(last_dot-rightbuf)-1; i>=0; i--) { if (rightbuf[i]=='.') { second_dot=&rightbuf[i]; break; } }
                if (second_dot) {
                    int all_digits=1; char *dp;
                    for (dp=second_dot+1; dp<last_dot; dp++) { if (*dp<'0'||*dp>'9') { all_digits=0; break; } }
                    if (all_digits && (last_dot-second_dot)>1) {
                        out->required_hits = pach_strtoul(second_dot+1, NULL, 10);
                        *second_dot = '\0';
                    }
                }
            }
        }
        if (!parse_operand(rightbuf, &out->right, &end_ptr)) return 0;
    }
    out->current_hits = 0;
    return 1;
}

/* ================================================================== */
/* LOGIC STRING PARSER                                                 */
/* ================================================================== */

int rc_glue_parse(const char *logic, RC_ParsedAchievement *out)
{
    const char *p;
    int         group_idx;
    memset(out, 0, sizeof(*out));
    out->is_active = 1;
    if (!logic || !logic[0]) return 0;
    p = logic; group_idx = 0;

    while (*p && group_idx < RC_MAX_GROUPS) {
        RC_CondGroup *grp = &out->groups[group_idx];
        grp->count = 0;
        while (*p) {
            const char *start = p;
            int len = 0;
            while (*p && *p != '_' && *p != 'S') { p++; len++; }
            if (len > 0 && grp->count < RC_MAX_CONDITIONS) {
                if (parse_single_condition(start, len, &grp->conds[grp->count]))
                    grp->count++;
            }
            if      (*p == '_') { p++; }
            else if (*p == 'S') { p++; break; }
            else                  break;
        }
        if (grp->count > 0) group_idx++;
    }
    out->num_groups = (unsigned char)group_idx;
    return (group_idx > 0) ? 1 : 0;
}

int rc_glue_parse_all(PACH_LoadedGame *game,
                      RC_ParsedAchievement *out_array, int max_count)
{
    int i, count = 0;
    if (!game || !game->loaded || !out_array) return 0;
    for (i = 0; i < game->header.num_achievements && i < max_count; i++) {
        if (rc_glue_parse(game->achievements[i].ra_logic, &out_array[i]))
            count++;
        else
            memset(&out_array[i], 0, sizeof(RC_ParsedAchievement));
    }
    return count;
}

/* ================================================================== */
/* DELTA / PRIOR SNAPSHOT TABLE                                        */
/* ================================================================== */

static RC_DeltaSlot *find_or_create_slot(RC_RuntimeState *state,
                                          unsigned int addr, RC_MemSize size)
{
    int i;
    for (i = 0; i < state->num_slots; i++)
        if (state->slots[i].addr == addr && state->slots[i].size == size)
            return &state->slots[i];
    if (state->num_slots < RC_MAX_DELTA_SLOTS) {
        RC_DeltaSlot *slot = &state->slots[state->num_slots++];
        memset(slot, 0, sizeof(*slot));
        slot->addr = addr; slot->size = size;
        return slot;
    }
    return NULL;
}

static unsigned int read_mem_value(unsigned int addr, RC_MemSize size)
{
    switch (size) {
        case RC_MEMSIZE_8BIT:     return (unsigned int)pach_mem_read8(addr);
        case RC_MEMSIZE_16BIT:    return (unsigned int)pach_mem_read16(addr);
        case RC_MEMSIZE_24BIT:    return pach_mem_read24(addr);
        case RC_MEMSIZE_32BIT:    return pach_mem_read32(addr);
        case RC_MEMSIZE_BIT0:     return (unsigned int)(pach_mem_read8(addr)      & 1);
        case RC_MEMSIZE_BIT1:     return (unsigned int)((pach_mem_read8(addr)>>1) & 1);
        case RC_MEMSIZE_BIT2:     return (unsigned int)((pach_mem_read8(addr)>>2) & 1);
        case RC_MEMSIZE_BIT3:     return (unsigned int)((pach_mem_read8(addr)>>3) & 1);
        case RC_MEMSIZE_BIT4:     return (unsigned int)((pach_mem_read8(addr)>>4) & 1);
        case RC_MEMSIZE_BIT5:     return (unsigned int)((pach_mem_read8(addr)>>5) & 1);
        case RC_MEMSIZE_BIT6:     return (unsigned int)((pach_mem_read8(addr)>>6) & 1);
        case RC_MEMSIZE_BIT7:     return (unsigned int)((pach_mem_read8(addr)>>7) & 1);
        case RC_MEMSIZE_FLOAT:    return pach_mem_read32(addr); /* LE float raw bits */
        case RC_MEMSIZE_FLOAT_BE: return pach_mem_read32(addr); /* BE float raw bits */
        default:                  return 0;
    }
}

/* Pre-register delta/prior slots so they have valid initial values */
static void register_operand(RC_Operand *op, RC_RuntimeState *state)
{
    RC_MemSize size = (RC_MemSize)op->size;
    if (size == RC_MEMSIZE_CONST_UINT || size == RC_MEMSIZE_CONST_FLOAT)
        return;
    RC_DeltaSlot *slot = find_or_create_slot(state, op->address, size);
    if (slot) {
        unsigned int v = read_mem_value(op->address, size);
        slot->current = v;
        slot->delta = v;
        slot->prior = v;
    }
}

static void register_achievement_memrefs(RC_ParsedAchievement *ach,
                                          RC_RuntimeState *state)
{
    int g, c;
    for (g = 0; g < (int)ach->num_groups; g++) {
        RC_CondGroup *grp = &ach->groups[g];
        for (c = 0; c < (int)grp->count; c++) {
            register_operand(&grp->conds[c].left, state);
            register_operand(&grp->conds[c].right, state);
        }
    }
}

static unsigned int resolve_operand(RC_Operand *op, RC_RuntimeState *state,
                                     unsigned int indirect_offset)
{
    unsigned int  addr;
    RC_DeltaSlot *slot;
    unsigned int  val;
    RC_MemSize    size = (RC_MemSize)op->size;

    if (size == RC_MEMSIZE_CONST_UINT)  return op->const_uint;
    if (size == RC_MEMSIZE_CONST_FLOAT) {
        unsigned int bits;
        memcpy(&bits, &op->const_float, 4);
        return bits;
    }

    addr = op->address + indirect_offset;

    if (op->is_delta || op->is_prior) {
        slot = find_or_create_slot(state, addr, size);
        if (!slot) return 0;
        val = op->is_delta ? slot->delta : slot->prior;
    } else {
        val = read_mem_value(addr, size);
    }

    if (op->has_mask)
        val = val & op->mask;

    return val;
}

static int is_float_operand(RC_Operand *op) {
    RC_MemSize s = (RC_MemSize)op->size;
    return (s == RC_MEMSIZE_FLOAT || s == RC_MEMSIZE_FLOAT_BE || s == RC_MEMSIZE_CONST_FLOAT);
}

static unsigned int bswap32(unsigned int v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v >> 24) & 0xFF);
}

static float operand_to_float(RC_Operand *op, unsigned int bits) {
    RC_MemSize s = (RC_MemSize)op->size;
    if (s == RC_MEMSIZE_FLOAT) {
        float f; memcpy(&f, &bits, 4); return f;  /* LE: native, no swap */
    }
    if (s == RC_MEMSIZE_FLOAT_BE) {
        unsigned int swapped = bswap32(bits);
        float f; memcpy(&f, &swapped, 4); return f;  /* BE: swap first */
    }
    if (s == RC_MEMSIZE_CONST_FLOAT) {
        float f; memcpy(&f, &bits, 4); return f;
    }
    return (float)bits;
}

static float bits_to_float(unsigned int bits) {
    float f; memcpy(&f, &bits, 4); return f;
}

static int compare_floats(float l, RC_CompOp op, float r)
{
    switch (op) {
        case RC_OP_EQ: { float d=l-r; if(d<0.0f)d=-d; return d<0.001f; }
        case RC_OP_NE: { float d=l-r; if(d<0.0f)d=-d; return d>=0.001f; }
        case RC_OP_GT: return l>r;
        case RC_OP_LT: return l<r;
        case RC_OP_GE: return l>=r;
        case RC_OP_LE: return l<=r;
        default: return 0;
    }
}

static int compare_uint(unsigned int l, RC_CompOp op, unsigned int r)
{
    switch (op) {
        case RC_OP_EQ: return l == r;
        case RC_OP_NE: return l != r;
        case RC_OP_GT: return l > r;
        case RC_OP_LT: return l < r;
        case RC_OP_GE: return l >= r;
        case RC_OP_LE: return l <= r;
        default: return 0;
    }
}

static int compare_values(RC_Operand *left, unsigned int lv,
                           RC_CompOp op, RC_Operand *right, unsigned int rv)
{
    if (is_float_operand(left) || is_float_operand(right)) {
        float lf = operand_to_float(left, lv);
        float rf = operand_to_float(right, rv);
        return compare_floats(lf, op, rf);
    }
    return compare_uint(lv, op, rv);
}

static void update_delta_snapshots(RC_RuntimeState *state)
{
    int i;
    for (i = 0; i < state->num_slots; i++) {
        RC_DeltaSlot *slot = &state->slots[i];
        unsigned int current = read_mem_value(slot->addr, slot->size);
        slot->delta = slot->current;
        if (current != slot->current) slot->prior = slot->current;
        slot->current = current;
    }
}

/* ================================================================== */
/* GROUP / ACHIEVEMENT EVALUATOR                                       */
/* ================================================================== */

static int evaluate_group(RC_CondGroup *grp, RC_RuntimeState *state,
                           int *out_reset, int *out_paused)
{
    int          i;
    int          group_result    = 1;
    int          paused          = 0;
    int          has_trigger     = 0, trigger_hit     = 0;
    unsigned int add_source      = 0;
    unsigned int add_address     = 0;
    int          and_next_active = 0, and_next_result = 1;
    int          or_next_active  = 0, or_next_result  = 0;
    int          reset_next      = 0;

    *out_reset = 0; *out_paused = 0;

    /* First pass: PAUSE_IF only */
    for (i = 0; i < (int)grp->count; i++) {
        RC_Condition *cond = &grp->conds[i];
        if ((RC_CondType)cond->type == RC_COND_PAUSE_IF) {
            unsigned int lv = resolve_operand(&cond->left,  state, 0);
            unsigned int rv = resolve_operand(&cond->right, state, 0);
            if (compare_values(&cond->left, lv, (RC_CompOp)cond->op, &cond->right, rv)) {
                if (cond->required_hits > 0) {
                    cond->current_hits++;
                    if (cond->current_hits >= cond->required_hits) paused = 1;
                } else { paused = 1; }
            }
        }
    }
    if (paused) { *out_paused = 1; return 0; }

    add_source = 0; add_address = 0;

    /* Second pass: all other conditions */
    for (i = 0; i < (int)grp->count; i++) {
        RC_Condition *cond     = &grp->conds[i];
        RC_CondType   ctype    = (RC_CondType)cond->type;
        RC_CompOp     cop      = (RC_CompOp)cond->op;
        unsigned int lv, rv;
        int   cmp_result;

        if (ctype == RC_COND_PAUSE_IF) continue;

        lv  = resolve_operand(&cond->left,  state, add_address);
        lv += add_source;
        rv  = resolve_operand(&cond->right, state, add_address);

        if (ctype != RC_COND_ADD_SOURCE &&
            ctype != RC_COND_SUB_SOURCE &&
            ctype != RC_COND_ADD_ADDRESS) {
            add_source = 0; add_address = 0;
        }

        switch (ctype) {
            case RC_COND_ADD_SOURCE:
                add_source = lv; continue;
            case RC_COND_SUB_SOURCE:
                add_source = (unsigned int)(-(int)lv); continue;
            case RC_COND_ADD_ADDRESS:
                add_address = lv;
                continue;
            default: break;
        }

        cmp_result = compare_values(&cond->left, lv, cop, &cond->right, rv);

        if (ctype == RC_COND_RESET_NEXT_IF) { reset_next = cmp_result; continue; }
        if (reset_next) { cond->current_hits = 0; reset_next = 0; }

        if (cond->required_hits > 0) {
            if (cmp_result && cond->current_hits < cond->required_hits)
                cond->current_hits++;
            cmp_result = (cond->current_hits >= cond->required_hits) ? 1 : 0;
        }

        if (ctype == RC_COND_RESET_IF) {
            if (cmp_result) { *out_reset = 1; return 0; }
            continue;
        }
        if (ctype == RC_COND_TRIGGER) {
            has_trigger = 1;
            if (cmp_result) trigger_hit  = 1;
            else            group_result = 0;
            continue;
        }
        if (ctype == RC_COND_MEASURED_IF || ctype == RC_COND_MEASURED) {
            if (!cmp_result) group_result = 0;
            continue;
        }
        if (ctype == RC_COND_AND_NEXT) {
            if (and_next_active) and_next_result = and_next_result && cmp_result;
            else { and_next_active = 1; and_next_result = cmp_result; }
            continue;
        }
        if (ctype == RC_COND_OR_NEXT) {
            if (or_next_active) or_next_result = or_next_result || cmp_result;
            else { or_next_active = 1; or_next_result = cmp_result; }
            continue;
        }

        {
            int final_result = cmp_result;
            if (and_next_active) { final_result = and_next_result && cmp_result; and_next_active=0; and_next_result=1; }
            if (or_next_active)  { final_result = or_next_result  || cmp_result; or_next_active=0;  or_next_result=0;  }
            if (!final_result) group_result = 0;
        }
    }

    if (has_trigger && !trigger_hit) group_result = 0;
    return group_result;
}

static void reset_all_hits(RC_ParsedAchievement *ach)
{
    int g, c;
    for (g = 0; g < (int)ach->num_groups; g++) {
        RC_CondGroup *grp = &ach->groups[g];
        for (c = 0; c < (int)grp->count; c++) grp->conds[c].current_hits = 0;
    }
}

/* Global crash locator - written before each risky operation */
volatile unsigned int g_crash_point = 0;

/* Debug: dump condition values for one achievement, limited calls */
static int g_debug_dump_count = 0;
static void debug_dump_ach(RC_ParsedAchievement *ach, RC_RuntimeState *state, int ach_idx)
{
    int i;
    RC_CondGroup *grp;
    unsigned int add_source = 0, add_address = 0;

    /* Dump ach #0 twice, ach #3 twice */
    if (ach_idx == 0 && g_debug_dump_count >= 2) return;
    if (ach_idx == 3 && g_debug_dump_count >= 4) return;
    if (ach_idx != 0 && ach_idx != 3) return;
    g_debug_dump_count++;

    grp = &ach->groups[0];
    rc_log_int("DBG:ach", ach_idx);
    rc_log_int("DBG:ng", (int)ach->num_groups);
    rc_log_int("DBG:nc", (int)grp->count);

    for (i = 0; i < (int)grp->count && i < 12; i++) {
        RC_Condition *cond = &grp->conds[i];
        RC_CondType ctype = (RC_CondType)cond->type;
        unsigned int lv, rv;

        lv = resolve_operand(&cond->left, state, add_address);
        lv += add_source;
        rv = resolve_operand(&cond->right, state, add_address);

        if (ctype != RC_COND_ADD_SOURCE &&
            ctype != RC_COND_SUB_SOURCE &&
            ctype != RC_COND_ADD_ADDRESS) {
            add_source = 0; add_address = 0;
        }

        rc_log_int("c", i);
        rc_log_int("  tp", (int)ctype);
        rc_log_int("  lv", (int)lv);
        rc_log_int("  rv", (int)rv);
        rc_log_int("  la", (int)cond->left.address);
        rc_log_int("  ls", (int)cond->left.size);
        rc_log_int("  ld", (int)cond->left.is_delta);

        if (ctype == RC_COND_ADD_ADDRESS) {
            add_address = lv;
            rc_log_int("  ->aa", (int)add_address);
        } else if (ctype == RC_COND_ADD_SOURCE) {
            add_source = lv;
        } else if (ctype == RC_COND_SUB_SOURCE) {
            add_source = (unsigned int)(-(int)lv);
        }
    }
}

static int evaluate_achievement(RC_ParsedAchievement *ach, RC_RuntimeState *state)
{
    int reset=0, paused=0, core, has_alts, any_alt, g;

    g_crash_point = 0xAA01;
    if (!ach->is_active || ach->num_groups == 0) return 0;

    g_crash_point = 0xAA02;
    core = evaluate_group(&ach->groups[0], state, &reset, &paused);

    g_crash_point = 0xAA03;
    if (reset)  { reset_all_hits(ach); return 0; }
    if (paused) return 0;

    g_crash_point = 0xAA04;
    has_alts = (ach->num_groups > 1) ? 1 : 0;
    any_alt  = 0;
    if (has_alts) {
        for (g = 1; g < (int)ach->num_groups; g++) {
            int ar=0, ap=0;
            int alt = evaluate_group(&ach->groups[g], state, &ar, &ap);
            if (ar) { reset_all_hits(ach); return 0; }
            if (alt && !ap) any_alt = 1;
        }
    }

    g_crash_point = 0xAA05;
    return (core && (!has_alts || any_alt)) ? 1 : 0;
}

/* ================================================================== */
/* PUBLIC API                                                          */
/* ================================================================== */

void rc_glue_init(RC_RuntimeState *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

RC_EvalResult rc_glue_update(PACH_LoadedGame *game,
                              PACH_ProfileGameProgress *progress,
                              RC_RuntimeState *state,
                              RC_ParsedAchievement *parsed_cache,
                              int num_parsed)
{
    static int init_done  = 0;
    RC_EvalResult result;
    int i;
    result.unlocked_count = 0;

    if (!game || !progress || !state || !parsed_cache || num_parsed <= 0)
        return result;

    /* ---- First call: register delta/prior slots only, return immediately ---- */
    if (!init_done) {
        for (i = 0; i < num_parsed; i++) {
            register_achievement_memrefs(&parsed_cache[i], state);
        }
        init_done = 1;
        return result;
    }

    /* ---- Update delta snapshots once per frame ---- */
    update_delta_snapshots(state);

    /* ---- Evaluate ALL achievements ---- */
    for (i = 0; i < num_parsed; i++) {
        RC_ParsedAchievement *ach = &parsed_cache[i];

        if (ach->is_active && ach->num_groups > 0 &&
            i < progress->num_achievements &&
            progress->unlock_time[i] == 0)
        {
            if (evaluate_achievement(ach, state)) {
                ach->is_active = 0;
                progress->unlock_time[i] = 1;
                if (result.unlocked_count < RC_MAX_UNLOCKED_PER_FRAME) {
                    result.unlocked_indices[result.unlocked_count] = i;
                    result.unlocked_defs[result.unlocked_count] =
                        &game->achievements[i];
                    result.unlocked_count++;
                }
            }
        }
    }

    sceKernelDelayThread(0);  /* yield after full evaluation */

    return result;
}