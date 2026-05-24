#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <MPU6050.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// definitii pini

// chip select sd
#define PIN_CS_SD 4
// pini butoane
#define PIN_BTN_NXT 5
#define PIN_BTN_PRV 6
#define PIN_BTN_MNU 7

#define PIN_BUZZER 2

// chip select lcd
#define PIN_LCD_CS 10

// pini control lcd
#define PIN_LCD_DC 9
#define PIN_LCD_RST 8

#define DEBOUNCE_MS 200
// nr maxim de carti, important pentru ca avem spatiu limitat
#define MAX_BOOKS 10 
// formatul fat permite fisiere cu formatul 8_caractere_nume  +  .  +  extensie_3  +  terminator
#define MAX_FILENAME 13
// .dat e o extensie folosita pentru fisiere care nu sunt destinate userului
#define LAST_BOOK_FILE "LAST.DAT"
// cat de des se verifica orientarea in ms
#define ORIENT_INTERVAL 500


// !!!! Pentru text am folosit F, pentru a le muta din RAM in Flash, ca sa am spatiu

// Constante ecran
// Portret: 128x128, Landscape: 128x128 <- outdated, de la fostul ecran, acum am 128x160
// Acestea sunt pentru portret — se recalculeaza la rotatie, in functie de ecran, dar la actualul ecran nu e cazul
// dimensiuni caractere
// dimensiunile standard ale carcterelor in Adafruit
#define CHAR_W 6
#define CHAR_H 8
// inaltimea liniei, 8px caracterul + 2 px padding
#define LINE_H 10
// unde incepe textul
#define HEADER_Y 0
// unde incepe textul efectv, lasand loc pentru linia de header  +  un spatiu suplimentar
#define TEXT_Y 12

// fisierele
// am ales dimensiuile astfel incat sa nu ocupe prea mult spatiu in ram
char books[MAX_BOOKS][MAX_FILENAME];
// constante carte
uint8_t bookCount = 0;
uint8_t currentBook = 0;
uint32_t currentPage = 0;
uint8_t menuCursor = 0;
uint8_t currentRotation = 255;  // valoare imposibila ca sa nu fie confundata cu vreo stare anterioara inexistenta

// cele 2 moduri de functionare
enum Mode { 
  MODE_MENU, 
  MODE_READ 
};
// la startup
Mode mode = MODE_MENU;

// variabile debounce pentru butoane si orientare
// unsigned long, pentru ca pe int ar face overflow(lucram cu milisecunde)
unsigned long lastNxt = 0, lastPrv = 0, lastMnu = 0, lastOrient = 0;

// instantieri
Adafruit_ST7735 tft = Adafruit_ST7735(PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST); // instantiere ecran cu pinii de control
// nu e nevoie de parametrii, pentru ca el default e cu ad0 la gnd
// primii 6 biti ai adresei sunt hardcodati pentru mpu, iar ultimul e definit de ad0  - > deci poate fi pus la gnd sau la vcc pentru diferentierea a 2 module
MPU6050 mpu; 

// functii buzzer pentru diferitele actiuni

// delay dupa tone pentru ca tone e asincron si nu vrem sa executam urmatorul pas inaintea finalizarii feedback-ului sonor
void beepOK() { 
  // 2 sunete scurt unul mai jos, unul mai sus
  // bip de frecventa 1000Hz, 80 ms
  tone(PIN_BUZZER, 1000, 80);
  delay(120);
  tone(PIN_BUZZER, 1200, 80);
  delay(80); 
}
void beepError() { 
  // un sunet scurt si jos
  tone(PIN_BUZZER, 300, 400);
  delay(400); 
}
void beepMenu()  { 
  // sunet neutru
  tone(PIN_BUZZER, 700, 150);
  delay(150); 
}

// functii pentru recalcularea dimensiunilor ecranului, nu mai e cazul la ecranul patrat
uint8_t screenW()  { 
  return (currentRotation == 1 || currentRotation == 3) ? 160 : 128; 
}
uint8_t screenH()  { 
  return (currentRotation == 1 || currentRotation == 3) ? 128 : 160; 
}
uint8_t lcdCols()  { 
  return screenW() / CHAR_W; 
}         // portret=21, landscape=26
uint8_t lcdRows()  { 
  // scad din inaltimea totala headerul si las satiu pentru footer
  return (screenH() - TEXT_Y - LINE_H * 2) / LINE_H; 
}  // portret=13, landscape=8
uint8_t footerY()  { 
  return screenH() - LINE_H - 2; 
}     // portret=148, landscape=116
uint16_t charsPerPage() { 
  return (uint16_t)lcdCols() * lcdRows(); 
}

