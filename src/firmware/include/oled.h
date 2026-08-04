#include "../lib/i2c/i2c.h"

class OLED {
public:

    OLED(uint32_t addr) : addr_(addr) {}
    static void init() {
        // Display off
        oled_cmd(0xAE);
        // basic settings
        oled_cmd(0xD5); oled_cmd(0x80);  // clock divide
        oled_cmd(0xA8); oled_cmd(0x3F);  // multiplex
        oled_cmd(0xD3); oled_cmd(0x00);  // offset
        oled_cmd(0x40);                  // start line
        oled_cmd(0x8D); oled_cmd(0x14);  // charge pump
        oled_cmd(0x20); oled_cmd(0x00);  // horizontal addressing
        oled_cmd(0xA1);                  // remap columns
        oled_cmd(0xC8);                  // remap rows
        oled_cmd(0xDA); oled_cmd(0x12);  // COM pins
        oled_cmd(0x81); oled_cmd(0xCF);  // contrast
        oled_cmd(0xD9); oled_cmd(0xF1);  // precharge
        oled_cmd(0xDB); oled_cmd(0x40);  // VCOMH
        oled_cmd(0xA4);                  // output follows RAM
        oled_cmd(0xA6);                  // non-inverted
        oled_cmd(0xAF);                  // display on
    }

    static void test(){
        // Effacer
        uint8_t frame[128] = {0};
        oled_cmd(0x21); oled_cmd(0x00); oled_cmd(0x7F); // colonnes 0..127
        oled_cmd(0x22); oled_cmd(0x00); oled_cmd(0x07); // pages 0..7
        oled_data(frame, sizeof(frame));

        // Dessiner un carré 8x8 en haut à gauche
        for (int i = 0; i < 8; ++i) {
            frame[i] = 0xFF;
        }
        oled_cmd(0x21); oled_cmd(0x00); oled_cmd(0x07); // colonne 0..7
        oled_cmd(0x22); oled_cmd(0x00); oled_cmd(0x00); // page 0
        oled_data(frame, 8);
    }

private:
    uint32_t addr_;

    // Private methods
    static void oled_cmd(uint8_t c) {
        i2c.beginTransmission(0x3C);   // adresse 7-bit de l’OLED
        i2c.write(0x00);              // commande
        i2c.write(c);
        i2c.endTransmission();
    }

    static void oled_data(const uint8_t *buf, uint8_t len) {
        i2c.beginTransmission(0x3C);
        i2c.write(0x40);              // données
        for (uint8_t i = 0; i < len; ++i) {
            i2c.write(buf[i]);
        }
        i2c.endTransmission();
    }
};