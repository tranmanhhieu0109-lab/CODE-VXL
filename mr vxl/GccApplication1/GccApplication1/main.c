/*
 * ==========================================================
 *  MÁY TÍNH C?M TAY - ATmega324P - 1 FILE DUY NH?T
 * ==========================================================
 *
 *  C?U HÌNH CHÂN THEO YÊU C?U:
 * ----------------------------------------------------------
 *  [LCD 16x2] -> Giao ti?p 8-bit
 *   D0 - D7  --> PA0 - PA7 (PORTA)
 *   RS       --> PC0
 *   RW       --> PC1
 *   EN       --> PC2
 *
 *  [KEYPAD 4x4] -> PORTB
 *   ROW1 - ROW4 (Hàng) --> PB4 - PB7 (Output)
 *   COL1 - COL4 (C?t)  --> PB0 - PB3 (Input, có Pull-up)
 *
 *   Layout map phím (Gi? nguyên Logic FSM):
 *     PB4 (Hàng 0): C(Lên)    D(=)        #(Xóa)    *(Ch?m)
 *     PB5 (Hàng 1): 8         9           A(Menu)   B(Xu?ng)
 *     PB6 (Hàng 2): 4         5           6         7
 *     PB7 (Hàng 3): 0         1           2         3
 *
 * ==========================================================
 */

#define F_CPU 8000000UL // T?n s? th?ch anh 8MHz

#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>

/* ==========================================================
   PH?N 1: DRIVER LCD 8-BIT (Dùng PORTA và PORTC)
   ========================================================== */
#define LCD_DATA_PORT PORTA
#define LCD_DATA_DDR  DDRA

#define LCD_CTRL_PORT PORTC
#define LCD_CTRL_DDR  DDRC
#define LCD_RS        PC0
#define LCD_RW        PC1
#define LCD_EN        PC2

// T?o xung ch?t d? li?u vào LCD
static void lcd_pulse(void) {
    LCD_CTRL_PORT |=  (1<<LCD_EN); 
    _delay_us(1);
    LCD_CTRL_PORT &= ~(1<<LCD_EN); 
    _delay_us(50);
}

// G?i 1 byte (l?nh ho?c d? li?u) t?i LCD
static void lcd_byte(uint8_t b, uint8_t is_data) {
    if (is_data) LCD_CTRL_PORT |=  (1<<LCD_RS); // Ch? ?? d? li?u
    else         LCD_CTRL_PORT &= ~(1<<LCD_RS); // Ch? ?? l?nh
    
    LCD_CTRL_PORT &= ~(1<<LCD_RW); /* Luôn ? ch? ?? ghi (RW=0) */
    
    LCD_DATA_PORT = b;             /* Xu?t th?ng 8 bit ra PORTA */
    lcd_pulse();
}

static void lcd_cmd(uint8_t cmd)  { lcd_byte(cmd, 0); }
static void lcd_data(uint8_t d)   { lcd_byte(d,   1); }

// Kh?i t?o màn hình LCD
static void lcd_init(void) {
    LCD_DATA_DDR = 0xFF; /* PA0-PA7 là Output */
    LCD_CTRL_DDR |= (1<<LCD_RS)|(1<<LCD_RW)|(1<<LCD_EN); // C?u hình chân ?i?u khi?n là Output
    LCD_CTRL_PORT &= ~((1<<LCD_RS)|(1<<LCD_RW)|(1<<LCD_EN));
    
    _delay_ms(50);
    lcd_cmd(0x38);   /* Ch? ?? 8-bit, 2 dòng, font 5x8 */
    _delay_ms(5);
    lcd_cmd(0x38);
    _delay_us(150);
    lcd_cmd(0x38);
    
    lcd_cmd(0x0C);   /* B?t màn hình, t?t con tr? (cursor) */
    lcd_cmd(0x06);   /* T? ??ng t?ng ??a ch? (d?ch ph?i con tr? khi ghi) */
    lcd_cmd(0x01);   /* Xóa màn hình */
    _delay_ms(2);
}