//     functii card sd

bool endsWith_TXT(const char* name) {
  // verifica daca functia se termina in txt
  uint8_t len = strlen(name);
  if (len < 4) 
    return false; // nu are sens
  return (name[len - 4] ==  '.' &&
          (name[len - 3] ==  'T' || name[len - 3] ==  't') &&
          (name[len - 2] ==  'X' || name[len - 2] ==  'x') &&
          (name[len - 1] ==  'T' || name[len - 1] ==  't'));
}

void progressFilename(uint8_t idx, char* out) {
  // genereaza numele fisierului de progres pentru o carte
  // cartea 0  = > P0.DAT

  // am scris manual caracterele in loc de sprintf pentru a nu fi nevoie de biblioteca
  out[0] = 'P';
  if (idx < 10) { // diferit daca e o cifra sau un numar
    out[1] = '0' + idx; // adun '0' ca sa obtin ascii-ul
    out[2] = '.';
    out[3] = 'D'; 
    out[4] = 'A'; 
    out[5] = 'T'; 
    out[6] = '\0';
  } else {
    out[1] = '0' + (idx / 10); 
    out[2] = '0' + (idx % 10); 
    out[3] = '.';
    out[4] = 'D'; 
    out[5] = 'A'; 
    out[6] = 'T'; 
    out[7] = '\0';
  }
}

void scanBooks() {
  // cauta pe cardul SD cartile
  bookCount = 0;
  File root = SD.open("/");
  // by design lucrez exclusiv in radacina, deci doar aici trebuie sa caut
  while (bookCount < MAX_BOOKS) { // chiar daca sunt mai mult de 10 carti, nu le citesc
    File entry = root.openNextFile(); // functie care imi returneaza automat urmatorul fisier
    if (!entry) 
      break;
    if (entry.isDirectory()) { // by design lucrez doar in root
      entry.close(); 
      continue; 
    }
    // iau numele
    const char* name = entry.name();
    // verific daca are extentia .txt
    if (endsWith_TXT(name)) {
      // copiez cartea
      strncpy(books[bookCount], name, MAX_FILENAME);
      // strncpy nu garanteaza terminatorul de sir
      books[bookCount][MAX_FILENAME - 1] = '\0';
      bookCount++;
    }
    entry.close();
  }
  root.close();
}

uint32_t pageOffset(uint8_t idx, uint32_t page) {
  // calculeaza offsetul la care se afla pagina ceruta intr-o carte
  if (page == 0) 
    return 0;
  // deschid fisierul de pe card
  File f = SD.open(books[idx]);
  if (!f) 
    return 0;
  // numarul de caractere de pe pagina curenta, numarul paginii curente si offsetul ultimei pagini gasite in memorie
  uint32_t charsOnPage = 0, currentPg = 0, offset = 0;
  uint16_t cpp = charsPerPage(); // functie definita mai sus
  // parcurg cat timp mai sunt bytes de citit si si inca nu sunt la pagina cautata, practic parcurg pana in punctul curent
  while (f.available() && currentPg < page) {
    // citesc cate un caracter
    char c = f.read();
    // incrementez nr de caractere pe pagina asta
    charsOnPage++;
    if (charsOnPage >= cpp) {
      // cand trec de maximul pe pagina
      // astept sa apara un separator de cuvant sau de paragrafe sau sa se termine cartea inainte de a trece la urmatoarea pagina
      if (c ==  ' ' || c ==  '\n' || c ==  '\r' || !f.available()) {
        // de unde incepe urmatorul cuvant de dupa separator
        offset = f.position(); 
        // bug gasit foarte tariu, aici se pot pierde cateva litere, pentru ca se reseteaza charsOnPage cand se trece la urmatoarea apgina, desi logic acel cuvant ar trebui luat in calcul
        charsOnPage = 0; 
        currentPg++;
      }
    }
  }
  f.close();
  return offset;
}

uint32_t totalPages(uint8_t idx) {
  // calculeaza nr total de pagini pentru a afisa nr pagini / nr total
  // deschide cartea
  File f = SD.open(books[idx]);
  if (!f) 
    return 0;
  uint32_t pages = 1, charsOnPage = 0;
  uint16_t cpp = charsPerPage();
  while (f.available()) {
    // similar cu functia anterioara
    char c = f.read();
    charsOnPage++;
    if (charsOnPage >= cpp) {
      if (c ==  ' ' || c ==  '\n' || c ==  '\r' || !f.available()) {
        // acelasi bug ca mai sus
        charsOnPage = 0;
        if (f.available()) 
          pages++;
      }
    }
  }
  f.close();
  return pages;
}

