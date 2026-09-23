#pragma once
#include <string>

namespace raft{

struct applyMessage{
    bool        validCommand;
    size_t      commandIndex;
    std::string command;

    bool        validSnapshot;
    size_t      snapshotIndex;
    size_t      snapshotTerm;
    std::string snapshot;
    
    applyMessage():
        validCommand(false),
        commandIndex(-1),
        command(),
        validSnapshot(false),
        snapshotIndex(-1),
        snapshotTerm(-1),
        snapshot(){}

    applyMessage(
        bool valid_cmd,
        size_t cmd_index,
        const std::string &cmd
    ):
        validCommand(valid_cmd),
        commandIndex(cmd_index),
        command(cmd),
        validSnapshot(false),
        snapshotIndex(-1),
        snapshotTerm(-1),
        snapshot(){}

    applyMessage(
        bool valid_snapshot,
        size_t snapshot_index,
        size_t snapshot_term,
        const std::string &shot
    ):
        validCommand(false),
        commandIndex(-1),
        command(),
        validSnapshot(valid_snapshot),
        snapshotIndex(snapshot_index),
        snapshotTerm(snapshot_term),
        snapshot(shot){}

    applyMessage(
        bool valid_cmd,
        size_t cmd_index,
        const std::string &cmd,
        bool valid_snapshot,
        size_t snapshot_index,
        size_t snapshot_term,
        const std::string &shot
    ):
        validCommand(valid_cmd),
        commandIndex(cmd_index),
        command(cmd),
        validSnapshot(valid_snapshot),
        snapshotIndex(snapshot_index),
        snapshotTerm(snapshot_term),
        snapshot(shot){}

};
    
} // namespace raft