static void lcd_clear(void)  { lcd_cmd(0x01); _delay_ms(2); }
static void lcd_cursor_on(void)  { lcd_cmd(0x0E); } // Hi?n con tr?
static void lcd_cursor_off(void) { lcd_cmd(0x0C); } // ?n con tr?

// Di chuy?n con tr? t?i hàng (0/1) và c?t (0-15)
static void lcd_goto(uint8_t row, uint8_t col) {
    lcd_cmd(0x80 | (row ? 0x40 : 0x00) | col);
}

// In chu?i ký t? ra LCD
static void lcd_str(const char *s) {
    while (*s) lcd_data((uint8_t)*s++);
}

// In chu?i lên 1 dòng c? ??nh, xóa các ký t? th?a phía sau
static void lcd_line(uint8_t row, uint8_t col, const char *s) {
    lcd_goto(row, col);
    uint8_t i = 0, max = 16 - col;
    while (s[i] && i < max) { lcd_data((uint8_t)s[i]); i++; }
    while (i < max)         { lcd_data(' '); i++; }
}

/* ==========================================================
   PH?N 2: DRIVER BÀN PHÍM 4x4 (Dùng PORTB)
   ========================================================== */
#define KEYPAD_DDR   DDRB
#define KEYPAD_PORT  PORTB
#define KEYPAD_PIN   PINB
#define KEY_NONE     0xFF
#define KEY_HOLD_MS  2000 // Th?i gian nh?n di?n nh?n gi? phím (2 giây)

/* Map phím theo Layout Proteus (?ã thay E, F thành #, *) */
static const uint8_t keymap[4][4] = {
    {'C','D','#','*'}, /* Hàng 0: PB4 */
    {'8','9','A','B'}, /* Hàng 1: PB5 */
    {'4','5','6','7'}, /* Hàng 2: PB6 */
    {'0','1','2','3'}  /* Hàng 3: PB7 */
};

static void keypad_init(void) {
    KEYPAD_DDR  = 0xF0;   /* PB4-PB7 (Hàng): Output, PB0-PB3 (C?t): Input */
    KEYPAD_PORT = 0xFF;   /* B?t ?i?n tr? kéo lên (Pull-up) ? C?t, Hàng xu?t m?c Cao (1) */
}

// Quét ma tr?n phím, tr? v? ký t? ho?c KEY_NONE n?u không ai nh?n
static uint8_t keypad_scan(void) {
    for (uint8_t r = 0; r < 4; r++) {
        /* Kéo chân hàng hi?n t?i (r+4) xu?ng m?c 0, gi? Pull-up ? c?t */
        KEYPAD_PORT = (uint8_t)(0xFF & ~(1 << (r + 4)));
        _delay_us(10); // ??i tr?ng thái ?n ??nh
        
        uint8_t col = KEYPAD_PIN & 0x0F; /* ??c tr?ng thái 4 c?t (PB0-PB3) */
        
        for (uint8_t c = 0; c < 4; c++) {
            if (!(col & (1 << c))) { // N?u c?t c b? kéo xu?ng 0 -> Phát hi?n phím nh?n
                KEYPAD_PORT = 0xFF; /* Tr? l?i m?c cao cho t?t c? hàng tr??c khi thoát */
                return keymap[r][c];
            }
        }
    }
    KEYPAD_PORT = 0xFF;
    return KEY_NONE;
}

// Hàm l?y phím có ch?ng d?i (debounce)
static uint8_t keypad_get(void) {
    uint8_t k;
    // Ch? th? phím (n?u ?ang gi?)
    while (keypad_scan() != KEY_NONE) _delay_ms(10);
    // Ch? nh?n phím m?i
    do { k = keypad_scan(); _delay_ms(20); } while (k == KEY_NONE);
    _delay_ms(50); // Debounce thêm sau khi nh?n di?n
    return k;
}

// Hàm ki?m tra phím có ?ang b? nh?n gi? hay không
static uint8_t keypad_held(void) {
    uint8_t k = keypad_scan();
    if (k == KEY_NONE) return 0;
    uint16_t cnt = 0;
    while (keypad_scan() == k) {
        _delay_ms(10); cnt++;
        if (cnt >= KEY_HOLD_MS / 10) return 1; // ??t th?i gian nh?n gi?
    }
    return 0;
}

