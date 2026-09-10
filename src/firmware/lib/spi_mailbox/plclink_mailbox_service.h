#ifndef SPI_MAILBOX_PLCLINK_MAILBOX_SERVICE_H
#define SPI_MAILBOX_PLCLINK_MAILBOX_SERVICE_H

#include <cstdint>

#include "protocol_states.h"
#include "protocol_types.h"

class SpiMailbox;
class PointCatalog;
struct PointDefinition;
struct PointState;

namespace plclink_mailbox_service {

using ResolvePointStateFn = bool (*)(void* context,
                                     uint16_t point_index,
                                     const PointDefinition& definition,
                                     PointState& out_state);

using WritePointStateFn = plclink::ErrorCode (*)(void* context,
                                                 uint16_t point_index,
                                                 uint8_t expected_value_type,
                                                 uint8_t write_flags,
                                                 uint32_t value_bits,
                                                 uint8_t* out_applied_value_type,
                                                 uint32_t* out_result_sequence);

bool service(SpiMailbox& mailbox,
             PointCatalog& point_catalog,
             ResolvePointStateFn resolve_point_state,
             WritePointStateFn write_point_state,
             void* resolve_context,
             uint32_t defs_generation,
             uint32_t states_sequence);

bool pumpUpdates(SpiMailbox& mailbox,
                 PointCatalog& point_catalog,
                 ResolvePointStateFn resolve_point_state,
                 void* resolve_context,
                 uint32_t defs_generation,
                 uint32_t states_sequence);

} // namespace plclink_mailbox_service

#endif