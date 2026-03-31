#ifndef RCHEEVOS_GLUE_H
#define RCHEEVOS_GLUE_H

#include "format.h"
#include "game_db.h"
#include "profile.h"

#define RC_MAX_CONDITIONS         24
#define RC_MAX_GROUPS             4
#define RC_MAX_DELTA_SLOTS        128
#define RC_MAX_UNLOCKED_PER_FRAME 8

typedef enum {
    RC_MEMSIZE_8BIT,
    RC_MEMSIZE_16BIT,
    RC_MEMSIZE_24BIT,
    RC_MEMSIZE_32BIT,
    RC_MEMSIZE_BIT0,
    RC_MEMSIZE_BIT1,
    RC_MEMSIZE_BIT2,
    RC_MEMSIZE_BIT3,
    RC_MEMSIZE_BIT4,
    RC_MEMSIZE_BIT5,
    RC_MEMSIZE_BIT6,
    RC_MEMSIZE_BIT7,
    RC_MEMSIZE_FLOAT,       /* fF = LE float (native on PSP) */
    RC_MEMSIZE_FLOAT_BE,    /* fB = BE float (byte-swapped)  */
    RC_MEMSIZE_CONST_UINT,
    RC_MEMSIZE_CONST_FLOAT
} RC_MemSize;

typedef struct {
    unsigned int  address;
    unsigned int  const_uint;
    float         const_float;
    unsigned int  mask;
    unsigned char size;      /* RC_MemSize  */
    unsigned char has_mask;
    unsigned char is_delta;
    unsigned char is_prior;
} RC_Operand;

typedef enum {
    RC_OP_EQ, RC_OP_NE, RC_OP_LT, RC_OP_LE, RC_OP_GT, RC_OP_GE, RC_OP_NONE
} RC_CompOp;

typedef enum {
    RC_COND_STANDARD = 0,
    RC_COND_RESET_IF,
    RC_COND_PAUSE_IF,
    RC_COND_AND_NEXT,
    RC_COND_OR_NEXT,
    RC_COND_TRIGGER,
    RC_COND_MEASURED,
    RC_COND_MEASURED_IF,
    RC_COND_ADD_SOURCE,
    RC_COND_ADD_ADDRESS,
    RC_COND_SUB_SOURCE,
    RC_COND_RESET_NEXT_IF
} RC_CondType;

typedef struct {
    RC_Operand    left;
    RC_Operand    right;
    unsigned int  required_hits;
    unsigned int  current_hits;
    unsigned char type;    /* RC_CondType */
    unsigned char op;      /* RC_CompOp   */
    unsigned char _pad[2];
} RC_Condition;

typedef struct {
    RC_Condition  conds[RC_MAX_CONDITIONS];
    unsigned char count;
    unsigned char _pad[3];
} RC_CondGroup;

typedef struct {
    RC_CondGroup  groups[RC_MAX_GROUPS];
    unsigned char num_groups;
    unsigned char is_active;
    unsigned char _pad[2];
} RC_ParsedAchievement;

typedef struct {
    unsigned int addr;
    RC_MemSize   size;
    unsigned int current;
    unsigned int delta;
    unsigned int prior;
} RC_DeltaSlot;

typedef struct {
    RC_DeltaSlot slots[RC_MAX_DELTA_SLOTS];
    int          num_slots;
} RC_RuntimeState;

typedef struct {
    int                  unlocked_count;
    int                  unlocked_indices[RC_MAX_UNLOCKED_PER_FRAME];
    PACH_AchievementDef *unlocked_defs[RC_MAX_UNLOCKED_PER_FRAME];
} RC_EvalResult;

void          rc_glue_init(RC_RuntimeState *state);
int           rc_glue_parse(const char *logic, RC_ParsedAchievement *out);
int           rc_glue_parse_all(PACH_LoadedGame *game,
                                RC_ParsedAchievement *out_array,
                                int max_count);
RC_EvalResult rc_glue_update(PACH_LoadedGame *game,
                             PACH_ProfileGameProgress *progress,
                             RC_RuntimeState *state,
                             RC_ParsedAchievement *parsed_cache,
                             int num_parsed);

#endif /* RCHEEVOS_GLUE_H */