/* ==========================================================
   PH?N 3: THU?T TOÁN SHUNTING-YARD + TÍNH RPN (GI? NGUYÊN)
   ========================================================== */
#define EXPR_MAX   64 // ?? dài t?i ?a c?a bi?u th?c
#define STKSZ      32 // Kích th??c ng?n x?p (stack)

// Các lo?i Token (thành ph?n c?a bi?u th?c)
typedef enum { TOK_NUM, TOK_OP, TOK_FUNC, TOK_LP, TOK_RP } TokType;
// Các hàm toán h?c h? tr?
typedef enum { FN_SIN, FN_COS, FN_TAN, FN_SQRT, FN_LOG, FN_LN } FuncID;

typedef struct {
    TokType type;
    union { double num; char op; FuncID fn; };
} Token;

// Các mã l?i tr? v? khi tính toán
#define EVAL_OK     0 // Tính toán thành công
#define EVAL_SYNTAX 1 // L?i cú pháp
#define EVAL_DIV0   2 // L?i chia cho 0
#define EVAL_DOMAIN 3 // L?i mi?n giá tr? (VD: log s? âm)

static Token  out[STKSZ]; // Queue ch?a bi?u th?c d?ng h?u t? (RPN)
static uint8_t out_n;
static Token  ops[STKSZ]; // Ng?n x?p ch?a toán t?
static int8_t ops_top;
static double nstk[STKSZ]; // Ng?n x?p ch?a s? khi tính RPN
static int8_t nstk_top;

// Các hàm thao tác v?i Ng?n x?p (Stack) toán t?
static void   op_push(Token t)   { if(ops_top<STKSZ-1) ops[++ops_top]=t; }
static Token  op_pop(void)       { return ops[ops_top--]; }
static Token  op_peek(void)      { return ops[ops_top]; }
static uint8_t op_empty(void)    { return ops_top<0; }

// Các hàm thao tác v?i Ng?n x?p (Stack) s?
static void   n_push(double v)   { if(nstk_top<STKSZ-1) nstk[++nstk_top]=v; }
static double n_pop(void)        { return nstk[nstk_top--]; }

// Hàm xác ??nh ?? ?u tiên c?a toán t?
static uint8_t prec(char o) {
    if(o=='+'||o=='-') return 1;
    if(o=='*'||o=='/') return 2;
    if(o=='^')         return 3;
    return 0;
}

// B?ng tra c?u các hàm toán h?c
static const struct { const char *name; FuncID id; } fns[] = {
    {"sin",FN_SIN},{"cos",FN_COS},{"tan",FN_TAN},
    {"sqrt",FN_SQRT},{"log",FN_LOG},{"ln",FN_LN}
};

