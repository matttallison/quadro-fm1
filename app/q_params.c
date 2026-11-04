/* SPDX-License-Identifier: GPL-3.0-only */
/* QUADRO: the patch. One byte a parameter (signed where it goes below 0), a name; the descriptors the screens and
 * the knobs go by; the pages (four parameters each) of every section. A patch is what one of the 16 programs holds. */

enum {
    /* BASS: the electric bass (its own VCO, VCF and AD envelope) and the string bass (into the strings' chorus) */
    QP_B_EB16, QP_B_EB8, QP_B_SB16, QP_B_SB8, QP_B_OCT, QP_B_TUNE, QP_B_CUT, QP_B_RES, QP_B_ENV, QP_B_DEC, QP_B_SBVOL,
    /* STRINGS */
    QP_S_8, QP_S_4, QP_S_ATK, QP_S_REL, QP_S_ENS, QP_S_TONE,
    QP_HOLLOW,                                  /* (1.0's square for STRINGS and POLY: now their WAVE; a program that has it set gets SQR) */
    /* POLY SYNTH */
    QP_P_8, QP_P_4, QP_P_CUT, QP_P_RES, QP_P_ENV, QP_P_KBD, QP_P_LFO, QP_P_TREM, QP_P_A, QP_P_D, QP_P_S, QP_P_R,
    /* LEAD SYNTH */
    QP_L_WAVE, QP_L_VCO2, QP_L_INT, QP_L_DET, QP_L_CUT, QP_L_RES, QP_L_ENV, QP_L_OCT, QP_L_A, QP_L_DEC, QP_L_SLOW,
    QP_L_R, QP_L_2V, QP_L_POR1, QP_L_POR2, QP_L_TRIG, QP_L_VIB, QP_L_VDEL, QP_L_TRILL, QP_L_PWM,
    /* modulation and the arpeggiator */
    QP_LFO, QP_TRSPD, QP_ARP,
    /* PHASE SHIFTER */
    QP_PH_B, QP_PH_S, QP_PH_P, QP_PH_L, QP_PH_SPD, QP_PH_DEP, QP_PH_RES, QP_PH_MOD,
    /* OUTPUT MIXER, the keyboard, the room */
    QP_M_B, QP_M_S, QP_M_P, QP_M_L, QP_SPLIT, QP_REV, QP_DLY, QP_DTIME,
    QP_L_TOUCH,                                 /* the TOUCH SENSOR: key pressure on the lead's keys */
    QP_PARA,                                    /* paraphonic strings and poly: one AR, one ADSR, one VCF */
    QP_ARP_OCT,                                 /* the sequencer's range, 1..3 octaves */
    QP_CHORD,                                   /* CHORD: one key, a chord on the strings and poly */
    QP_CHBASS,                                  /* CH BASS: the bass plays the root, 1 or 2 octaves down, no split */
    QP_SYNC,                                    /* the sequencer and the trill on the MIDI clock: a note value */
    /* 1.1: the oscillators, the filters, the LFO (appended: a saved program keeps its parameters' places) */
    QP_B_WAVE, QP_B_PW, QP_B_FMODE, QP_B_KBD, QP_B_LFO, QP_B_DRIVE,
    QP_S_WAVE, QP_S_PW, QP_S_16,
    QP_P_WAVE, QP_P_PW, QP_P_PWM, QP_P_DET, QP_P_VIB, QP_P_FMODE, QP_P_16,
    QP_L_W2, QP_L_SUB, QP_L_NOISE, QP_L_SYNC, QP_L_FMODE, QP_L_DRIVE, QP_L_KBD,
    QP_LFO_W,
    /* 1.2: the step sequencer: the pattern (semitones from the lowest lead key, REST), its length, the gate */
    QP_SQ_LEN, QP_SQ_GATE, QP_SQ_SW,
    QP_SQ0, QP_SQ1, QP_SQ2, QP_SQ3, QP_SQ4, QP_SQ5, QP_SQ6, QP_SQ7,
    QP_SQ8, QP_SQ9, QP_SQ10, QP_SQ11, QP_SQ12, QP_SQ13, QP_SQ14, QP_SQ15,
    QP_N
};
#define QD_NAME 12

typedef struct {
    int8_t v[QP_N];
    char name[QD_NAME];
} q_patch_t;

