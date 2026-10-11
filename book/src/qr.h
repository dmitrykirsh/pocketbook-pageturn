/* Компактный генератор QR-кодов: байтовый режим, коррекция ошибок M, версии 1..6 (до 106 байт). */
#ifndef QR_H
#define QR_H
#include <stdint.h>

#define QR_MAX_SIZE 41   /* 17 + 4*6 */

/* Кодирует text. Результат: out[y * size + x] = 1 (тёмный) / 0 (светлый).
 * Возвращает размер стороны (21..41) или 0, если текст не помещается. */
int qr_encode(const char *text, uint8_t out[QR_MAX_SIZE * QR_MAX_SIZE]);

#endif