// Thu?t toán Shunting-yard: Chuy?n bi?u th?c trung t? sang h?u t? (RPN)
static uint8_t shunt(const char *expr) {
    ops_top = -1; out_n = 0;
    const char *p = expr;
    uint8_t prev_num = 0; // C? ki?m tra token tr??c ?ó là s? (dùng ?? phân bi?t d?u âm và phép tr?)

    while (*p) {
        if (isspace((unsigned char)*p)) { p++; continue; }

        // ??c s?
        if (isdigit((unsigned char)*p) || *p == '.') {
            char *end; Token t;
            t.type = TOK_NUM;
            t.num  = strtod(p, &end);
            if (end == p) return EVAL_SYNTAX;
            out[out_n++] = t;
            p = end; prev_num = 1; continue;
        }

        // ??c hàm (sin, cos, sqrt,...)
        uint8_t found = 0;
        for (uint8_t i = 0; i < 6; i++) {
            size_t L = strlen(fns[i].name);
            if (strncmp(p, fns[i].name, L) == 0) {
                Token t; t.type = TOK_FUNC; t.fn = fns[i].id;
                op_push(t); p += L; found = 1; prev_num = 0; break;
            }
        }
        if (found) continue;

        // D?u ngo?c m?
        if (*p == '(') {
            Token t; t.type = TOK_LP;
            op_push(t); p++; prev_num = 0; continue;
        }

        // D?u ngo?c ?óng
        if (*p == ')') {
            while (!op_empty() && op_peek().type != TOK_LP)
                out[out_n++] = op_pop();
            if (op_empty()) return EVAL_SYNTAX; // L?i thi?u ngo?c m?
            op_pop(); // B? d?u ngo?c m? kh?i stack
            if (!op_empty() && op_peek().type == TOK_FUNC)
                out[out_n++] = op_pop(); // ??y hàm ra queue
            p++; prev_num = 1; continue;
        }

        // Các toán t? (+, -, *, /, ^)
        if (*p=='+' || *p=='-' || *p=='*' || *p=='/' || *p=='^') {
            char op = *p;
            // X? lý d?u âm m?t ngôi (vd: -5 ho?c (-5))
            if (op == '-' && !prev_num) {
                Token z; z.type = TOK_NUM; z.num = 0.0;
                out[out_n++] = z; // ??y s? 0 ?o vào queue (?? bi?n -x thành 0-x)
            }
            Token t; t.type = TOK_OP; t.op = op;
            // Xét ?? ?u tiên ?? ??y toán t? t? stack ra queue
            while (!op_empty() && op_peek().type == TOK_OP &&
                   (prec(op_peek().op) > prec(op) ||
                   (prec(op_peek().op) == prec(op) && op != '^')))
                out[out_n++] = op_pop();
            op_push(t); p++; prev_num = 0; continue;
        }

        return EVAL_SYNTAX; // Ký t? l? không nh?n di?n ???c
    }

    // ?? toàn b? toán t? còn l?i trong stack ra queue
    while (!op_empty()) {
        if (op_peek().type == TOK_LP) return EVAL_SYNTAX; // L?i d? ngo?c m?
        out[out_n++] = op_pop();
    }
    return EVAL_OK;
}

// Tính toán giá tr? t? bi?u th?c h?u t? (RPN)
static uint8_t calc_rpn(double *res) {
    nstk_top = -1;
    for (uint8_t i = 0; i < out_n; i++) {
        Token t = out[i];
        if (t.type == TOK_NUM) { n_push(t.num); continue; }
        
        if (t.type == TOK_OP) {
            if (nstk_top < 1) return EVAL_SYNTAX;
            double b = n_pop(), a = n_pop();
            switch(t.op) {
                case '+': n_push(a+b); break;
                case '-': n_push(a-b); break;
                case '*': n_push(a*b); break;
                case '/': if(b==0) return EVAL_DIV0; n_push(a/b); break;
                case '^': n_push(pow(a,b)); break;
                default:  return EVAL_SYNTAX;
            }
            continue;
        }
        
        if (t.type == TOK_FUNC) {
            if (nstk_top < 0) return EVAL_SYNTAX;
            double a = n_pop();
            double rad = a * (M_PI / 180.0); // Chuy?n ?? sang radian cho l??ng giác
            switch(t.fn) {
                case FN_SIN:  n_push(sin(rad)); break;
                case FN_COS:  n_push(cos(rad)); break;
                case FN_TAN:
                    if(fabs(cos(rad)) < 1e-10) return EVAL_DOMAIN; // Hàm tan không xác ??nh t?i 90 ??, 270 ??...
                    n_push(tan(rad)); break;
                case FN_SQRT:
                    if(a < 0) return EVAL_DOMAIN;
                    n_push(sqrt(a)); break;
                case FN_LOG:
                    if(a <= 0) return EVAL_DOMAIN;
                    n_push(log10(a)); break;
                case FN_LN:
                    if(a <= 0) return EVAL_DOMAIN;
                    n_push(log(a)); break;
            }
            continue;
        }
    }
    if (nstk_top != 0) return EVAL_SYNTAX; // N?u stack s? không còn ?úng 1 ph?n t? là l?i cú pháp
    *res = n_pop();
    return EVAL_OK;
}

