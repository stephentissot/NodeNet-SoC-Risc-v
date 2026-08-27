#pragma once

class NodeNetCommands
{
public:
  // Enum commands
  // command params are "num" (uint8_t) for sub command, "param1"(uint8_t) and "param2"(signed int) and "param3" (string)
  enum Cmd
  {
    EVENT,      // Event
    HEARTBEAT,  // Heartbeat message
    DISCOVER_REQ,   // Discover request
    DISCOVER_RES,   // Discover response
    FEATURES_REQ,   // Features request
    FEATURES_RES,   // Features response
    MASTER,         // To say I am the master !
    CMD_STATUS,
    CMD_INFO,   // Information request
                // 0: Just return "Ready"
                // 1: Number of filters
                // 2: Current position
                // 3: Current speed
                // 4: Current accelSpeed
                // 5:Current homeOffset
                // 6: Filters names (array)
                // 7: 'param1' filter name
                // 8: Motor offsets per filter (array)
                // 9:Focuser offsets per filter (array)
                // default: ALL main settings
    CMD_RESET,  // Reset command
                // 0: Reboot Esp32
                // 1: Set numberOfFilters, param1 can be 5 or 7 
                // 2: Set homing offset, param2
                // 3: set motor speed, param2
                // 4: set motor acceleration, param2
                // 5: set focuser offset (param2) for filter (param1)
                // 6: set motor offset (param2) for filter (param1)
                // 7: set filter (param1) name (param3)
                // 8: set rs485 to true (param1 = 1) or false (param1 = 0)
    CMD_TEST,   // Test command
                // 1: move to filter param1 and use param2 as homeOffset to test new home offset
    CMD_GO,     // Go to param1 filter
    CMD_GONEXT, // Go to next filter
    CMD_GOPREV, // Go to previous filter
    CMD_POS,
    CMD_MOVING,
    CMD_HOME,
    CMD_UNKNOWN,
    REMOTE_BUTTON_UP,
    REMOTE_BUTTON_DOWN,
    REMOTE_DATA_REQ,
    REMOTE_DATA_RES,
// Settings commands
    SET_RS485_TERMINATOR, // Set RS485 terminator state (param1 = 1 for true, param1 = 0 for false)
    UPDATE_PROPERTY, // Update property command : propertyName -> the name of the property to update, value -> the new value (typed, can be int bool or string) of the property
    REBOOT, // Reboot the device
    NETWORK_REQ, // Ask for network settings
    NETWORK_RES, // Response with network settings
    NETWORK_UPD, // Request to update network settings
    POINT_DEFS_REQ,
    POINT_DEFS_RES,
    POINT_STATES_REQ,
    POINT_STATES_RES,
    POINT_UPSERT,
    POINT_DELETE,
    PLC_STATUS_REQ,
    PLC_STATUS_RES,
    PLC_SLOTS_REQ,
    PLC_SLOTS_RES,
    PLC_LOAD_REQ,
    PLC_LOAD_RES,
    PLC_UPLOAD_BEGIN_REQ,
    PLC_UPLOAD_BEGIN_RES,
    PLC_UPLOAD_STATUS_REQ,
    PLC_UPLOAD_STATUS_RES,
    PLC_UPLOAD_COMMIT_REQ,
    PLC_UPLOAD_COMMIT_RES,
    PLC_UPLOAD_ABORT_REQ,
    PLC_UPLOAD_ABORT_RES,
    PLC_UPLOAD_DATA_RES,
  };