enum { F_LVL, F_ONOFF, F_ENUM, F_SEMI, F_CENT, F_TIME, F_HZ, F_NOTE, F_OCT, F_RATE, F_PCT, F_INT, F_STEP };
typedef struct {
    const char *label;                          /* at most 5 characters: a knob column */
    const char *long_label;                     /* the page's big line when it is turned */
    int8_t min, max, def;
    uint8_t fmt;
    const char *const *names;                   /* F_ENUM */
} q_desc_t;

static const char *const N_LWAVE[] = {"SAW", "SQR", "10%", "PWM10", "PWM50", "TRI", "SINE"};
static const char *const N_LW2[] = {"=VCO1", "SAW", "SQR", "TRI", "SINE"};
static const char *const N_WAVE[] = {"SAW", "SQR", "PULSE", "TRI", "SINE"};
static const char *const N_BWAVE[] = {"SQR", "SAW", "PULSE", "TRI", "SINE"};   /* (SQR first: the bass of 1.0) */
static const char *const N_FMODE[] = {"LP24", "LP12", "BP", "HP"};
static const char *const N_LFOW[] = {"SINE", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_ARP[] = {"OFF", "UP", "UP/DN", "DOWN", "RAND", "STEP", "PEDAL", "THUMB", "CONV", "DIVRG", "PLAYED", "SKIP"};
static const char *const N_TRIG[] = {"SINGL", "MULTI"};
static const char *const N_PHMOD[] = {"SWEEP", "S&H", "ENV", "LFO"};
static const char *const N_DTIME[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const N_TOUCH[] = {"OFF", "BEND", "BRILL"};
static const char *const N_SYNC[] = {"OFF", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const N_CHBASS[] = {"OFF", "-1 OCT", "-2 OCT"};
static const char *const N_CHORD[] = {"OFF", "MAJ", "MIN", "7", "MIN7", "MAJ7", "SUS4", "5TH", "STACK", "MIN9", "ADD9", "MIN6", "MAJ6", "DIM", "AUG", "SUS2", "7SUS", "DOM9", "MAJ9", "MIN11", "M7B5", "OCT", "4THS", "M7#11"};

static const q_desc_t QD[QP_N] = {
    [QP_B_EB16] = {"EB16", "ELEC BASS 16'", 0, 1, 1, F_ONOFF, 0},
    [QP_B_EB8] = {"EB8", "ELEC BASS 8'", 0, 1, 0, F_ONOFF, 0},
    [QP_B_SB16] = {"SB16", "STRING BASS 16'", 0, 1, 0, F_ONOFF, 0},
    [QP_B_SB8] = {"SB8", "STRING BASS 8'", 0, 1, 0, F_ONOFF, 0},
    [QP_B_OCT] = {"OCT", "BASS OCTAVE", -1, 0, 0, F_OCT, 0},
    [QP_B_TUNE] = {"TUNE", "BASS TUNE", -50, 50, 0, F_CENT, 0},
    [QP_B_CUT] = {"CUT", "BASS CUTOFF", 0, 127, 40, F_HZ, 0},
    [QP_B_RES] = {"RES", "BASS RESONANCE", 0, 127, 30, F_PCT, 0},
    [QP_B_ENV] = {"ENV", "BASS ENV > VCF", 0, 127, 70, F_PCT, 0},
    [QP_B_DEC] = {"DECAY", "BASS DECAY", 0, 127, 80, F_TIME, 0},
    [QP_B_SBVOL] = {"SBVOL", "STRING BASS VOL", 0, 127, 100, F_LVL, 0},
    [QP_S_8] = {"8'", "STRINGS 8' VIOLA", 0, 1, 1, F_ONOFF, 0},
    [QP_S_4] = {"4'", "STRINGS 4' VIOLIN", 0, 1, 1, F_ONOFF, 0},
    [QP_S_ATK] = {"ATK", "STRINGS ATTACK", 0, 127, 60, F_TIME, 0},
    [QP_S_REL] = {"REL", "STRINGS RELEASE", 0, 127, 80, F_TIME, 0},
    [QP_S_ENS] = {"ENS", "ENSEMBLE DEPTH", 0, 127, 90, F_PCT, 0},
    [QP_S_TONE] = {"TONE", "STRINGS TONE", 0, 127, 80, F_PCT, 0},
    [QP_HOLLOW] = {"HOLLW", "HOLLOW (OLD)", 0, 1, 0, F_ONOFF, 0},
    [QP_P_8] = {"8'", "POLY 8'", 0, 1, 1, F_ONOFF, 0},
    [QP_P_4] = {"4'", "POLY 4'", 0, 1, 0, F_ONOFF, 0},
    [QP_P_CUT] = {"CUT", "POLY VCF CUTOFF", 0, 127, 50, F_HZ, 0},
    [QP_P_RES] = {"RES", "POLY RESONANCE", 0, 127, 10, F_PCT, 0},
    [QP_P_ENV] = {"ADSR", "POLY ADSR DEPTH", 0, 127, 60, F_PCT, 0},
    [QP_P_KBD] = {"KBD", "POLY KEY TRACK", 0, 127, 64, F_PCT, 0},
    [QP_P_LFO] = {"LFO", "POLY LFO > VCF", 0, 127, 0, F_PCT, 0},
    [QP_P_TREM] = {"TREM", "POLY TREMOLO", 0, 127, 0, F_PCT, 0},
    [QP_P_A] = {"ATK", "POLY ATTACK", 0, 127, 20, F_TIME, 0},
    [QP_P_D] = {"DEC", "POLY DECAY", 0, 127, 70, F_TIME, 0},
    [QP_P_S] = {"SUS", "POLY SUSTAIN", 0, 127, 90, F_PCT, 0},
    [QP_P_R] = {"REL", "POLY RELEASE", 0, 127, 60, F_TIME, 0},
    [QP_L_WAVE] = {"WAVE", "LEAD WAVEFORM", 0, 6, 0, F_ENUM, N_LWAVE},
    [QP_L_VCO2] = {"VCO2", "LEAD VCO 2", 0, 1, 1, F_ONOFF, 0},
    [QP_L_INT] = {"INT", "VCO 2 INTERVAL", -24, 24, 0, F_SEMI, 0},
    [QP_L_DET] = {"DET", "VCO 2 DETUNE", 0, 50, 8, F_CENT, 0},
    [QP_L_CUT] = {"CUT", "LEAD VCF CUTOFF", 0, 127, 55, F_HZ, 0},
    [QP_L_RES] = {"RES", "LEAD RESONANCE", 0, 127, 30, F_PCT, 0},
    [QP_L_ENV] = {"ENV", "LEAD ENV > VCF", 0, 127, 70, F_PCT, 0},
    [QP_L_OCT] = {"OCT", "LEAD TRANSPOSE", -2, 2, 0, F_OCT, 0},
    [QP_L_A] = {"ATK", "LEAD ATTACK", 0, 127, 10, F_TIME, 0},
    [QP_L_DEC] = {"DECAY", "LEAD DECAY TIME", 0, 127, 64, F_PCT, 0},
    [QP_L_SLOW] = {"SLOW", "SUSTAIN LOW", 0, 1, 0, F_ONOFF, 0},
    [QP_L_R] = {"REL", "LEAD RELEASE", 0, 127, 50, F_TIME, 0},
    [QP_L_2V] = {"2VOIC", "2 VOICE ON", 0, 1, 0, F_ONOFF, 0},
    [QP_L_POR1] = {"POR1", "VCO 1 PORTAMENTO", 0, 127, 0, F_TIME, 0},
    [QP_L_POR2] = {"POR2", "VCO 2 PORTAMENTO", 0, 127, 0, F_TIME, 0},
    [QP_L_TRIG] = {"TRIG", "KEYBOARD TRIGGER", 0, 1, 1, F_ENUM, N_TRIG},
    [QP_L_VIB] = {"VIB", "VIBRATO DEPTH", 0, 127, 0, F_PCT, 0},
    [QP_L_VDEL] = {"VDEL", "VIBRATO DELAY", 0, 127, 80, F_TIME, 0},
    [QP_L_TRILL] = {"TRILL", "TRILL INTERVAL", 0, 24, 0, F_SEMI, 0},
    [QP_L_PWM] = {"PWM", "ENV > PULSE WIDTH", 0, 127, 64, F_PCT, 0},
    [QP_LFO] = {"LFO", "LFO SPEED", 0, 127, 60, F_RATE, 0},
    [QP_TRSPD] = {"SPEED", "TRILL / SEQ SPEED", 0, 127, 50, F_RATE, 0},
    [QP_ARP] = {"SEQ", "SEQUENCER", 0, 11, 0, F_ENUM, N_ARP},
    [QP_PH_B] = {"BASS", "PHASER: BASS IN", 0, 1, 0, F_ONOFF, 0},
    [QP_PH_S] = {"STRG", "PHASER: STRINGS IN", 0, 1, 1, F_ONOFF, 0},
    [QP_PH_P] = {"POLY", "PHASER: POLY IN", 0, 1, 0, F_ONOFF, 0},
    [QP_PH_L] = {"LEAD", "PHASER: LEAD IN", 0, 1, 0, F_ONOFF, 0},
    [QP_PH_SPD] = {"SPEED", "SWEEP SPEED", 0, 127, 30, F_RATE, 0},
    [QP_PH_DEP] = {"DEPTH", "SWEEP DEPTH", 0, 127, 90, F_PCT, 0},
    [QP_PH_RES] = {"RES", "PHASER RESONANCE", 0, 127, 40, F_PCT, 0},
    [QP_PH_MOD] = {"MOD", "PHASER MOD", 0, 3, 0, F_ENUM, N_PHMOD},
    [QP_M_B] = {"BASS", "BASS LEVEL", 0, 127, 100, F_LVL, 0},
    [QP_M_S] = {"STRG", "STRINGS LEVEL", 0, 127, 100, F_LVL, 0},
    [QP_M_P] = {"POLY", "POLY LEVEL", 0, 127, 100, F_LVL, 0},
    [QP_M_L] = {"LEAD", "LEAD LEVEL", 0, 127, 100, F_LVL, 0},
    [QP_SPLIT] = {"SPLIT", "BASS SPLIT", 48, 72, 60, F_NOTE, 0},
    [QP_REV] = {"REVRB", "REVERB SEND", 0, 127, 30, F_PCT, 0},
    [QP_DLY] = {"DELAY", "DELAY SEND", 0, 127, 0, F_PCT, 0},
    [QP_DTIME] = {"TIME", "DELAY TIME", 0, 5, 1, F_ENUM, N_DTIME},
    [QP_L_TOUCH] = {"TOUCH", "TOUCH SENSOR", 0, 2, 1, F_ENUM, N_TOUCH},
    [QP_PARA] = {"PARA", "PARAPHONIC", 0, 1, 0, F_ONOFF, 0},
    [QP_ARP_OCT] = {"RANGE", "SEQUENCER RANGE", 1, 3, 1, F_INT, 0},
    [QP_CHORD] = {"CHORD", "CHORD (ONE KEY)", 0, 22, 0, F_ENUM, N_CHORD},
    [QP_CHBASS] = {"CHBAS", "CHORD BASS (ROOT)", 0, 2, 0, F_ENUM, N_CHBASS},
    [QP_SYNC] = {"SYNC", "SYNC TO MIDI CLOCK", 0, 6, 4, F_ENUM, N_SYNC},
    [QP_B_WAVE] = {"WAVE", "BASS WAVEFORM", 0, 4, 0, F_ENUM, N_BWAVE},
    [QP_B_PW] = {"PW", "BASS PULSE WIDTH", 0, 127, 64, F_PCT, 0},
    [QP_B_FMODE] = {"TYPE", "BASS FILTER TYPE", 0, 3, 0, F_ENUM, N_FMODE},
    [QP_B_KBD] = {"KBD", "BASS KEY TRACK", 0, 127, 0, F_PCT, 0},
    [QP_B_LFO] = {"LFO", "BASS LFO > VCF", 0, 127, 0, F_PCT, 0},
    [QP_B_DRIVE] = {"DRIVE", "BASS FILTER DRIVE", 0, 127, 0, F_PCT, 0},
    [QP_S_WAVE] = {"WAVE", "STRINGS WAVEFORM", 0, 4, 0, F_ENUM, N_WAVE},
    [QP_S_PW] = {"PW", "STRINGS PULSE WIDTH", 0, 127, 64, F_PCT, 0},
    [QP_S_16] = {"16'", "STRINGS 16' CELLO", 0, 1, 0, F_ONOFF, 0},
    [QP_P_WAVE] = {"WAVE", "POLY WAVEFORM", 0, 4, 0, F_ENUM, N_WAVE},
    [QP_P_PW] = {"PW", "POLY PULSE WIDTH", 0, 127, 64, F_PCT, 0},
    [QP_P_PWM] = {"PWM", "POLY LFO > PULSE WIDTH", 0, 127, 0, F_PCT, 0},
    [QP_P_DET] = {"DET", "POLY UNISON DETUNE", 0, 50, 0, F_CENT, 0},
    [QP_P_VIB] = {"VIB", "POLY LFO > PITCH", 0, 127, 0, F_PCT, 0},
    [QP_P_FMODE] = {"TYPE", "POLY FILTER TYPE", 0, 3, 0, F_ENUM, N_FMODE},
    [QP_P_16] = {"16'", "POLY 16' SUB", 0, 1, 0, F_ONOFF, 0},
    [QP_L_W2] = {"WAVE2", "VCO 2 WAVEFORM", 0, 4, 0, F_ENUM, N_LW2},
    [QP_L_SUB] = {"SUB", "SUB OSCILLATOR", 0, 127, 0, F_PCT, 0},
    [QP_L_NOISE] = {"NOISE", "NOISE LEVEL", 0, 127, 0, F_PCT, 0},
    [QP_L_SYNC] = {"SYNC", "VCO 2 HARD SYNC", 0, 1, 0, F_ONOFF, 0},
    [QP_L_FMODE] = {"TYPE", "LEAD FILTER TYPE", 0, 3, 0, F_ENUM, N_FMODE},
    [QP_L_DRIVE] = {"DRIVE", "LEAD FILTER DRIVE", 0, 127, 0, F_PCT, 0},
    [QP_L_KBD] = {"KBD", "LEAD KEY TRACK", 0, 127, 32, F_PCT, 0},
    [QP_SQ_LEN] = {"LENGTH", "STEP SEQUENCER LENGTH", 1, 16, 8, F_INT, 0},
    [QP_SQ_GATE] = {"GATE", "SEQUENCER GATE LENGTH", 5, 100, 100, F_INT, 0},
    [QP_SQ_SW] = {"SWING", "SEQUENCER SWING", 0, 50, 0, F_INT, 0},
    [QP_SQ0] = {"STEP 1", "SEQUENCER STEP 1", -25, 24, 0, F_STEP, 0},
    [QP_SQ1] = {"STEP 2", "SEQUENCER STEP 2", -25, 24, 0, F_STEP, 0},
    [QP_SQ2] = {"STEP 3", "SEQUENCER STEP 3", -25, 24, 12, F_STEP, 0},
    [QP_SQ3] = {"STEP 4", "SEQUENCER STEP 4", -25, 24, 0, F_STEP, 0},
    [QP_SQ4] = {"STEP 5", "SEQUENCER STEP 5", -25, 24, 3, F_STEP, 0},
    [QP_SQ5] = {"STEP 6", "SEQUENCER STEP 6", -25, 24, 0, F_STEP, 0},
    [QP_SQ6] = {"STEP 7", "SEQUENCER STEP 7", -25, 24, 7, F_STEP, 0},
    [QP_SQ7] = {"STEP 8", "SEQUENCER STEP 8", -25, 24, 10, F_STEP, 0},
    [QP_SQ8] = {"STEP 9", "SEQUENCER STEP 9", -25, 24, 0, F_STEP, 0},
    [QP_SQ9] = {"STEP 10", "SEQUENCER STEP 10", -25, 24, 12, F_STEP, 0},
    [QP_SQ10] = {"STEP 11", "SEQUENCER STEP 11", -25, 24, 0, F_STEP, 0},
    [QP_SQ11] = {"STEP 12", "SEQUENCER STEP 12", -25, 24, 7, F_STEP, 0},
    [QP_SQ12] = {"STEP 13", "SEQUENCER STEP 13", -25, 24, 5, F_STEP, 0},
    [QP_SQ13] = {"STEP 14", "SEQUENCER STEP 14", -25, 24, 0, F_STEP, 0},
    [QP_SQ14] = {"STEP 15", "SEQUENCER STEP 15", -25, 24, 10, F_STEP, 0},
    [QP_SQ15] = {"STEP 16", "SEQUENCER STEP 16", -25, 24, -12, F_STEP, 0},
    [QP_LFO_W] = {"SHAPE", "LFO WAVEFORM", 0, 4, 0, F_ENUM, N_LFOW},
};

/* sections: the four of the original and the phase shifter / mixer */
enum { SEC_BASS, SEC_STR, SEC_POLY, SEC_LEAD, SEC_PHAS, SEC_MIX, SEC_N };
static const char *const SEC_NAME[SEC_N] = {"BASS", "STRINGS", "POLY", "LEAD", "PHASER", "MIXER"};
static const char *const SEC_SHORT[SEC_N] = {"BASS", "STRG", "POLY", "LEAD", "PHAS", "MIX"};

/* the page kinds: which button opens them (EDIT the sound, ENV the envelope, LFO the modulation, SCL the keys) and
 * what the screen draws over the knobs */
enum { VZ_VOICES, VZ_VCF, VZ_ENV_AD, VZ_ENV_AR, VZ_ENV_ADSR, VZ_ENV_LEAD, VZ_VCO, VZ_MOD, VZ_KEYS, VZ_ARP, VZ_PH_IN,
       VZ_PH_SWEEP, VZ_MIXER, VZ_FX, VZ_TUNE, VZ_ENS, VZ_WAVE };
enum { PK_EDIT, PK_ENV, PK_LFO, PK_SCL };
typedef struct {
    uint8_t sec, kind, vz;
    const char *name;
    uint8_t p[4];                               /* QP_N: an empty column */
} q_page_t;
#define _ QP_N
static const q_page_t PAGES[] = {
    {SEC_BASS, PK_EDIT, VZ_VOICES, "VOICES", {QP_B_EB16, QP_B_EB8, QP_B_SB16, QP_B_SB8}},
    {SEC_BASS, PK_EDIT, VZ_WAVE, "OSC", {QP_B_WAVE, QP_B_PW, QP_B_TUNE, QP_B_OCT}},
    {SEC_BASS, PK_EDIT, VZ_VCF, "VCF", {QP_B_CUT, QP_B_RES, QP_B_ENV, QP_B_DEC}},
    {SEC_BASS, PK_EDIT, VZ_VCF, "FILTER", {QP_B_FMODE, QP_B_DRIVE, QP_B_KBD, _}},
    {SEC_BASS, PK_ENV, VZ_ENV_AD, "ENVELOPE", {QP_B_DEC, QP_B_ENV, QP_B_SBVOL, QP_S_REL}},
    {SEC_BASS, PK_LFO, VZ_MOD, "LFO", {QP_B_LFO, QP_LFO, QP_LFO_W, _}},
    {SEC_BASS, PK_SCL, VZ_KEYS, "KEYBOARD", {QP_SPLIT, QP_CHORD, QP_CHBASS, QP_L_OCT}},
    {SEC_STR, PK_EDIT, VZ_VOICES, "VOICES", {QP_S_16, QP_S_8, QP_S_4, QP_PARA}},
    {SEC_STR, PK_EDIT, VZ_WAVE, "OSC", {QP_S_WAVE, QP_S_PW, _, _}},
    {SEC_STR, PK_EDIT, VZ_ENS, "ENSEMBLE", {QP_S_ENS, QP_S_TONE, QP_PH_S, QP_M_S}},
    {SEC_STR, PK_ENV, VZ_ENV_AR, "AR", {QP_S_ATK, QP_S_REL, _, QP_M_S}},
    {SEC_STR, PK_LFO, VZ_ENS, "CHORUS", {QP_S_ENS, QP_S_TONE, QP_LFO, QP_M_S}},
    {SEC_STR, PK_SCL, VZ_KEYS, "KEYBOARD", {QP_SPLIT, QP_CHORD, QP_CHBASS, QP_L_OCT}},
    {SEC_POLY, PK_EDIT, VZ_VOICES, "VOICES", {QP_P_16, QP_P_8, QP_P_4, QP_PARA}},
    {SEC_POLY, PK_EDIT, VZ_WAVE, "OSC", {QP_P_WAVE, QP_P_PW, QP_P_PWM, QP_P_DET}},
    {SEC_POLY, PK_EDIT, VZ_VCF, "VCF", {QP_P_CUT, QP_P_RES, QP_P_ENV, QP_P_FMODE}},
    {SEC_POLY, PK_ENV, VZ_ENV_ADSR, "ADSR", {QP_P_A, QP_P_D, QP_P_S, QP_P_R}},
    {SEC_POLY, PK_LFO, VZ_MOD, "LFO", {QP_LFO, QP_LFO_W, QP_P_LFO, QP_P_TREM}},
    {SEC_POLY, PK_LFO, VZ_MOD, "PITCH", {QP_P_VIB, QP_P_KBD, _, _}},
    {SEC_POLY, PK_SCL, VZ_KEYS, "KEYBOARD", {QP_SPLIT, QP_CHORD, QP_CHBASS, QP_L_OCT}},
    {SEC_LEAD, PK_EDIT, VZ_VCO, "VCO", {QP_L_WAVE, QP_L_VCO2, QP_L_INT, QP_L_DET}},
    {SEC_LEAD, PK_EDIT, VZ_VCO, "MIX", {QP_L_W2, QP_L_SYNC, QP_L_SUB, QP_L_NOISE}},
    {SEC_LEAD, PK_EDIT, VZ_VCF, "VCF", {QP_L_CUT, QP_L_RES, QP_L_ENV, QP_L_PWM}},
    {SEC_LEAD, PK_EDIT, VZ_VCF, "FILTER", {QP_L_FMODE, QP_L_DRIVE, QP_L_KBD, _}},
    {SEC_LEAD, PK_ENV, VZ_ENV_LEAD, "ADS/ASR", {QP_L_A, QP_L_DEC, QP_L_SLOW, QP_L_R}},
    {SEC_LEAD, PK_LFO, VZ_MOD, "VIBRATO", {QP_L_VIB, QP_L_VDEL, QP_L_TRILL, QP_L_TOUCH}},
    {SEC_LEAD, PK_LFO, VZ_MOD, "LFO", {QP_LFO, QP_LFO_W, _, _}},
    {SEC_LEAD, PK_SCL, VZ_KEYS, "PLAY", {QP_L_2V, QP_L_POR1, QP_L_POR2, QP_L_TRIG}},
    {SEC_LEAD, PK_SCL, VZ_ARP, "SEQUENCER", {QP_ARP, QP_TRSPD, QP_SYNC, QP_ARP_OCT}},
    {SEC_LEAD, PK_SCL, VZ_ARP, "GATE", {QP_SQ_LEN, QP_SQ_GATE, QP_SQ_SW, _}},
    {SEC_LEAD, PK_SCL, VZ_ARP, "STEPS 1-4", {QP_SQ0, QP_SQ1, QP_SQ2, QP_SQ3}},
    {SEC_LEAD, PK_SCL, VZ_ARP, "STEPS 5-8", {QP_SQ4, QP_SQ5, QP_SQ6, QP_SQ7}},
    {SEC_LEAD, PK_SCL, VZ_ARP, "STEPS 9-12", {QP_SQ8, QP_SQ9, QP_SQ10, QP_SQ11}},
    {SEC_LEAD, PK_SCL, VZ_ARP, "STEPS 13-16", {QP_SQ12, QP_SQ13, QP_SQ14, QP_SQ15}},
    {SEC_PHAS, PK_EDIT, VZ_PH_IN, "INPUTS", {QP_PH_B, QP_PH_S, QP_PH_P, QP_PH_L}},
    {SEC_PHAS, PK_EDIT, VZ_PH_SWEEP, "SWEEP", {QP_PH_SPD, QP_PH_DEP, QP_PH_RES, QP_PH_MOD}},
    {SEC_MIX, PK_EDIT, VZ_MIXER, "OUTPUT MIXER", {QP_M_B, QP_M_S, QP_M_P, QP_M_L}},
    {SEC_MIX, PK_EDIT, VZ_FX, "ROOM", {QP_REV, QP_DLY, QP_DTIME, QP_SPLIT}},
};
#undef _
#define NPAGES (sizeof PAGES / sizeof PAGES[0])

static int32_t q_clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void q_patch_init(q_patch_t *p, const char *name)
{
    uint32_t i;
    for (i = 0; i < QP_N; i++)
        p->v[i] = QD[i].def;
    for (i = 0; i < QD_NAME; i++)
        p->name[i] = 0;
    for (i = 0; i < QD_NAME - 1u && name[i]; i++)
        p->name[i] = name[i];
}
