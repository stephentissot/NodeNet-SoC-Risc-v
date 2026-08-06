#include "i2c.h"

bool I2c::probe(uint8_t addr)
{
        uint8_t dummy = 0u;
        clear_nack();
        set_address(addr);        
        write(I2C_REG_CMD, I2C_CMD_START | I2C_CMD_WRITE | I2C_CMD_STOP); // Start condition + write command + stop condition
        if(!push_data(dummy)) return false; // Send a dummy byte to trigger the ACK/NACK response
        if(!wait_idle(I2C_TIMEOUT_LOOP)) return false; // Wait for the command to be processed
        if(nack_detected()) return false; // If NACK is detected, the device did not respond
        return true;
} 






// Private methods for low-level I2C operations
bool I2c::wait_idle(uint32_t timeout_ms)
    {
        // Sécurité : si l'utilisateur passe 0 ou une valeur trop grande issue de l'ancienne macro, 
        // on force un timeout réaliste de 10 millisecondes.
        const uint32_t max_duration = (timeout_ms == 0u || timeout_ms > 1000u) ? 10u : timeout_ms;
        
        // -------------------------------------------------------------------------
        // ÉTAPE 1 : Attendre que la FIFO de commande soit totalement vide
        // -------------------------------------------------------------------------
        uint32_t start = millis();
        while (1) {
            // Lecture explicite : GCC est obligé de générer l'instruction 'lw' ici
            uint32_t fifo_stat = read(I2C_REG_FIFO_STATUS);
            
            if ((fifo_stat & I2C_FIFO_CMD_EMPTY) != 0u) {
                break; // La FIFO est vide, on sort proprement
            }
            
            if ((millis() - start) >= max_duration) {
                return false; // Timeout atteint
            }
            
            // Le 'nop' force une instruction physique, la barrière fige l'ordre
            __asm__ volatile("nop" ::: "memory");
        }

        // -------------------------------------------------------------------------
        // ÉTAPE 2 : Attendre que la machine d'état (BUSY) repasse au repos
        // -------------------------------------------------------------------------
        start = millis(); // On réinitialise le chronomètre pour la seconde attente
        while (1) {
            uint32_t status = read(I2C_REG_STATUS);
            
            if ((status & I2C_STATUS_BUSY) == 0u) {
                break; // L'IP est au repos, le bus est libre
            }
            
            if ((millis() - start) >= max_duration) {
                return false; // Timeout atteint
            }
            
            __asm__ volatile("nop" ::: "memory");
        }

        // Barrière finale avant de rendre la main au main()
        __asm__ volatile("" ::: "memory");
        return true;
    }

int I2c::write(uint8_t addr, const uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return I2C_OK;
    }

    if (nack_detected()) {
        clear_nack();
    }
    set_address(addr);
    if (len == 1u) {
        if (!push_data(buf[0])) return I2C_FIFO_ERROR;    
        asm volatile("" ::: "memory");
        if (!push_cmd(I2C_CMD_START | I2C_CMD_WRITE)) return I2C_FIFO_ERROR;

        // Attendre que la commande soit consommée avant de toucher à l'adresse pour le STOP
        const uint32_t wait_start = millis();
        while ((read(I2C_REG_FIFO_STATUS) & I2C_FIFO_CMD_EMPTY) == 0u) {
            if ((millis() - wait_start) >= I2C_TIMEOUT_LOOP) {
                return I2C_TIMEOUT;
            }
        }
        asm volatile("" ::: "memory");
        if (!push_cmd(I2C_CMD_STOP)) return I2C_FIFO_ERROR;

    } else {
        set_address(addr);
        // --- Premier octet (START + WRITE) ---
        if (!push_data(buf[0])) return I2C_FIFO_ERROR;
        asm volatile("nop" ::: "memory");
        if (!push_cmd(I2C_CMD_START | I2C_CMD_WRITE)) return I2C_FIFO_ERROR;
        
        // --- Octets intermédiaires (len >= 3) ---
        for (size_t i = 1; i + 1u < len; ++i) {
            
            // CRUCIAL : Attendre que la FIFO de commande soit TOTALEMENT VIDE.
            // Cela prouve que l'IP a consommé l'adresse du cycle précédent, 
            // on peut maintenant modifier le registre d'adresse sans écraser le passé.
            const uint32_t wait_start = millis();
            while ((read(I2C_REG_FIFO_STATUS) & I2C_FIFO_CMD_EMPTY) == 0u) {
                if ((millis() - wait_start) >= I2C_TIMEOUT_LOOP) {
                    return I2C_TIMEOUT;
                }
            }
            if (!push_data(buf[i])) return I2C_FIFO_ERROR;
            asm volatile("nop" ::: "memory");
            if (!push_cmd(I2C_CMD_WRITE)) return I2C_FIFO_ERROR;
        }
        
        // --- Dernier octet (WRITE puis STOP séparé) ---
        // Attendre que la FIFO de commande soit vide avant de préparer le dernier WRITE
        const uint32_t wait_start = millis();
        while ((read(I2C_REG_FIFO_STATUS) & I2C_FIFO_CMD_EMPTY) == 0u) {
            if ((millis() - wait_start) >= I2C_TIMEOUT_LOOP) {
                return I2C_TIMEOUT;
            }
        }

        if (!push_data(buf[len - 1u])) return I2C_FIFO_ERROR;
        
        set_address(addr);
        asm volatile("nop" ::: "memory");
        if (!push_cmd(I2C_CMD_WRITE)) return I2C_FIFO_ERROR;

        // Attendre à nouveau que la FIFO de commande se vide avant de poser le STOP
        const uint32_t stop_wait_start = millis();
        while ((read(I2C_REG_FIFO_STATUS) & I2C_FIFO_CMD_EMPTY) == 0u) {
            if ((millis() - stop_wait_start) >= I2C_TIMEOUT_LOOP) {
                return I2C_TIMEOUT;
            }
        }

        set_address(addr);
        asm volatile("nop" ::: "memory");
        if (!push_cmd(I2C_CMD_STOP)) return I2C_FIFO_ERROR;
    }

    // Attente finale de libération complète du bus physique
    if (!wait_idle(I2C_TIMEOUT_LOOP)) {
        return I2C_TIMEOUT;
    }

    if (nack_detected()) {
        clear_nack();
        return I2C_NACK;
    }

    return I2C_OK;
}

int I2c::read(uint8_t addr, uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return I2C_OK;
    }

    if (nack_detected()) {
        clear_nack();
    }

    set_address(addr);

    for (size_t i = 0; i < len; ++i) {
        uint8_t cmd = (i == 0u) ? (I2C_CMD_START | I2C_CMD_READ) : I2C_CMD_READ;
        if (i + 1u == len) {
            cmd |= I2C_CMD_STOP;
        }
        if (!push_cmd(cmd)) {
            return I2C_FIFO_ERROR;
        }
    }

    for (size_t i = 0; i < len; ++i) {
        const uint32_t start = millis();
        while ((read(I2C_REG_FIFO_STATUS) & I2C_FIFO_RD_EMPTY) != 0u) {
            if ((millis() - start) >= I2C_TIMEOUT_LOOP) {
                return I2C_TIMEOUT;
            }
        }
        buf[i] = (uint8_t)(read(I2C_REG_DATA) & 0xFFu);
    }

    if (!wait_idle(I2C_TIMEOUT_LOOP)) {
        return I2C_TIMEOUT;
    }

    if (nack_detected()) {
        clear_nack();
        return I2C_NACK;
    }

    return I2C_OK;
}    