void saveBookProgress(uint8_t idx, uint32_t page) {
  // salveaza propriu-zis in fisierul generat de progressFilename
  char fname[9]; // suficient pentru pana la 100 de fisiere de progres, mult mai mult decat voi avea vreodata
  progressFilename(idx, fname);
  // sterg progresul anterior pentru a nu se crea conflicte
  if (SD.exists(fname)) 
    // suprascriu fisierul existent
    SD.remove(fname);
  File f = SD.open(fname, FILE_WRITE);
  // deschid un fisier nou pentru a scrie in el si scriu doar numarul paginii
  if (!f) 
    return;
  f.println(page);
  f.close();
}

uint32_t loadBookProgress(uint8_t idx) {
  // la deschiderea cartii vreau sa aflu cat am citit din ea
  char fname[9]; 
  // generez fisierul ca sa stiu ce caut
  progressFilename(idx, fname);
  // verific ca exista fisierul si il deschid
  if (!SD.exists(fname)) 
    return 0;
  File f = SD.open(fname);
  if (!f) 
    return 0;
  uint32_t page = 0;
  while (f.available()) {
    // citesc cifrele si inmultesc cu 10 pentru a construi numarul
    char c = f.read();
    if (c < '0' || c > '9') 
      break;
    page = page * 10  +  (c  -  '0');
  }
  f.close();
  return page;
}

void saveLastBook(uint8_t idx) {
  // la pornire se deschide ultima carte deschisa, deci trebuie sa o salvam
  if (SD.exists(LAST_BOOK_FILE)) 
    // il sterg daca exista ca sa il pot suprascrie
    SD.remove(LAST_BOOK_FILE);
  File f = SD.open(LAST_BOOK_FILE, FILE_WRITE);
  // il deschi pentru scriere
  if (!f) 
    return;
  f.write(idx); 
  f.close();
}

void loadLastBook() {
  // incarca ultima carte deschisa si pagina respectiva
  if (!SD.exists(LAST_BOOK_FILE)) 
    return;
  File f = SD.open(LAST_BOOK_FILE);
  if (!f || f.size() < 1) { 
    if(f) 
      f.close(); 
    return; 
  }
  // citesc indexul salbat
  uint8_t idx = f.read(); 
  f.close();
  if (idx >= bookCount) 
    // verific sa nu depasesc bookCount
    return;
  // restabilesc variabilele
  currentBook = idx; 
  menuCursor = idx;
  // aflu pagina actuala
  currentPage = loadBookProgress(idx);
  // trec in modul de citit
  mode = MODE_READ;
}

// fucntii display
void displayMenu() {
  // fill negru inainte de a desena
  tft.fillScreen(ST77XX_BLACK);
  
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(0, 0);
  tft.println(F("====MENIU===="));

  for (uint8_t i = 0; i < bookCount; i++ ) {
    // parcurg cartile
    tft.setCursor(0, 10  +  i * LINE_H);
    // 10 pixeli pentru titlul meniului + spatiu pentru cartile anterioare
    // cartea selectata e galbena
    if (i == menuCursor) 
      tft.setTextColor(ST77XX_YELLOW);
    else
      tft.setTextColor(ST77XX_WHITE);
    // printez si un cursor
    if (i == menuCursor) {
      tft.print(F(">"));
    } else {
      tft.print(F(" "));
    } 
    if (i == currentBook) 
      // pentru cartea din care userul tocmai a iesit
      tft.setTextColor(ST77XX_GREEN);
    tft.print(books[i]);
  }

  // Footer orientare
  tft.fillRect(0, footerY(), screenW(), LINE_H, ST77XX_BLACK);
  // fill cu negru doar la zona de subsol
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(0, footerY());
  tft.print(F("rot:"));
  tft.print(currentRotation);
}