// Hàm g?p: Ch?y Shunting-yard r?i tính RPN
static uint8_t expr_eval(const char *s, double *res) {
    uint8_t e = shunt(s);
    if (e) return e;
    return calc_rpn(res);
}

/* ==========================================================
   PH?N 4: MÁY TR?NG THÁI (FSM) - QU?N LÝ GIAO DI?N
   ========================================================== */
typedef enum {
    ST_MAIN = 0,  // Màn hình chính
    ST_EXPR,      // Ch? ?? nh?p bi?u th?c
    ST_OPLIST,    // Menu ch?n toán t? nâng cao
    ST_QUAD       // Ch? ?? gi?i ph??ng trình b?c 2
} State;

static State state = ST_MAIN;
static uint8_t main_sel = 0; // Bi?n ch?n menu ? màn hình chính

// Buffer ch?a bi?u th?c nh?p vào
static char    expr_buf[EXPR_MAX + 1];
static uint8_t expr_len, cursor, disp_off;

// Danh sách các toán t?/hàm m? r?ng cho Menu
typedef struct { const char *label; const char *ins; } OpEntry;
static const OpEntry ops_list[] = {
    {"+","+"},   {"-","-"},   {"*","*"},   {"/","/"},
    {"(","("},   {")",")"},   {"^","^"},
    {"sqrt(","sqrt("},
    {"sin(","sin("},  {"cos(","cos("},  {"tan(","tan("},
    {"log(","log("},  {"ln(","ln("}
};
#define OPS_N  13
static uint8_t op_sel, op_scr;

// Các bi?n cho Gi?i ph??ng trình b?c 2
static uint8_t  quad_step; // 0: nh?p a, 1: nh?p b, 2: nh?p c, 3: xu?t k?t qu?
static char     coef_buf[12];
static double   qa, qb, qc;

// Hi?n th? bi?u th?c có cu?n trên LCD
static void show_expr(void) {
    if (cursor < disp_off) disp_off = cursor;
    if (cursor >= disp_off + 16) disp_off = cursor - 15;

    char tmp[17]; memset(tmp, ' ', 16); tmp[16] = '\0';
    for (uint8_t i = 0; i < 16 && (disp_off + i) < expr_len; i++)
        tmp[i] = expr_buf[disp_off + i];

    lcd_goto(0, 0); lcd_str(tmp);
    lcd_goto(0, cursor - disp_off); // C?p nh?t v? trí con tr? nh?p nháy
    lcd_cursor_on();
}

// Chèn chu?i vào v? trí con tr? hi?n t?i
static void expr_insert(const char *s) {
    uint8_t L = strlen(s);
    if (expr_len + L >= EXPR_MAX) return; // V??t quá b? nh?
    memmove(&expr_buf[cursor + L], &expr_buf[cursor], expr_len - cursor + 1);
    memcpy(&expr_buf[cursor], s, L);
    expr_len += L; cursor += L;
}

// Xóa ký t? tr??c con tr? (Backspace)
static void expr_del(void) {
    if (cursor == 0) return;
    memmove(&expr_buf[cursor-1], &expr_buf[cursor], expr_len - cursor + 1);
    expr_len--; cursor--;
}

/* ---- TR?NG THÁI: MENU CHÍNH (ST_MAIN) ---- */
static void enter_main(void) {
    lcd_clear(); lcd_cursor_off(); main_sel = 0;
    lcd_line(0, 0, ">1.Tinh bieu thuc");
    lcd_line(1, 0, " 2.Giai PT bac 2 ");
}

static void refresh_main(void) {
    lcd_line(0, 0, main_sel==0 ? ">1.Tinh bieu thuc" : " 1.Tinh bieu thuc");
    lcd_line(1, 0, main_sel==1 ? ">2.Giai PT bac 2 " : " 2.Giai PT bac 2 ");
}

static State handle_main(uint8_t k) {
    if (k=='1' || (k=='D' && main_sel==0)) return ST_EXPR; // Vào tính toán
    if (k=='2' || (k=='D' && main_sel==1)) return ST_QUAD; // Vào PT b?c 2
    if (k=='B'||k=='C') { main_sel = !main_sel; refresh_main(); } // Cu?n menu
    return ST_MAIN;
}

