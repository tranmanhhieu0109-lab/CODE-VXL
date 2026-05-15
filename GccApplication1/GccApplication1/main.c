#define F_CPU 8000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// ==========================================
// C?U HÌNH LCD 16x2 (Ch? ?? 4-bit qua Port B)
// ==========================================
#define RS 0
#define RW 1
#define EN 2

void lcd_pulse() {
	PORTB |= (1 << EN);   // Kích EN lên 1
	_delay_us(10);
	PORTB &= ~(1 << EN);  // Kích EN v? 0 (S??n xu?ng ?? ch?t d? li?u)
	_delay_ms(2);
}

void lcd_send_nibble(unsigned char n) {
	PORTB = (PORTB & 0x0F) | (n & 0xF0);
	lcd_pulse();
}

void lcd_command(unsigned char cmnd) {
	PORTB &= ~(1 << RS);
	PORTB &= ~(1 << RW);
	lcd_send_nibble(cmnd);
	lcd_send_nibble(cmnd << 4);
}

void lcd_data(unsigned char data) {
	PORTB |= (1 << RS);
	PORTB &= ~(1 << RW);
	lcd_send_nibble(data);
	lcd_send_nibble(data << 4);
}

void lcd_init() {
	DDRB = 0xFF; // Port B làm Output
	_delay_ms(50);
	lcd_command(0x02); // Kh?i t?o 4-bit
	lcd_command(0x28); // 2 dòng, font 5x7
	lcd_command(0x0C); // Hi?n màn hình, t?t con tr?
	lcd_command(0x01); // Xóa màn hình
}

void lcd_print(char *str) {
	while (*str) lcd_data(*str++);
}

// ==========================================
// LOGIC QUÉT PHÍM MA TR?N T?I ?U (Port A)
// ==========================================
char get_key() {
	const char keypad[4][4] = {
		{'C', '/', '*', '-'}, // Hàng 0
		{'8', '9', '.', '+'}, // Hàng 1
		{'4', '5', '6', '7'}, // Hàng 2
		{'0', '1', '2', '3'}  // Hàng 3
	};

	for (int col = 0; col < 4; col++) {
		// Xu?t m?c 0 ra c?t t??ng ?ng, gi? m?c 1 cho các hàng (Pull-up)
		PORTA = ~(1 << (col + 4)) | 0x0F;
		_delay_us(10);

		uint8_t row_val = PINA & 0x0F;

		if (row_val != 0x0F) { // N?u có hàng nào ?ó b?ng 0
			_delay_ms(20); // Ch?ng rung
			row_val = PINA & 0x0F;
			
			if (row_val != 0x0F) {
				int row = -1;
				if      (!(row_val & (1 << 0))) row = 0; // Hàng 0 nh?n
				else if (!(row_val & (1 << 1))) row = 1; // Hàng 1 nh?n
				else if (!(row_val & (1 << 2))) row = 2; // Hàng 2 nh?n
				else if (!(row_val & (1 << 3))) row = 3; // Hàng 3 nh?n

				if (row != -1) {
					// Ch? nh? phím (Có thêm delay nh? ?? không b? treo Proteus)
					while ((PINA & 0x0F) != 0x0F) {
						_delay_ms(1);
					}
					return keypad[row][col]; // Tr? v? ký t?
				}
			}
		}
	}
	return '\0'; // Không nh?n phím nào
}

