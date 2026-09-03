#ifndef POINT_CATALOG_H
#define POINT_CATALOG_H

#include <cstddef>
#include <cstdint>
#include <stdlib.h>

extern "C" void* malloc(size_t);
extern "C" void free(void*);
extern "C" void* realloc(void*, size_t);

#include <ArduinoJson.h>

#include "PointDefinition.h"
#include "PointState.h"

class PointCatalog {
public:
    static constexpr size_t kMaxPoints = 512u;
    static constexpr size_t kMaxSerializedSize = 8192u;
    static constexpr size_t kIndexCapacity = 1024u;

    enum class PlcPointKind : uint8_t {
        None = 0u,
        EngineEnabled,
        EngineClearFault,
        SlotStart,
        SlotStop,
        SlotReset,
        SlotClearFault,
        SlotOther,
    };

    struct PlcPointMeta {
        bool is_local = false;
        bool has_slot = false;
        uint16_t slot_id = 0u;
        PlcPointKind point_kind = PlcPointKind::None;
    };

    struct BrowseDeviceMeta {
        uint16_t catalog_index = 0u;
        uint16_t feature_start = 0u;
        uint16_t feature_count = 0u;
        uint16_t encoded_features_size = 0u;
    };

    PointCatalog();

    void clear();
    size_t size() const;
    const PointDefinition* entries() const;
    const PointState* states() const;
    const PointCommandState* commandStates() const;
    static PointDefinition* slotVariableDefinitionScratch();

    const PointDefinition* find(const PointIdentity& id) const;
    size_t findIndex(const PointIdentity& id) const;
    const PlcPointMeta& plcPointMeta(size_t index) const;
    size_t browseDeviceCount() const;
    size_t findBrowseDeviceIndex(const char* device_id) const;
    const BrowseDeviceMeta& browseDeviceMeta(size_t index) const;
    const char* browseFeatureName(const BrowseDeviceMeta& meta, size_t feature_offset) const;
    PointState* findState(const PointIdentity& id);
    const PointState* findState(const PointIdentity& id) const;
    PointCommandState* findCommandState(const PointIdentity& id);
    const PointCommandState* findCommandState(const PointIdentity& id) const;
    void beginBatchUpdate();
    void endBatchUpdate();
    bool upsert(const PointDefinition& definition);
    bool remove(const PointIdentity& id);
    bool replaceSlotVariableDefinitions(uint16_t slot_id,
                                        const PointDefinition* definitions,
                                        size_t definition_count,
                                        bool& changed_out);
    bool updateState(const PointIdentity& id, const PointState& state);
    bool updateCommandState(const PointIdentity& id, const PointCommandState& state);
    bool popDirtyStateIndex(size_t& index_out);
    bool runtimeFullSyncRequired() const;
    void acknowledgeRuntimeFullSync();

    bool loadFromJson(const char* json);
    bool saveToJson(char* out, size_t out_size) const;

    static void serializeDefinition(JsonObject obj, const PointDefinition& definition);
    static void serializePersistedDefinition(JsonArray entry, const PointDefinition& definition);
    static bool deserializeDefinition(PointDefinition& definition, JsonObjectConst obj);

private:
    static constexpr uint16_t kInvalidIndex = 0xFFFFu;
    static constexpr size_t kDirtyStateQueueCapacity = kMaxPoints;

    static bool identitiesEqual(const PointIdentity& lhs, const PointIdentity& rhs);
    static void copyDefinition(PointDefinition& dst, const PointDefinition& src);
    static PlcPointMeta classifyPlcPointMeta(const PointDefinition& definition);
    static uint32_t hashIdentity(const PointIdentity& id);

    void resetIndex();
    void rebuildIndex();
    void rebuildBrowseIndex();
    size_t lookupIndex(const PointIdentity& id) const;
    void insertIndex(const PointIdentity& id, size_t index);
    void resetDirtyStateTracking();
    void requestRuntimeFullSync();
    void markStateDirty(size_t index);
    bool dirtyStateFlag(size_t index) const;
    void setDirtyStateFlag(size_t index, bool dirty);

    PointDefinition entries_[kMaxPoints] = {};
    PointCommandState command_states_[kMaxPoints] = {};
    PlcPointMeta plc_point_meta_[kMaxPoints] = {};
    BrowseDeviceMeta browse_devices_[kMaxPoints] = {};
    uint16_t browse_feature_indices_[kMaxPoints] = {};
    uint16_t index_slots_[kIndexCapacity] = {};
    size_t count_ = 0u;
    size_t browse_device_count_ = 0u;
    size_t browse_feature_count_ = 0u;
    uint16_t dirty_state_queue_[kDirtyStateQueueCapacity] = {};
    uint8_t dirty_state_flags_[(kMaxPoints + 7u) / 8u] = {};
    size_t dirty_state_queue_head_ = 0u;
    size_t dirty_state_queue_tail_ = 0u;
    size_t dirty_state_queue_count_ = 0u;
    bool runtime_full_sync_required_ = true;
    size_t batch_update_depth_ = 0u;
    bool batch_browse_rebuild_pending_ = false;
    bool batch_runtime_full_sync_pending_ = false;
};

#endif