/* ---- TR?NG THÁI: TÍNH BI?U TH?C (ST_EXPR) ---- */
static void enter_expr(void) {
    expr_len = cursor = disp_off = 0;
    memset(expr_buf, 0, sizeof(expr_buf));
    lcd_clear();
    lcd_line(1, 0, "A:Toan #:Xoa D:=");
    show_expr();
}
static uint8_t result_shown = 0; // C? ki?m tra k?t qu? ?ã hi?n th? ch?a

static State handle_expr(uint8_t k) {
    char s[2] = {k, '\0'};

    /* Tính n?ng UX mô ph?ng máy tính Casio: D?n màn hình sau khi b?m "=" */
    if (result_shown) {
        result_shown = 0; 
        
        /* Nh?n qua trái/ph?i ?? gi? l?i bi?u th?c s?a ti?p */
        if (k == 'B' || k == 'C') {
            lcd_line(1, 0, "A:Toan #:Xoa D:=");
        } 
        /* Nh?n s? ho?c d?u khác s? làm m?i hoàn toàn */
        else {
            expr_len = cursor = disp_off = 0;
            memset(expr_buf, 0, sizeof(expr_buf));
            lcd_line(1, 0, "A:Toan #:Xoa D:=");
            
            if (k == 'D') {
                if (keypad_held()) { op_sel=0; op_scr=0; return ST_OPLIST; }
                show_expr();
                return ST_EXPR; 
            }
        }
    }

    switch(k) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            expr_insert(s); break;

        case '*': expr_insert("."); break;   /* Phím * dùng làm d?u th?p phân */

        case '#':
            if (keypad_held()) {             /* Gi? # ?? xóa t?t c? (AC) */
                expr_len = cursor = disp_off = 0;
                memset(expr_buf, 0, sizeof(expr_buf));
                lcd_line(1, 0, "  --  AC  --    ");
                _delay_ms(600);
                lcd_line(1, 0, "A:Toan #:Xoa D:=");
            } else {
                expr_del();                  /* Nh?n nh? ?? xóa 1 ký t? (DEL) */
            }
            break;

        case 'B': if(cursor>0)        cursor--;  break; // D?ch con tr? trái
        case 'C': if(cursor<expr_len) cursor++;  break; // D?ch con tr? ph?i

        case 'A': 
            if (keypad_held()) {
                return ST_MAIN;    /* Gi? phím A 2s -> Thoát v? Menu chính */
            } else {
                op_sel=0; op_scr=0; 
                return ST_OPLIST;  /* Nh?n nh? phím A -> M? Menu các phép toán */
            }

        case 'D': {
            if (keypad_held()) {             /* Gi? phím D c?ng m? Menu Toán */
                op_sel=0; op_scr=0; return ST_OPLIST;
            }
            
            lcd_cursor_off();
            double res; char out_s[17];
            uint8_t err = expr_eval(expr_buf, &res); // Tính giá tr? bi?u th?c
            
            if (err == EVAL_OK) {
                // S?a l?i sai s? siêu nh? c?a hàm pow() / ph?y ??ng trên AVR
                double res_rounded = round(res);
                double diff = fabs(res - res_rounded);
                
                if (diff < 1e-6 && fabs(res) < 2000000000.0) {
                    snprintf(out_s, 17, "=%ld", (long)res_rounded);
                } else {
                    char float_str[10];
                    dtostrf(res, 0, 4, float_str); // Chuy?n double sang string 
                    snprintf(out_s, 17, "=%s", float_str);
                }
            } else if (err == EVAL_DIV0)   { strcpy(out_s,"ERR:Chia cho 0  "); }
              else if (err == EVAL_DOMAIN) { strcpy(out_s,"ERR:Ngoai mien  "); }
              else                         { strcpy(out_s,"ERR:Cu phap     "); }
              
            lcd_line(1, 0, out_s); // In k?t qu? ra dòng 2
            
            result_shown = 1; // B?t c? báo ?ã hi?n th? k?t qu?
            lcd_cursor_on();
            break;
        }
    }
    show_expr();
    return ST_EXPR;
}

