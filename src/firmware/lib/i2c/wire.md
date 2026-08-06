Voici la signature exacte de la API Wire classique Arduino, avec les types généralement utilisés en C++/Arduino :

void begin(void)
void beginTransmission(uint8_t address)
uint8_t endTransmission(void)
uint8_t endTransmission(bool stopBit)
uint8_t requestFrom(uint8_t address, uint8_t quantity)
uint8_t requestFrom(uint8_t address, uint8_t quantity, uint8_t stopBit)
uint8_t requestFrom(uint8_t address, uint8_t quantity, uint32_t iaddress, uint8_t isize, uint8_t stopBit)
size_t write(uint8_t data)
size_t write(const uint8_t *data, size_t quantity)
size_t write(const char *str)
int available(void)
int read(void)
int peek(void)
void flush(void)
void onReceive(void (*callback)(int))
void onRequest(void (*callback)(void))
Valeurs de retour :

beginTransmission(...) : void
endTransmission(...) : uint8_t
0 = success
1 = data too long
2 = NACK on address transmit
3 = NACK on data transmit
4 = other error
requestFrom(...) : uint8_t
nombre d’octets réellement disponibles à lire
write(...) : size_t
nombre d’octets écrits
available() : int
nombre d’octets disponibles en lecture
read() : int
octet lu, ou -1 si rien
peek() : int
octet suivant sans consommer, ou -1 si rien
flush() : void
onReceive/onRequest : void