// pentru fiecare pagina: header = nume fisier  +  pagina curenta / total
// in subsol => orientarea
void displayPage(uint8_t idx, uint32_t page) {
  // ca sa afisez progresul
  uint32_t total = totalPages(idx);
  // e important sa calculez nr total de pagini inainte de a deschide fisierul, pentru ca SD.h nu poate deschide decat un singur fisier deodata
  // deci daca intai as deschide fisierul si apoi as apela functia, a doua deschidere ar da eroare
  File f = SD.open(books[idx]);
  if (!f) 
    return;
  // calculez offsetul paginii
  uint32_t offset = pageOffset(idx, page);
  // merg la respectivul offset
  f.seek(offset);

  tft.fillScreen(ST77XX_BLACK);
  // fill negru

  // Header
  tft.setTextColor(ST77XX_CYAN);
  // in header scriu numele fisierului si pagina curenta
  tft.setCursor(0, HEADER_Y);
  tft.print(books[idx]);
  tft.print(F(" "));
  // paginile sunt indexate de la 0
  tft.print(page + 1);
  tft.print(F("/"));
  tft.print(total);

  // textul paginii
  tft.setTextColor(ST77XX_WHITE);
  // variabilele pentru unde sunt si care sunt limitele
  uint8_t col = 0, row = 0;
  uint8_t cols = lcdCols();
  uint8_t rows = lcdRows();
  // de unde incepe textul
  tft.setCursor(0, TEXT_Y);

  while (f.available() && row < rows) {
    char c = f.read();
    if (c ==  '\r') // poate sa apara la randuri noi, mai ales pentru fisierele de pe windows, dar il ignor
      continue;
    if (c ==  '\n') {
      // la final de rand, trec la urmatorul row si trec la inceputul coloanei
      // aici pot sa fac un offset pentru alineat, dar am preferat sa economisesc spatiu
      row++; 
      col = 0;
      // trec la urmatoarea pagina
      tft.setCursor(0, TEXT_Y + row * LINE_H);
      continue;
    }
    // pastrez alineatul
    if (c ==  ' ' && col ==  0) {
      tft.print(" "); 
      col++ ; 
      continue;
    }
    tft.print(c);
    col++;
    if (col >=  cols) {
      // pentru despartirea in silabe, verific daca mai sunt caractere in fisier
      // apoi verific daca urmatorul e spatiu sau daca e \n, altfel pun cratima
      // si trec pe urmatorul rand
      if (f.available() && f.peek() != ' ' && f.peek() != '\n')
        tft.print("-");
      row++; 
      col = 0;
      tft.setCursor(0, TEXT_Y + row * LINE_H);
    }
  }

  // Footer orientare
  // fill cu negru doar in subsol
  tft.fillRect(0, footerY(), screenW(), LINE_H, ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(0, footerY());
  tft.print(F("rot:"));
  tft.print(currentRotation);

  f.close();
}

// autorotate cu histerezis = asteapra 3 detectii consecutive pentru a schimba orientarea
uint8_t orientCount = 0;
// valoarea orientarii inainte sa se confirme, adica inainte de a 3a citire consecutiva
uint8_t orientPending = 255;

void checkOrientation() {
  // senzorul mpu6050 masoarea forta gravitationala pe cele 3 axe, deci il pot folosi pentru a "gasi" directia in jos
  // valori intre  - 16384 si 16384
  // citesc si accelerometrul si giroscopul, dar nu ma intereseaza decat valorile accelerometrului
  int16_t aX, aY, aZ, gX, gY, gZ;
  mpu.getMotion6(&aX, &aY, &aZ, &gX, &gY, &gZ);

  // calculez valorile absolute pentru ca axa cu cea mai mare valoare e cea pe care trage gravitatia cel mai tare, deci aia e directia spre jos
  int16_t absX = abs(aX);
  int16_t absY = abs(aY);
  int16_t absZ = abs(aZ);

  // Serial.print(F("aX=")); 
  // Serial.print(aX);
  // Serial.print(F(" aY=")); 
  // Serial.print(aY);
  // Serial.print(F(" aZ=")); 
  // Serial.println(aZ);

  // am nevoie sa stiu orientarea precedenta ca sa stiu daca e mnevoie sa modific ceva
  uint8_t detected = currentRotation;

  // valori alese empiric
  if (absX > absY && absX > 8000) {
    // X cel mai mare => landscape
    if (aX > 0) 
      detected = 1;  // landscape stanga
    else
      detected = 3;  // landscape dreapta
  } else if (absY > 4000 || absX < 4000) {
    // Y sau pozitie verticala = portret
    if (aZ < 0) 
      detected = 0;  // portret normal
    else
      detected = 2;  // portret inversat
  }

  if (detected  == currentRotation) {
    // nu modific nimic
    orientPending = 255;
    orientCount = 0;
    return;
  }

  if (detected == orientPending) {
    // daca e orientarea care deja e pending, incrementez
    orientCount++;
  } else {
    // altfel asta e prima citire a acestei orientari, deci o trec in pending
    orientPending = detected;
    orientCount = 1;
  }

  // am citit de 3 ori deja, deci rotesc ecranul
  if (orientCount >=  3) {
    currentRotation = orientPending;
    orientPending = 255;
    orientCount = 0;
    tft.setRotation(currentRotation);
    // Redeseneaza cu aceleasi dimensiuni, doar orientarea ecranului se schimba
    if (mode == MODE_READ) 
      displayPage(currentBook, currentPage);
    else                  
      displayMenu();
  }
}

// setup
void setup() {
  Serial.begin(9600);

  // activez pullup pentru butoane
  pinMode(PIN_BTN_NXT, INPUT_PULLUP);
  pinMode(PIN_BTN_PRV, INPUT_PULLUP);
  pinMode(PIN_BTN_MNU, INPUT_PULLUP);
  // pinul de buzzer e pus pe output, va fi controlat prin pwm
  pinMode(PIN_BUZZER, OUTPUT);

  // pun pinii de CS pe output si ii pun pe high pentru a initializa corect spi
  pinMode(PIN_LCD_CS, OUTPUT); 
  digitalWrite(PIN_LCD_CS, HIGH);

  pinMode(PIN_CS_SD, OUTPUT); 
  digitalWrite(PIN_CS_SD, HIGH);

  // spi pentru SD
  // face initializarile spi, seteaza viteza si formatul transferului, trimite comenzile de initializare cardului si verifica daca e formatat bine
  // apoi returneaza eroare sau succes
  if (!SD.begin(PIN_CS_SD)) {
    Serial.println(F("SD EROARE!"));
    beepError();
    while(true);
  }
  Serial.println(F("SD OK!"));

  // LCD dupa SD
  // tft.initR(INITR_144GREENTAB); // specific pentru fostul ecran, l-am ales din exemplele de la Adafruit
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  currentRotation = 0;
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  // il dezactivez pentru ca il gestionez eu
  tft.setTextWrap(false);

  // initializare MPU prin I2C
  Wire.begin(); // nu e nevoie de parametrii, pinii a4 ai a5 sunt cei default pentru i2c
  // se trimit comenzile de initializare ale comunicatiei prin i2c si se verifica comunicatia cu mpu, care e la adresa 0x68
  mpu.initialize();
  if (!mpu.testConnection()) {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(0, 0);
    tft.println(F("MPU6050 EROARE!"));
    beepError(); while(true);
  }

  // caut cartile disponibile
  scanBooks();
  if (bookCount == 0) {
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(0, 0);
    tft.println(F("Nicio carte!"));
    beepError(); 
    while(true);
  }

  // deschid ultima carte citita
  loadLastBook();

  // intru in modul de citit sau de meniu
  if (mode == MODE_READ) 
    displayPage(currentBook, currentPage);
  else 
    displayMenu();
}

// main loop
void loop() {
  unsigned long now = millis();

  // debounce pentru autorotate
  if (now - lastOrient > ORIENT_INTERVAL) {
    lastOrient = now;
    checkOrientation();
  }

  // butonul next + debounce
  if (digitalRead(PIN_BTN_NXT) == LOW && now - lastNxt > DEBOUNCE_MS) {
    lastNxt = now;
    // in functie de modul curent, trec la urmatoarea optiune
    if (mode == MODE_MENU) {
      if (menuCursor + 1 < bookCount) { 
        // doar daca exista o urmatoare carte
        menuCursor++; 
        beepOK(); 
        displayMenu(); 
      }
      else 
        beepError();
    } else {
      if (currentPage + 1 < totalPages(currentBook)) {
        currentPage++; 
        // salvez progresul in carte pe masura ce el se produce, ca sa se salveze si daca utilizatorul nu iese din carte inainte de a inchide dispozitivul
        saveBookProgress(currentBook, currentPage); 
        beepOK();
        displayPage(currentBook, currentPage);
      } 
      else 
        beepError();
    }
  }
  // idem
  if (digitalRead(PIN_BTN_PRV) == LOW && now - lastPrv > DEBOUNCE_MS) {
    lastPrv = now;
    if (mode == MODE_MENU) {
      if (menuCursor > 0) { 
        menuCursor -- ; 
        beepOK(); 
        displayMenu(); 
      }
      else beepError();
    } else {
      if (currentPage>0) {
        currentPage--; 
        saveBookProgress(currentBook, currentPage); 
        beepOK();
        displayPage(currentBook, currentPage);
      } 
      else
        beepError();
    }
  }

  if (digitalRead(PIN_BTN_MNU) == LOW && now - lastMnu > DEBOUNCE_MS) {
    lastMnu = now;
    beepMenu();
    if (mode == MODE_MENU) {
      currentBook = menuCursor;
      currentPage = loadBookProgress(currentBook);
      saveLastBook(currentBook);
      mode = MODE_READ;
      displayPage(currentBook, currentPage);
    } else {
      saveBookProgress(currentBook, currentPage);
      saveLastBook(currentBook);
      mode = MODE_MENU;
      menuCursor = currentBook;
      displayMenu();
    }
  }
}