/* ---- TR?NG THÁI: MENU TOÁN T? (ST_OPLIST) ---- */
static void show_oplist(void) {
    char ln[17];
    snprintf(ln,17,"%c%-15s", op_sel==op_scr   ? '>':' ', ops_list[op_scr].label);
    lcd_line(0,0,ln);
    if (op_scr+1 < OPS_N)
         snprintf(ln,17,"%c%-15s", op_sel==op_scr+1?'>':' ', ops_list[op_scr+1].label);
    else memset(ln,' ',16), ln[16]='\0';
    lcd_line(1,0,ln);
}

static void enter_oplist(void) { lcd_cursor_off(); lcd_clear(); show_oplist(); }

static State handle_oplist(uint8_t k) {
    switch(k) {
        case 'B': // Cu?n lên
            if(op_sel>0){ op_sel--; if(op_sel<op_scr) op_scr=op_sel; } break;
        case 'C': // Cu?n xu?ng
            if(op_sel<OPS_N-1){ op_sel++; if(op_sel>=op_scr+2) op_scr=op_sel-1; } break;
        case 'D': // Ch?n l?nh
            expr_insert(ops_list[op_sel].ins); // Chèn l?nh vào bi?u th?c
            lcd_clear();
            lcd_line(1,0,"A:Toan #:Xoa D:=");
            show_expr(); lcd_cursor_on();
            return ST_EXPR; // Quay l?i màn hình tính toán
        case 'A': case '#': // H?y b?, quay l?i
            lcd_clear();
            lcd_line(1,0,"A:Toan #:Xoa D:=");
            show_expr(); lcd_cursor_on();
            return ST_EXPR;
    }
    show_oplist();
    return ST_OPLIST;
}

/* ---- TR?NG THÁI: GI?I PH??NG TRÌNH B?C 2 (ST_QUAD) ---- */
static void quad_prompt(void) {
    const char *lbl[] = {"a","b","c"};
    char top[17], bot[17];
    snprintf(top, 17, "ax2+bx+c=0");
    snprintf(bot, 17, "Nhap %s: %-7s", lbl[quad_step], coef_buf);
    lcd_line(0,0,top);
    lcd_line(1,0,bot);
    lcd_goto(1, 8 + strlen(coef_buf));
    lcd_cursor_on();
}

static void enter_quad(void) {
    quad_step = 0; qa = qb = qc = 0;
    memset(coef_buf, 0, sizeof(coef_buf));
    lcd_clear(); quad_prompt();
}

static void quad_result(void) {
    lcd_cursor_off();
    double d = qb*qb - 4*qa*qc; // Tính Delta
    char l0[17], l1[17];
    char f1[10], f2[10]; 

    if (qa == 0) { // N?u a = 0 -> Tr? thành ph??ng trình b?c 1: bx + c = 0
        if (qb == 0) {
            lcd_line(0,0,"Vo so/Vo nghiem");
            lcd_line(1,0,"");
            return;
        }
        snprintf(l0, 17, "PT bac 1:");
        dtostrf(-qc/qb, 0, 4, f1);
        snprintf(l1, 17, "x=%s", f1);
    } else if (d > 1e-9) { // Delta > 0 (2 nghi?m phân bi?t)
        dtostrf((-qb+sqrt(d))/(2*qa), 0, 4, f1);
        dtostrf((-qb-sqrt(d))/(2*qa), 0, 4, f2);
        snprintf(l0, 17, "x1=%s", f1);
        snprintf(l1, 17, "x2=%s", f2);
    } else if (d >= -1e-9) { // Delta = 0 (nghi?m kép)
        snprintf(l0, 17, "Nghiem kep:");
        dtostrf(-qb/(2*qa), 0, 4, f1);
        snprintf(l1, 17, "x=%s", f1);
    } else { // Delta < 0 (Nghi?m ph?c)
        double re = -qb/(2*qa), im = sqrt(-d)/(2*qa);
        dtostrf(re, 0, 3, f1);
        dtostrf(im, 0, 3, f2);
        snprintf(l0, 17, "%s+%si", f1, f2);
        snprintf(l1, 17, "%s-%si", f1, f2);
    }
    
    lcd_line(0,0,l0);
    lcd_line(1,0,l1);
}

