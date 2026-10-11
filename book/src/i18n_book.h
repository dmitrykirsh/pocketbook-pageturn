/* Тексты экрана PageTurn на книге. Язык берётся из global.cfg (language=xx); по умолчанию en. */
#ifndef I18N_BOOK_H
#define I18N_BOOK_H

enum {
    B_TITLE, B_SERVER_OK, B_SERVER_DOWN, B_WIFI_FMT, B_WIFI_NONE, B_PORT_FMT, B_PIN_LABEL,
    B_BTN_CHANGE, B_BTN_RANDOM, B_BTN_REFRESH, B_BTN_CLOSE, B_KB_TITLE, B_ERR_PIN, B_SAVED,
    B_HINT1, B_HINT2, B_BTN_QR, B_BTN_BACK, B_QR_HINT, B_COUNT
};

typedef struct {
    const char *code;
    const char *s[B_COUNT];
} book_lang_t;

static const book_lang_t BOOK_LANGS[] = {
    { "en", {
        "PageTurn", "Server is running", "Server is not running", "Wi-Fi address: %s",
        "Wi-Fi is not connected", "Port: %d", "PIN",
        "Change PIN", "Random PIN", "Refresh", "Close",
        "New PIN (4-8 digits)", "The PIN must be 4-8 digits", "PIN saved",
        "On your phone: open PageTurn, tap \"Find my book\" and enter this PIN.",
        "The server keeps running in the background after you close this screen.",
        "QR code", "Back", "On your phone, open PageTurn and tap \"Scan QR\"." } },
    { "ru", {
        "PageTurn", "Сервер работает", "Сервер не запущен", "Адрес Wi-Fi: %s",
        "Wi-Fi не подключён", "Порт: %d", "PIN-код",
        "Сменить PIN", "Случайный PIN", "Обновить", "Закрыть",
        "Новый PIN (4-8 цифр)", "PIN должен состоять из 4-8 цифр", "PIN сохранён",
        "На телефоне: откройте PageTurn, нажмите «Найти мою книгу» и введите этот PIN.",
        "После закрытия этого экрана сервер продолжает работать в фоне.",
        "QR-код", "Назад", "На телефоне откройте PageTurn и нажмите «Сканировать QR»." } },
    { "uk", {
        "PageTurn", "Сервер працює", "Сервер не запущено", "Адреса Wi-Fi: %s",
        "Wi-Fi не підключено", "Порт: %d", "PIN-код",
        "Змінити PIN", "Випадковий PIN", "Оновити", "Закрити",
        "Новий PIN (4-8 цифр)", "PIN має складатися з 4-8 цифр", "PIN збережено",
        "На телефоні: відкрийте PageTurn, натисніть «Знайти мою книгу» і введіть цей PIN.",
        "Після закриття цього екрана сервер працює у фоні.",
        "QR-код", "Назад", "На телефоні відкрийте PageTurn і натисніть «Сканувати QR»." } },
    { "de", {
        "PageTurn", "Server läuft", "Server läuft nicht", "WLAN-Adresse: %s",
        "WLAN nicht verbunden", "Port: %d", "PIN",
        "PIN ändern", "Zufällige PIN", "Aktualisieren", "Schließen",
        "Neue PIN (4-8 Ziffern)", "Die PIN muss aus 4-8 Ziffern bestehen", "PIN gespeichert",
        "Auf dem Telefon: PageTurn öffnen, „Mein Buch suchen“ tippen und diese PIN eingeben.",
        "Der Server läuft im Hintergrund weiter, wenn Sie diesen Bildschirm schließen.",
        "QR-Code", "Zurück", "Auf dem Telefon PageTurn öffnen und „QR scannen“ tippen." } },
    { "fr", {
        "PageTurn", "Le serveur fonctionne", "Le serveur ne fonctionne pas", "Adresse Wi-Fi : %s",
        "Wi-Fi non connecté", "Port : %d", "Code PIN",
        "Changer le PIN", "PIN aléatoire", "Actualiser", "Fermer",
        "Nouveau PIN (4 à 8 chiffres)", "Le PIN doit comporter 4 à 8 chiffres", "PIN enregistré",
        "Sur le téléphone : ouvrez PageTurn, appuyez sur « Trouver ma liseuse » et saisissez ce PIN.",
        "Le serveur continue de fonctionner en arrière-plan après la fermeture de cet écran.",
        "Code QR", "Retour", "Sur le téléphone, ouvrez PageTurn et appuyez sur « Scanner le QR »." } },
    { "es", {
        "PageTurn", "El servidor está en marcha", "El servidor no está en marcha", "Dirección Wi-Fi: %s",
        "Wi-Fi no conectado", "Puerto: %d", "PIN",
        "Cambiar PIN", "PIN aleatorio", "Actualizar", "Cerrar",
        "Nuevo PIN (4-8 dígitos)", "El PIN debe tener de 4 a 8 dígitos", "PIN guardado",
        "En el teléfono: abra PageTurn, pulse «Buscar mi lector» e introduzca este PIN.",
        "El servidor sigue funcionando en segundo plano al cerrar esta pantalla.",
        "Código QR", "Atrás", "En el teléfono, abra PageTurn y pulse «Escanear QR»." } },
    { "it", {
        "PageTurn", "Il server è in esecuzione", "Il server non è in esecuzione", "Indirizzo Wi-Fi: %s",
        "Wi-Fi non connesso", "Porta: %d", "PIN",
        "Cambia PIN", "PIN casuale", "Aggiorna", "Chiudi",
        "Nuovo PIN (4-8 cifre)", "Il PIN deve avere da 4 a 8 cifre", "PIN salvato",
        "Sul telefono: apri PageTurn, tocca «Trova il mio lettore» e inserisci questo PIN.",
        "Il server continua a funzionare in background dopo aver chiuso questa schermata.",
        "Codice QR", "Indietro", "Sul telefono apri PageTurn e tocca «Scansiona QR»." } },
};

#define BOOK_LANG_COUNT ((int)(sizeof(BOOK_LANGS) / sizeof(BOOK_LANGS[0])))

#endif