// ==========================================
// CH??NG TRÌNH CHÍNH
// ==========================================
int main() {
	// --- Kh?i t?o I/O ---
	DDRA = 0xF0; PORTA = 0xFF; // PA4-7 Output (C?t), PA0-3 Input Pull-up (Hàng)
	DDRD &= ~0x07; PORTD |= 0x07; // Các nút r?i trên Port D (PD0, PD1, PD2 Input Pull-up)
	
	// --- Kh?i t?o Màn hình ---
	lcd_init();
	lcd_print("Dien Tu 10 Diem");
	_delay_ms(1000);
	lcd_command(0x01);

	// --- Bi?n toàn c?c tính toán ---
	char buffer[16] = "";
	double num1 = 0, num2 = 0, result = 0;
	char op = 0;
	int b_idx = 0;
	int calc_done = 0; // C? báo hi?u: 1 là v?a tính xong, 0 là bình th??ng

	while (1) {
		char key = get_key(); // L?y ký t? t? bàn phím
		
		// --- 1. X? LÝ BÀN PHÍM MA TR?N ---
		if (key != '\0') {
			if (key >= '0' && key <= '9') {
				if (calc_done) { // ?ang có k?t qu? c? mà b?m s? -> Tính phép m?i
					lcd_command(0x01);
					b_idx = 0; buffer[0] = '\0';
					calc_done = 0;
				}
				buffer[b_idx++] = key; buffer[b_idx] = '\0';
				lcd_data(key);
			}
			else if (key == '.') {
				if (calc_done) {
					lcd_command(0x01);
					b_idx = 0; buffer[0] = '\0';
					calc_done = 0;
				}
				buffer[b_idx++] = '.'; buffer[b_idx] = '\0';
				lcd_data('.');
			}
			else if (key == 'C') { // Xóa tr?ng
				lcd_command(0x01);
				b_idx = 0; buffer[0] = '\0'; num1 = 0; op = 0; result = 0;
				calc_done = 0;
			}
			else { // Phép tính (+, -, *, /)
				if (calc_done) {
					num1 = result; // L?y k?t qu? v?a tính làm s? th? 1
					calc_done = 0;
					} else {
					num1 = atof(buffer); // Tính bình th??ng
				}
				
				op = key;
				lcd_command(0x01);
				
				char float_str[10];
				dtostrf(num1, 4, 1, float_str);
				
				char disp[16];
				sprintf(disp, "%s%c", float_str, op);
				lcd_print(disp);
				
				b_idx = 0; buffer[0] = '\0';
			}
		}

		// --- 2. NÚT D?U B?NG "=" (Chân PD0) ---
		if (!(PIND & (1 << PD0))) {
			_delay_ms(20); while (!(PIND & (1 << PD0)));
			num2 = atof(buffer);
			
			if (op == '+') result = num1 + num2;
			else if (op == '-') result = num1 - num2;
			else if (op == '*') result = num1 * num2;
			else if (op == '/') result = (num2 != 0) ? (num1 / num2) : 0;
			else if (op == 'p') result = pow(num1, num2);

			char float_str[16];
			dtostrf(result, 4, 2, float_str);

			char res[20];
			sprintf(res, "=%s", float_str);
			
			lcd_command(0xC0); // Xu?ng dòng 2
			lcd_print(res);
			b_idx = 0; buffer[0] = '\0';
			calc_done = 1; // ?ánh d?u ?ã có k?t qu?
		}

		// --- 3. NÚT C?N B?C 2 (Chân PD1) ---
		if (!(PIND & (1 << PD1))) {
			_delay_ms(20); while (!(PIND & (1 << PD1)));
			
			if (calc_done) {
				num1 = result; // L?y k?t qu? tr??c ?ó ?i tính c?n luôn
				} else {
				num1 = atof(buffer);
			}
			
			result = sqrt(num1);
			lcd_command(0x01);
			
			char str_n1[10], str_res[10];
			dtostrf(num1, 4, 1, str_n1);
			dtostrf(result, 4, 2, str_res);
			
			char res[20];
			sprintf(res, "v(%s)=%s", str_n1, str_res);
			lcd_print(res);
			
			b_idx = 0; buffer[0] = '\0';
			calc_done = 1; // ?ánh d?u ?ã có k?t qu?
		}

		// --- 4. NÚT L?Y TH?A x^y (Chân PD2) ---
		if (!(PIND & (1 << PD2))) {
			_delay_ms(20); while (!(PIND & (1 << PD2)));
			
			if (calc_done) {
				num1 = result; // L?y k?t qu? tr??c làm c? s?
				calc_done = 0;
				
				lcd_command(0x01);
				char str_n1[10];
				dtostrf(num1, 4, 1, str_n1);
				
				char res[16];
				sprintf(res, "%s^", str_n1);
				lcd_print(res);
				} else {
				num1 = atof(buffer);
				lcd_data('^');
			}
			
			op = 'p';
			b_idx = 0; buffer[0] = '\0';
		}
	}
}