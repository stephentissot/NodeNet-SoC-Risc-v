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
    static constexpr size_t kMaxPoints = 64u;
    static constexpr size_t kMaxSerializedSize = 8192u;

    PointCatalog() = default;

    void clear();
    size_t size() const;
    const PointDefinition* entries() const;
    const PointState* states() const;
    const PointCommandState* commandStates() const;

    const PointDefinition* find(const PointIdentity& id) const;
    size_t findIndex(const PointIdentity& id) const;
    PointState* findState(const PointIdentity& id);
    const PointState* findState(const PointIdentity& id) const;
    PointCommandState* findCommandState(const PointIdentity& id);
    const PointCommandState* findCommandState(const PointIdentity& id) const;
    bool upsert(const PointDefinition& definition);
    bool remove(const PointIdentity& id);
    bool updateState(const PointIdentity& id, const PointState& state);
    bool updateCommandState(const PointIdentity& id, const PointCommandState& state);

    bool loadFromJson(const char* json);
    bool saveToJson(char* out, size_t out_size) const;

    static void serializeDefinition(JsonObject obj, const PointDefinition& definition);
    static bool deserializeDefinition(PointDefinition& definition, JsonObjectConst obj);

private:
    static bool identitiesEqual(const PointIdentity& lhs, const PointIdentity& rhs);
    static void copyDefinition(PointDefinition& dst, const PointDefinition& src);

    PointDefinition entries_[kMaxPoints] = {};
    PointState states_[kMaxPoints] = {};
    PointCommandState command_states_[kMaxPoints] = {};
    size_t count_ = 0u;
};

#endif