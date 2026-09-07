#ifndef SPI_MAILBOX_PLCLINK_MAILBOX_SERVICE_H
#define SPI_MAILBOX_PLCLINK_MAILBOX_SERVICE_H

#include <cstdint>

class SpiMailbox;
class PointCatalog;

namespace plclink_mailbox_service {

bool service(SpiMailbox& mailbox,
             const PointCatalog& point_catalog,
             uint32_t defs_generation,
             uint32_t states_sequence);

} // namespace plclink_mailbox_service

#endif