  // Parsing string -> enum
  static const Cmd parse(const char* cmd)
  {
    if (!cmd) return CMD_UNKNOWN;
    if (strcmp(cmd, "WhoIs") == 0)   return DISCOVER_REQ;
    if (strcmp(cmd, "IAm") == 0)   return DISCOVER_RES;
    if (strcmp(cmd, "FeaturesReq") == 0)   return FEATURES_REQ;
    if (strcmp(cmd, "FeaturesRes") == 0)   return FEATURES_RES;
    if (strcmp(cmd, "Master") == 0)   return MASTER;
    if (strcmp(cmd, "heartbeat") == 0)   return HEARTBEAT;
    // Filter commands
    if (strcmp(cmd, "filter.Status") == 0) return CMD_STATUS;
    if (strcmp(cmd, "filter.Go") == 0)     return CMD_GO;
    if (strcmp(cmd, "filter.Next") == 0)     return CMD_GONEXT;
    if (strcmp(cmd, "filter.Prev") == 0)     return CMD_GOPREV;
    if (strcmp(cmd, "filter.Pos") == 0)    return CMD_POS;
    if (strcmp(cmd, "filter.Home") == 0)   return CMD_HOME;
    if (strcmp(cmd, "filter.Moving") == 0)   return CMD_MOVING;
    if (strcmp(cmd, "filter.Info") == 0)   return CMD_INFO;
    if (strcmp(cmd, "filter.Reset") == 0)   return CMD_RESET;
    if (strcmp(cmd, "filter.Test") == 0)   return CMD_TEST;
    if (strcmp(cmd, "remote.buttonUp") == 0)   return REMOTE_BUTTON_UP;
    if (strcmp(cmd, "remote.buttonDown") == 0)   return REMOTE_BUTTON_DOWN;
    if (strcmp(cmd, "remote.requestData") == 0)   return REMOTE_DATA_REQ;
    if (strcmp(cmd, "remote.responseData") == 0)   return REMOTE_DATA_RES;
    if(strcmp(cmd, "rs485Terminator") == 0)   return SET_RS485_TERMINATOR;
    if(strcmp(cmd, "updateProperty") == 0)   return UPDATE_PROPERTY;
    if(strcmp(cmd, "reboot") == 0)   return REBOOT;
    if(strcmp(cmd, "networkReq") == 0)   return NETWORK_REQ;
    if(strcmp(cmd, "networkRes") == 0)   return NETWORK_RES;
    if(strcmp(cmd, "networkUpd") == 0)   return NETWORK_UPD;
    if(strcmp(cmd, "pointDefsReq") == 0)   return POINT_DEFS_REQ;
    if(strcmp(cmd, "pointDefsRes") == 0)   return POINT_DEFS_RES;
    if(strcmp(cmd, "pointStatesReq") == 0)   return POINT_STATES_REQ;
    if(strcmp(cmd, "pointStatesRes") == 0)   return POINT_STATES_RES;
    if(strcmp(cmd, "pointUpsert") == 0)   return POINT_UPSERT;
    if(strcmp(cmd, "pointDelete") == 0)   return POINT_DELETE;
    if(strcmp(cmd, "plcStatusReq") == 0)   return PLC_STATUS_REQ;
    if(strcmp(cmd, "plcStatusRes") == 0)   return PLC_STATUS_RES;
    if(strcmp(cmd, "plcSlotsReq") == 0)   return PLC_SLOTS_REQ;
    if(strcmp(cmd, "plcSlotsRes") == 0)   return PLC_SLOTS_RES;
    if(strcmp(cmd, "plcLoadReq") == 0)   return PLC_LOAD_REQ;
    if(strcmp(cmd, "plcLoadRes") == 0)   return PLC_LOAD_RES;
    if(strcmp(cmd, "plcUploadBeginReq") == 0)   return PLC_UPLOAD_BEGIN_REQ;
    if(strcmp(cmd, "plcUploadBeginRes") == 0)   return PLC_UPLOAD_BEGIN_RES;
    if(strcmp(cmd, "plcUploadStatusReq") == 0)   return PLC_UPLOAD_STATUS_REQ;
    if(strcmp(cmd, "plcUploadStatusRes") == 0)   return PLC_UPLOAD_STATUS_RES;
    if(strcmp(cmd, "plcUploadCommitReq") == 0)   return PLC_UPLOAD_COMMIT_REQ;
    if(strcmp(cmd, "plcUploadCommitRes") == 0)   return PLC_UPLOAD_COMMIT_RES;
    if(strcmp(cmd, "plcUploadAbortReq") == 0)   return PLC_UPLOAD_ABORT_REQ;
    if(strcmp(cmd, "plcUploadAbortRes") == 0)   return PLC_UPLOAD_ABORT_RES;
    if(strcmp(cmd, "plcUploadDataRes") == 0)   return PLC_UPLOAD_DATA_RES;

    return CMD_UNKNOWN;
  }

  static const char* toString(const Cmd cmd)
  {
    switch (cmd)
    {
      case HEARTBEAT: return "heartbeat";
      case CMD_STATUS: return "filter.Status";
      case CMD_GO:     return "filter.Go";
      case CMD_GONEXT: return "filter.Next";
      case CMD_GOPREV: return "filter.Prev";
      case CMD_POS:    return "filter.Pos";
      case CMD_HOME:   return "filter.Home";
      case CMD_MOVING:   return "filter.Moving";
      case CMD_INFO:   return "filter.Info";
      case CMD_RESET:   return "filter.Reset";
      case CMD_TEST:   return "filter.Test";

      case DISCOVER_REQ:   return "WhoIs";
      case DISCOVER_RES:   return "IAm";
      case FEATURES_REQ:   return "FeaturesReq";
      case FEATURES_RES:   return "FeaturesRes";
      case MASTER:   return "Master";
      case REMOTE_BUTTON_UP:   return "remote.buttonUp";
      case REMOTE_BUTTON_DOWN:   return "remote.buttonDown";
      case REMOTE_DATA_REQ: return "remote.requestData";
      case REMOTE_DATA_RES: return "remote.responseData";
      case SET_RS485_TERMINATOR: return "rs485Terminator";
      case UPDATE_PROPERTY: return "updateProperty";
      case REBOOT: return "reboot";
      case NETWORK_REQ: return "networkReq";
      case NETWORK_RES: return "networkRes";
      case NETWORK_UPD: return "networkUpd";
      case POINT_DEFS_REQ: return "pointDefsReq";
      case POINT_DEFS_RES: return "pointDefsRes";
      case POINT_STATES_REQ: return "pointStatesReq";
      case POINT_STATES_RES: return "pointStatesRes";
      case POINT_UPSERT: return "pointUpsert";
      case POINT_DELETE: return "pointDelete";
      case PLC_STATUS_REQ: return "plcStatusReq";
      case PLC_STATUS_RES: return "plcStatusRes";
      case PLC_SLOTS_REQ: return "plcSlotsReq";
      case PLC_SLOTS_RES: return "plcSlotsRes";
      case PLC_LOAD_REQ: return "plcLoadReq";
      case PLC_LOAD_RES: return "plcLoadRes";
      case PLC_UPLOAD_BEGIN_REQ: return "plcUploadBeginReq";
      case PLC_UPLOAD_BEGIN_RES: return "plcUploadBeginRes";
      case PLC_UPLOAD_STATUS_REQ: return "plcUploadStatusReq";
      case PLC_UPLOAD_STATUS_RES: return "plcUploadStatusRes";
      case PLC_UPLOAD_COMMIT_REQ: return "plcUploadCommitReq";
      case PLC_UPLOAD_COMMIT_RES: return "plcUploadCommitRes";
      case PLC_UPLOAD_ABORT_REQ: return "plcUploadAbortReq";
      case PLC_UPLOAD_ABORT_RES: return "plcUploadAbortRes";
      case PLC_UPLOAD_DATA_RES: return "plcUploadDataRes";
      default:         return "Unknown";
    }
  }
};
