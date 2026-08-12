#ifndef RASTERFALL_AI_NAMES_H
#define RASTERFALL_AI_NAMES_H

/* 雇佣 AI 姓名表：直接在此追加字符串即可扩充名字池。 */
static const char *const rasterfall_hired_ai_names[] = {
    "MERCURY", "ROOK", "VIXEN", "BISHOP", "NOVA",
    "MAVERICK", "EMBER", "ORBIT", "SABLE", "KITE",
    "WREN", "LOCKE", "JUNO", "RANGER", "PIXEL",
    "MOSS", "COMET", "DUSTY", "ONYX", "QUILL"
};
#define RASTERFALL_HIRED_AI_NAME_COUNT \
    ((int)(sizeof(rasterfall_hired_ai_names) / \
           sizeof(rasterfall_hired_ai_names[0])))

#endif
