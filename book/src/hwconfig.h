/* Заглушка hwconfig.h из SDK: нужен только enum input_dev_e (закомментирован в inkview.h). */
#ifndef HWCONFIG_STUB_H
#define HWCONFIG_STUB_H
enum input_dev_e {
    TP_UNKNOWN = 0,
    TP_CAPACITIVE,
    TP_DIGITIZER,
    MAX_INPUT_TOUCH_DEV,
    KEYBOARD_HW,
    KEYBOARD_BT,
    KEYBOARD_USB,
};
#endif
