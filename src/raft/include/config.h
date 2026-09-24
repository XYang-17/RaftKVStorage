#pragma once

#define RAFT_DEBUG                              true

#define RAFT_TIME_FACTOR                        10

#define RAFT_HEARTBEAT_SLEEP_TIME               25 * RAFT_TIME_FACTOR
#define RAFT_APPLY_INTERVAL                     10 * RAFT_TIME_FACTOR

#define RAFT_MIN_WAIT_TIME_BEFORE_ELECTION      30 * RAFT_TIME_FACTOR
#define RAFT_MAX_WAIT_TIME_BEFORE_ELECTION      50 * RAFT_TIME_FACTOR

#define RAFT_CONSENSUS_TIME                     100 * RAFT_TIME_FACTOR

#define RAFT_MAX_WAIT_TIME_FOR_READ             150 * RAFT_TIME_FACTOR

#define RAFT_COROUTINE_THREAD_NUM               1
#define RAFT_COROUTINE_AS_WORKER                false

#define RAFT_READ_THREAD_NUM                    4

#define RAFT_OK                                 "OK"
#define RAFT_ERR_NO_KEY                         "ErrNoKey"
#define RAFT_TIMEOUT                            "Timeout"
#define RAFT_ERR_WRONG_LEADER                   "ErrWrongLeader"

