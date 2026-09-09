#pragma once

#include <esp_err.h>

#include <cstddef>
#include <cstdint>

#include "protocol_defs.h"
#include "protocol_states.h"

namespace spi_link {

struct BootProgress {
	uint8_t percent;
	uint16_t loaded_points;
	uint16_t total_points;
	bool link_ready;
	bool caps_received;
	bool snapshot_started;
	bool snapshot_complete;
};

struct SnapshotInfo {
	uint32_t sequence;
	uint16_t point_count;
	uint16_t loaded_points;
	uint16_t max_payload;
	bool complete;
};

struct DefinitionsInfo {
	uint32_t generation;
	uint16_t point_count;
	uint32_t loaded_bytes;
	uint32_t total_bytes;
	bool complete;
};

struct CachedStateRecord {
	plclink::StateRecordV1 record;
	char string_value[plclink::kStateStringInlineCapacity];
};

struct PointUpdate {
	uint32_t sequence;
	uint16_t point_count;
	uint16_t loaded_points;
	bool complete;
	bool has_definition;
	CachedStateRecord state;
	plclink::DefinitionRecordV1 definition;
};

esp_err_t init();
esp_err_t poll();
bool is_busy();
BootProgress get_boot_progress();
SnapshotInfo get_snapshot_info();
DefinitionsInfo get_definitions_info();
size_t copy_state_records(plclink::StateRecordV1* out_records, size_t max_records);
size_t copy_cached_state_records(CachedStateRecord* out_records, size_t max_records);
size_t copy_definition_records(plclink::DefinitionRecordV1* out_records, size_t max_records);
bool copy_cached_state_record(size_t index, CachedStateRecord* out_record);
bool copy_definition_record(size_t index, plclink::DefinitionRecordV1* out_record);
bool pop_point_update(PointUpdate* out_update);
bool copy_string_state_by_path(const char* feature, const char* point_id, char* out_value, size_t out_size);
bool copy_bool_state_by_path(const char* feature, const char* point_id, bool* out_value);
bool copy_u32_state_by_path(const char* feature, const char* point_id, uint32_t* out_value);
esp_err_t request_states_refresh();

} // namespace spi_link
