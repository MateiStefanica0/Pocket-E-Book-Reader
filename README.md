# Pocket E-Book Reader

A portable e-book reader built on Arduino Nano that allows users to read
.txt books stored on a MicroSD card.

## Hardware

| Component | Role |
|---|---|
| Arduino Nano R3 | Main microcontroller |
| LCD ST7735 1.8" | Display |
| MicroSD module | Book and progress storage |
| MPU6050 | Autorotate sensor |
| Passive buzzer | Audio feedback |
| 3x Tactile buttons | Navigation |

## Features

* Read .txt books from MicroSD card
* Per-book progress saving (survives power off)
* Auto-resume last opened book on startup
* Autorotate with hysteresis (MPU6050)
* Menu for book selection
* Audio feedback for all actions

## Wiring

| Component | Pins |
|---|---|
| LCD ST7735 | CS=D10, DC=D9, RST=D8, MOSI=D11, SCK=D13 |
| MicroSD | CS=D4, MOSI=D11, MISO=D12, SCK=D13 |
| MPU6050 | SDA=A4, SCL=A5, AD0=GND |
| Button NEXT | D5 |
| Button PREV | D6 |
| Button MENU | D7 |
| Buzzer | D2 |

## Libraries

Adafruit_GFX, Adafruit_ST7735, SD, SPI, Wire, MPU6050

## Usage

1. Format MicroSD card as FAT32
2. Copy .txt books to root directory (8.3 filename format)
3. Upload firmware via Arduino IDE
4. Press NEXT/PREV to turn pages, MENU to enter the book selection menu

## File Structure on SD Card

/
├── BOOK1.TXT
├── BOOK2.TXT
├── P0.DAT       <- progress for book 0
├── P1.DAT       <- progress for book 1
└── LAST.DAT     <- last opened book