static State handle_quad(uint8_t k) {
    if (k=='A') return ST_MAIN; // Thoát ra menu chính

    if (quad_step == 3) {               /* ?ang hi?n k?t qu? ? b??c 3, nh?n phím b?t k? s? tính l?i t? ??u */
        quad_step = 0;
        memset(coef_buf,0,sizeof(coef_buf));
        lcd_clear(); quad_prompt();
        return ST_QUAD;
    }

    uint8_t L = strlen(coef_buf);
    if (k>='0'&&k<='9') {
        if(L<10){ coef_buf[L]=k; coef_buf[L+1]='\0'; }
    } else if (k=='*' && !strchr(coef_buf,'.')) { // Phím * t??ng ???ng d?u th?p phân
        if(L<10){ coef_buf[L]='.'; coef_buf[L+1]='\0'; }
    } else if (k=='#') { // Phím # t??ng ???ng Backspace
        if(L>0){ coef_buf[L-1]='\0'; }
        if(strcmp(coef_buf,"-")==0) coef_buf[0]='\0'; // N?u ch? còn d?u '-' thì xóa n?t
    } else if (k=='B' && L==0) {        // Nh?n phím B ?? nh?p d?u âm
        coef_buf[0]='-'; coef_buf[1]='\0';
    } else if (k=='D' && L>0) {         // Phím D (Enter) ch?t h? s?
        double val = atof(coef_buf);
        if(quad_step==0) qa=val;
        else if(quad_step==1) qb=val;
        else qc=val;
        
        memset(coef_buf,0,sizeof(coef_buf));
        quad_step++;
        if(quad_step==3) { quad_result(); return ST_QUAD; } // ?ã nh?p xong a,b,c -> gi?i
    }
    quad_prompt();
    return ST_QUAD;
}

/* ==========================================================
   PH?N 5: MAIN (VÒNG L?P CHÍNH)
   ========================================================== */
int main(void) {
    lcd_init();
    keypad_init();

    /* Hi?n th? Màn hình chào */
    lcd_clear();
    lcd_line(0,0,"  CALCULATOR    ");
    lcd_line(1,0," ATmega324P FSM ");
    _delay_ms(2000);

    enter_main(); // M? Menu chính

    while (1) {
        uint8_t k = keypad_get(); // ??c phím có ch?ng d?i
        State old_state = state;  // L?u l?i tr?ng thái màn hình c?
        State next = state;

        // Phân b? s? ki?n phím vào các Tr?ng thái x? lý t??ng ?ng
        switch (state) {
            case ST_MAIN:   next = handle_main(k);   break;
            case ST_EXPR:   next = handle_expr(k);   break;
            case ST_OPLIST: next = handle_oplist(k); break;
            case ST_QUAD:   next = handle_quad(k);   break;
        }

        // N?u tr?ng thái có s? thay ??i (chuy?n màn hình)
        if (next != state) {
            state = next;
            switch (state) {
                case ST_MAIN:   
                    enter_main();   
                    break;
                case ST_EXPR:   
                    if (old_state == ST_MAIN) {
                        /* Ch? xóa tr?ng b? nh? khi m?i ch?n t? Main Menu vào */
                        enter_expr(); 
                    } else {
                        /* C?p nh?t l?i giao di?n (không xóa bi?u th?c) khi t? Menu Phép toán quay v? */
                        lcd_clear();
                        lcd_line(1,0,"A:Toan #:Xoa D:=");
                        show_expr(); 
                    }
                    break;
                case ST_OPLIST: 
                    enter_oplist(); 
                    break;
                case ST_QUAD:   
                    if (old_state == ST_MAIN) enter_quad(); 
                    break;
            }
        }
    }
}